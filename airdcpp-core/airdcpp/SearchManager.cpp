/* 
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdinc.h"
#include "SearchManager.h"

#include "ClientManager.h"
#include "LogManager.h"
#include "PathUtil.h"
#include "ScopedFunctor.h"
#include "SearchInstance.h"
#include "SearchQuery.h"
#include "SearchResult.h"
#include "SearchTypes.h"
#include "ShareManager.h"
#include "TimerManager.h"
#include "UDPServer.h"
#include "ValueGenerator.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace dcpp {

SearchManager::SearchManager() : 
	udpServer(make_unique<UDPServer>()), 
	searchTypes(make_unique<SearchTypes>([this]{ fire(SearchManagerListener::SearchTypesChanged()); })) 
{
	TimerManager::getInstance()->addListener(this);

#ifdef _DEBUG
	testSUDP();
#endif
}

SearchManager::~SearchManager() {
	TimerManager::getInstance()->removeListener(this);

	for (auto& p: searchKeys) 
		delete p.first;
}

string SearchManager::normalizeWhitespace(const string& aString){
	string::size_type found = 0;
	string normalized = aString;
	while((found = normalized.find_first_of("\t\n\r", found)) != string::npos) {
		normalized[found] = ' ';
		found++;
	}
	return normalized;
}

SearchQueueInfo SearchManager::search(const SearchPtr& aSearch) noexcept {
	StringList who;
	ClientManager::getInstance()->getOnlineClients(who);

	return search(who, aSearch);
}

SearchQueueInfo SearchManager::search(StringList& aHubUrls, const SearchPtr& aSearch, void* aOwner /* NULL */) noexcept {

	string keyStr;
	if (SETTING(ENABLE_SUDP)) {
		//generate a random key and store it so we can check the results
		uint8_t* key = new uint8_t[16];
		RAND_bytes(key, 16);
		{
			WLock l (cs);
			searchKeys.emplace_back(key, GET_TICK());
		}
		keyStr = Encoder::toBase32(key, 16);
	}

	aSearch->owner = aOwner;
	aSearch->key = keyStr;

	StringSet queued;
	uint64_t estimateSearchSpan = 0;
	string lastError;
	for(auto& hubUrl: aHubUrls) {
		auto queueTime = ClientManager::getInstance()->search(hubUrl, aSearch, lastError);
		if (queueTime) {
			estimateSearchSpan = max(estimateSearchSpan, *queueTime);
			queued.insert(hubUrl);
		}
	}

	return { queued, estimateSearchSpan, lastError };
}


SearchInstancePtr SearchManager::createSearchInstance(const string& aOwnerId, uint64_t aExpirationTick) noexcept {
	auto searchInstance = make_shared<SearchInstance>(aOwnerId, aExpirationTick);

	{
		WLock l(cs);
		searchInstances.emplace(searchInstance->getToken(), searchInstance);
	}

	fire(SearchManagerListener::SearchInstanceCreated(), searchInstance);
	return searchInstance;
}


SearchInstancePtr SearchManager::removeSearchInstance(SearchInstanceToken aToken) noexcept {
	SearchInstancePtr ret = nullptr;

	{
		WLock l(cs);
		auto i = searchInstances.find(aToken);
		if (i == searchInstances.end()) {
			return nullptr;
		}

		ret = i->second;
		searchInstances.erase(i);
	}

	fire(SearchManagerListener::SearchInstanceRemoved(), ret);
	return ret;
}

SearchInstancePtr SearchManager::getSearchInstance(SearchInstanceToken aToken) const noexcept {
	RLock l(cs);
	auto i = searchInstances.find(aToken);
	return i != searchInstances.end() ? i->second : nullptr;
}

SearchInstanceList SearchManager::getSearchInstances() const noexcept {
	SearchInstanceList ret;

	{
		RLock l(cs);
		for (auto& i : searchInstances | views::values) {
			ret.push_back(i);
		}
	}

	return ret;
}

void SearchManager::testSUDP() {
	uint8_t keyChar[16];
	string data = "URES SI30744059452 SL8 FN/Downloads/ DM1644168099 FI440 FO124 TORLHTR7KH7GV7W";
	Encoder::fromBase32("DR6AOECCMYK5DQ2VDATONKFSWU", keyChar, 16);
	auto encrypted = encryptSUDP(keyChar, data);

	string result;
	auto success = decryptSUDP(keyChar, ByteVector(begin(encrypted), end(encrypted)), encrypted.length(), result);
	dcassert(success);
	dcassert(compare(data, result) == 0);
}

string SearchManager::encryptSUDP(const uint8_t* aKey, const string& aCmd) {
	string inData = aCmd;
	uint8_t ivd[16] = { };

	// prepend 16 random bytes to message
	RAND_bytes(ivd, 16);
	inData.insert(0, (char*)ivd, 16);

	// use PKCS#5 padding to align the message length to the cypher block size (16)
	uint8_t pad = 16 - (aCmd.length() & 15);
	inData.append(pad, (char)pad);

	// encrypt it
	boost::scoped_array<uint8_t> out(new uint8_t[inData.length()]);
	memset(ivd, 0, 16);
	auto commandLength = inData.length();

#define CHECK(n) if(!(n)) { dcassert(0); }

	int len, tmpLen;
	auto ctx = EVP_CIPHER_CTX_new();
	CHECK(EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), NULL, aKey, ivd, 1));
	CHECK(EVP_CIPHER_CTX_set_padding(ctx, 0));
	CHECK(EVP_EncryptUpdate(ctx, out.get(), &len, (unsigned char*)inData.c_str(), inData.length()));
	CHECK(EVP_EncryptFinal_ex(ctx, out.get() + len, &tmpLen));
	EVP_CIPHER_CTX_free(ctx);

	dcassert((commandLength & 15) == 0);

	inData.clear();
	inData.insert(0, (char*)out.get(), commandLength);
	return inData;
}

bool SearchManager::decryptSUDP(const uint8_t* aKey, const ByteVector& aData, size_t aDataLen, string& result_) {
	boost::scoped_array<uint8_t> out(new uint8_t[aData.size()]);

	uint8_t ivd[16] = { };

	auto ctx = EVP_CIPHER_CTX_new();

#define CHECK(n) if(!(n)) { dcassert(0); }
	int len;
	CHECK(EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), NULL, aKey, ivd, 0));
	CHECK(EVP_CIPHER_CTX_set_padding(ctx, 0));
	CHECK(EVP_DecryptUpdate(ctx, out.get(), &len, aData.data(), aDataLen));
	CHECK(EVP_DecryptFinal_ex(ctx, out.get() + len, &len));
	EVP_CIPHER_CTX_free(ctx);

	// Validate padding and replace with 0-bytes.
	int padlen = out[aDataLen - 1];
	if (padlen < 1 || padlen > 16) {
		return false;
	}

	bool valid = true;
	for (auto r = 0; r < padlen; r++) {
		if (out[aDataLen - padlen + r] != padlen) {
			valid = false;
			break;
		} else {
			out[aDataLen - padlen + r] = 0;
		}
	}

	if (valid) {
		result_ = (char*)&out[0] + 16;
		return true;
	}

	return false;
}

bool SearchManager::decryptPacket(string& x, size_t aLen, const ByteVector& aBuf) {
	RLock l (cs);
	for (const auto& i: searchKeys | views::reverse) {
		if (decryptSUDP(i.first, aBuf, aLen, x)) {
			return true;
		}
	}

	return false;
}

const string& SearchManager::getPort() const { 
	return udpServer->getPort(); 
}

void SearchManager::listen() {
	udpServer->listen();
}

void SearchManager::disconnect() noexcept {
	udpServer->disconnect();
}

void SearchManager::onSR(const string& x, const string& aRemoteIP /*Util::emptyString*/) {
	string::size_type i, j;
	// Directories: $SR <nick><0x20><directory><0x20><free slots>/<total slots><0x05><Hubname><0x20>(<Hubip:port>)
	// Files:		$SR <nick><0x20><filename><0x05><filesize><0x20><free slots>/<total slots><0x05><Hubname><0x20>(<Hubip:port>)
	i = 4;
	if( (j = x.find(' ', i)) == string::npos) {
		return;
	}
	string nick = x.substr(i, j-i);
	i = j + 1;

	// A file has 2 0x05, a directory only one
	size_t cnt = count(x.begin() + j, x.end(), 0x05);

	SearchResult::Types type = SearchResult::TYPE_FILE;
	string file;
	int64_t size = 0;

	if(cnt == 1) {
		// We have a directory...find the first space beyond the first 0x05 from the back
		// (dirs might contain spaces as well...clever protocol, eh?)
		type = SearchResult::TYPE_DIRECTORY;
		// Get past the hubname that might contain spaces
		if((j = x.rfind(0x05)) == string::npos) {
			return;
		}
		// Find the end of the directory info
		if((j = x.rfind(' ', j-1)) == string::npos) {
			return;
		}
		if(j < i + 1) {
			return;
		}
		file = x.substr(i, j-i) + '\\';
	} else if(cnt == 2) {
		if( (j = x.find((char)5, i)) == string::npos) {
			return;
		}
		file = x.substr(i, j-i);
		i = j + 1;
		if( (j = x.find(' ', i)) == string::npos) {
			return;
		}
		size = Util::toInt64(x.substr(i, j-i));
	}
	i = j + 1;

	if( (j = x.find('/', i)) == string::npos) {
		return;
	}
	uint8_t freeSlots = (uint8_t)Util::toInt(x.substr(i, j-i));
	i = j + 1;
	if( (j = x.find((char)5, i)) == string::npos) {
		return;
	}
	uint8_t slots = (uint8_t)Util::toInt(x.substr(i, j-i));
	i = j + 1;
	if( (j = x.rfind(" (")) == string::npos) {
		return;
	}
	string hubName = x.substr(i, j-i);
	i = j + 2;
	if( (j = x.rfind(')')) == string::npos) {
		return;
	}

	string connection, hubEncoding;
	HintedUser user;
	if (!ClientManager::getInstance()->connectNMDCSearchResult(aRemoteIP, x.substr(i, j - i), nick, user, connection, hubEncoding)) {
		return;
	}


	string tth;
	if (hubName.compare(0, 4, "TTH:") == 0) {
		tth = hubName.substr(4);
	}

	if (tth.empty() && type == SearchResult::TYPE_FILE) {
		return;
	}

	auto adcPath = PathUtil::toAdcFile(Text::toUtf8(file, hubEncoding));
	auto sr = make_shared<SearchResult>(
		user, type, slots, freeSlots, size,
		adcPath, aRemoteIP, TTHValue(tth), Util::emptyString, 0, connection, DirectoryContentInfo::uninitialized()
	);

	fire(SearchManagerListener::SR(), sr);
}

void SearchManager::onRES(const AdcCommand& cmd, const UserPtr& aFrom, const string& aRemoteIp) {
	int freeSlots = -1;
	int64_t size = -1;
	string adcPath;
	string tth;
	string token;
	time_t date = 0;
	int files = -1, folders = -1;

	for(auto& str: cmd.getParameters()) {
		if (str.compare(0, 2, "FN") == 0) {
			adcPath = str.substr(2);
		} else if(str.compare(0, 2, "SL") == 0) {
			freeSlots = Util::toInt(str.substr(2));
		} else if(str.compare(0, 2, "SI") == 0) {
			size = Util::toInt64(str.substr(2));
		} else if(str.compare(0, 2, "TR") == 0) {
			tth = str.substr(2);
		} else if(str.compare(0, 2, "TO") == 0) {
			token = str.substr(2);
		} else if(str.compare(0, 2, "DM") == 0) {
			date = Util::parseRemoteFileItemDate(str.substr(2));
		} else if(str.compare(0, 2, "FI") == 0) {
			files = Util::toInt(str.substr(2));
		} else if(str.compare(0, 2, "FO") == 0) {
			folders = Util::toInt(str.substr(2));
		}
	}

	// Validate result
	if (freeSlots == -1 || size == -1 || adcPath.empty()) {
		return;
	}

	auto type = adcPath.back() == ADC_SEPARATOR ? SearchResult::TYPE_DIRECTORY : SearchResult::TYPE_FILE;
	if (type == SearchResult::TYPE_FILE && tth.empty()) {
		return;
	}

	// Connect the result to a correct hub
	string hubUrl, connection;
	uint8_t slots = 0;
	if (!ClientManager::getInstance()->connectADCSearchResult(aFrom->getCID(), token, hubUrl, connection, slots)) {
		return;
	}

	// Parse TTH
	TTHValue th;
	if (type == SearchResult::TYPE_DIRECTORY) {
		// Generate TTH from the directory name and size
		th = ValueGenerator::generateDirectoryTTH(type == SearchResult::TYPE_FILE ? PathUtil::getAdcFileName(adcPath) : PathUtil::getAdcLastDir(adcPath), size);
	} else {
		th = TTHValue(tth);
	}

	auto sr = make_shared<SearchResult>(HintedUser(aFrom, hubUrl), type, slots, (uint8_t)freeSlots, size,
		adcPath, aRemoteIp, th, token, date, connection, DirectoryContentInfo(folders, files));

	// Hooks
	auto error = incomingSearchResultHook.runHooksError(this, sr);
	if (error) {
		dcdebug("Hook rejection for search result %s from user %s (%s)\n", sr->getAdcPath().c_str(), ClientManager::getInstance()->getFormatedNicks(sr->getUser()).c_str(), ActionHookRejection::formatError(error).c_str());
		return;
	}

	fire(SearchManagerListener::SR(), sr);
}

void SearchManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	vector<SearchInstanceToken> expiredIds;

	{
		RLock l(cs);
		for (const auto& i: searchInstances | views::values) {
			auto expiration = i->getTimeToExpiration();
			if (expiration && *expiration <= 0) {
				expiredIds.push_back(i->getToken());
				dcdebug("Removing an expired search instance (expiration: " U64_FMT ", now: " U64_FMT ")\n", *expiration, GET_TICK());
			}
		}
	}

	for (const auto& id: expiredIds) {
		removeSearchInstance(id);
	}

	{
		WLock l(cs);
		for (auto i = searchKeys.begin(); i != searchKeys.end();) {
			if (i->second + 1000 * 60 * 15 < aTick) {
				delete i->first;
				searchKeys.erase(i);
				i = searchKeys.begin();
			} else {
				++i;
			}
		}

	}
}

void SearchManager::respond(const AdcCommand& adc, Client* aClient, OnlineUser* aUser, bool aIsUdpActive, ProfileToken aProfile) noexcept {
	auto isDirect = adc.getType() == 'D';

	string path = ADC_ROOT_STR;
	int maxResults = aIsUdpActive ? 10 : 5;
	bool replyDirect = false;

	if (isDirect) {
		adc.getParam("PA", 0, path);
		replyDirect = adc.hasFlag("RE", 0);

		string tmp;
		if (adc.getParam("MR", 0, tmp)) 
			maxResults = min(aIsUdpActive ? 20 : 10, Util::toInt(tmp));
	}

	SearchResultList results;
	SearchQuery srch(adc.getParameters(), maxResults);

	ScopedFunctor([&] {
		fire(SearchManagerListener::IncomingSearch(), aClient, aUser, srch, results, aIsUdpActive);
	});

	string token;
	adc.getParam("TO", 0, token);

	try {
		ShareManager::getInstance()->search(results, srch, aProfile, aUser->getUser(), path, token.find("/as") != string::npos);
	} catch(const ShareException& e) {
		if (replyDirect) {
			//path not found (direct search)
			AdcCommand c(AdcCommand::SEV_FATAL, AdcCommand::ERROR_FILE_NOT_AVAILABLE, e.getError(), AdcCommand::TYPE_DIRECT);
			c.setTo(aUser->getIdentity().getSID());
			c.addParam("TO", token);

			aUser->getClient()->sendHooked(c);
		}
		return;
	}

	if (!results.empty()) {
		string sudpKey;
		adc.getParam("KY", 0, sudpKey);
		for (const auto& sr: results) {
			AdcCommand cmd = sr->toRES(AdcCommand::TYPE_UDP);
			if(!token.empty())
				cmd.addParam("TO", token);
			ClientManager::getInstance()->sendUDPHooked(cmd, aUser->getUser()->getCID(), false, false, sudpKey, aUser->getHubUrl());
		}
	}

	if (replyDirect) {
		AdcCommand c(AdcCommand::SEV_SUCCESS, AdcCommand::SUCCESS, "Succeed", AdcCommand::TYPE_DIRECT);
		c.setTo(aUser->getIdentity().getSID());
		c.addParam("FC", adc.getFourCC());
		c.addParam("TO", token);
		c.addParam("RC", Util::toString(results.size()));

		aUser->getClient()->sendHooked(c);
	}
}

void SearchManager::respond(Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize, int aFileType, const string& aString, bool aIsPassive) noexcept {
	SearchResultList results;

	auto maxResults = aIsPassive ? 5 : 10;
	auto srch = SearchQuery(aString, static_cast<Search::SizeModes>(aSearchType), aSize, static_cast<Search::TypeModes>(aFileType), maxResults);
	auto shareProfile = aClient->get(HubSettings::ShareProfile);
	ShareManager::getInstance()->search(results, srch, shareProfile, nullptr, ADC_ROOT_STR, false);

	fire(SearchManagerListener::IncomingSearch(), aClient, nullptr, srch, results, !aIsPassive);

	if (results.size() > 0) {
		if (aIsPassive) {
			string name = aSeeker.substr(4);
			// Good, we have a passive seeker, those are easier...
			string str;
			for (const auto& sr: results) {
				str += sr->toSR(*aClient);
				str[str.length() - 1] = 5;
				str += Text::fromUtf8(name, aClient->get(HubSettings::NmdcEncoding));
				str += '|';
			}

			if (str.size() > 0)
				aClient->send(str);

		} else {
			try {
				string ip, port;

				Util::parseIpPort(aSeeker, ip, port);

				if (port.empty())
					port = "412";

				for (const auto& sr : results) {
					auto data = sr->toSR(*aClient);
					ClientManager::getInstance()->sendUDP(data, ip, port);
				}
			} catch (...) {
				dcdebug("Search caught error\n");
			}
		}
	}
}

} // namespace dcpp
