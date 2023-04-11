/* 
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include "AdcHub.h"
#include "AirUtil.h"
#include "ClientManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "ScopedFunctor.h"
#include "SearchInstance.h"
#include "SearchResult.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "TimerManager.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <boost/range/algorithm/copy.hpp>

const auto DEBUG_SEARCH = false;

namespace dcpp {

ResourceManager::Strings SearchManager::types[Search::TYPE_LAST] = {
	ResourceManager::ANY,
	ResourceManager::AUDIO,
	ResourceManager::COMPRESSED,
	ResourceManager::DOCUMENT,
	ResourceManager::EXECUTABLE,
	ResourceManager::PICTURE,
	ResourceManager::VIDEO,
	ResourceManager::DIRECTORY,
	ResourceManager::TTH_ROOT,
	ResourceManager::FILE
};
const string& SearchManager::getTypeStr(int aType) noexcept {
	return ResourceManager::getInstance()->getString(types[aType]);
}

bool SearchManager::isDefaultTypeStr(const string& aType) noexcept {
	 return aType.size() == 1 && aType[0] >= '0' && aType[0] <= '9';
}


string SearchType::getDisplayName() const noexcept {
	return isDefault() ? SearchManager::getTypeStr(id[0] - '0') : name;
}

bool SearchType::isDefault() const noexcept {
	return SearchManager::isDefaultTypeStr(id);
}


Search::TypeModes SearchType::getTypeMode() const noexcept {
	if (!isDefault()) {
		// Custom search type
		return Search::TYPE_ANY;
	}

	return static_cast<Search::TypeModes>(id[0] - '0');
}

SearchManager::SearchManager() {
	setSearchTypeDefaults();
	TimerManager::getInstance()->addListener(this);
	SettingsManager::getInstance()->addListener(this);

#ifdef _DEBUG
	testSUDP();
#endif
}

SearchManager::~SearchManager() {
	TimerManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);

	for(auto& p: searchKeys) 
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
		for (auto& i : searchInstances | map_values) {
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
	for (const auto& i: searchKeys | reversed) {
		if (decryptSUDP(i.first, aBuf, aLen, x)) {
			return true;
		}
	}

	return false;
}

const string& SearchManager::getPort() const { 
	return udpServer.getPort(); 
}

void SearchManager::listen() {
	udpServer.listen();
}

void SearchManager::disconnect() noexcept {
	udpServer.disconnect();
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

	auto adcPath = Util::toAdcFile(Text::toUtf8(file, hubEncoding));
	auto sr = make_shared<SearchResult>(
		user, type, slots, freeSlots, size,
		adcPath, aRemoteIP, TTHValue(tth), Util::emptyString, 0, connection, DirectoryContentInfo()
	);

	fire(SearchManagerListener::SR(), sr);
}

void SearchManager::onRES(const AdcCommand& cmd, const UserPtr& from, const string& remoteIp) {
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
			date = Util::toTimeT(str.substr(2));
		} else if(str.compare(0, 2, "FI") == 0) {
			files = Util::toInt(str.substr(2));
		} else if(str.compare(0, 2, "FO") == 0) {
			folders = Util::toInt(str.substr(2));
		}
	}

	if(freeSlots != -1 && size != -1) {
		//connect to a correct hub
		string hubUrl, connection;
		uint8_t slots = 0;
		if (!ClientManager::getInstance()->connectADCSearchResult(from->getCID(), token, hubUrl, connection, slots)) {
			return;
		}

		if (adcPath.empty()) {
			return;
		}

		auto type = adcPath.back() == ADC_SEPARATOR ? SearchResult::TYPE_DIRECTORY : SearchResult::TYPE_FILE;
		if (type == SearchResult::TYPE_FILE && tth.empty())
			return;

		TTHValue th;
		if (type == SearchResult::TYPE_DIRECTORY) {
			//calculate a TTH from the directory name and size
			th = AirUtil::getTTH(type == SearchResult::TYPE_FILE ? Util::getAdcFileName(adcPath) : Util::getAdcLastDir(adcPath), size);
		} else {
			th = TTHValue(tth);
		}
		
		auto sr = make_shared<SearchResult>(HintedUser(from, hubUrl), type, slots, (uint8_t)freeSlots, size,
			adcPath, remoteIp, th, token, date, connection, DirectoryContentInfo(folders, files));
		fire(SearchManagerListener::SR(), sr);
	}
}

void SearchManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	vector<SearchInstanceToken> expiredIds;

	{
		RLock l(cs);
		for (const auto& i: searchInstances | map_values) {
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

void SearchManager::dbgMsg(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	if (DEBUG_SEARCH) {
		LogManager::getInstance()->message(aMsg, aSeverity, STRING(SEARCH));
	} else if (aSeverity == LogMessage::SEV_WARNING || aSeverity == LogMessage::SEV_ERROR) {
#ifdef _DEBUG
		LogManager::getInstance()->message(aMsg, aSeverity, STRING(SEARCH));
		dcdebug("%s\n", aMsg.c_str());
#endif
	}
}

// Partial bundle sharing (ADC)
void SearchManager::onPBD(const AdcCommand& aCmd, const UserPtr& from) {
	dcassert(!!from);

	string remoteBundle;
	string hubIpPort;
	string tth;
	bool add = false, update = false, reply = false, notify = false, remove = false;

	for (auto& str: aCmd.getParameters()) {
		if (str.compare(0, 2, "HI") == 0) {
			hubIpPort = str.substr(2);
		} else if (str.compare(0, 2, "BU") == 0) {
			remoteBundle = str.substr(2);
		} else if (str.compare(0, 2, "TH") == 0) {
			tth = str.substr(2);
		} else if (str.compare(0, 2, "UP") == 0) { //new notification of a finished TTH
			update = true;
		} else if (str.compare(0, 2, "AD") == 0) { //add TTHList
			add = true;
		} else if (str.compare(0, 2, "RE") == 0) { //require reply
			reply = true;
		} else if (str.compare(0, 2, "NO") == 0) { //notify only, don't download TTHList
			notify = true;
		} else if (str.compare(0, 2, "RM") == 0) { //remove notifications for a selected user and bundle
			remove = true;
		} else {
			dbgMsg("PBD: unknown param " + str, LogMessage::SEV_WARNING);
		}
	}

	if (remove && !remoteBundle.empty()) {
		dbgMsg("PBD: remove finished notify", LogMessage::SEV_INFO);
		// Local bundle really...
		QueueManager::getInstance()->removeBundleNotify(from, Util::toUInt32(remoteBundle));
	}

	if (tth.empty()) {
		dbgMsg("PBD: empty TTH", LogMessage::SEV_WARNING);
		return;
	}

	auto hubUrl = ClientManager::getInstance()->getADCSearchHubUrl(from->getCID(), hubIpPort);
	if (hubUrl.empty()) {
		dbgMsg("PBD: no online hubs for a CID %s", LogMessage::SEV_WARNING);
		return;
	}

	if (update) {
		dbgMsg("PBD: add file source", LogMessage::SEV_INFO);
		QueueManager::getInstance()->updatePBDHooked(HintedUser(from, hubUrl), TTHValue(tth));
		return;
	} else if (remoteBundle.empty()) {
		dbgMsg("PBD: empty remote bundle", LogMessage::SEV_WARNING);
		return;
	}

	auto u = HintedUser(from, hubUrl);
	if (notify) {
		dbgMsg("PBD: add finished notify", LogMessage::SEV_INFO);
		QueueManager::getInstance()->addFinishedNotify(u, TTHValue(tth), remoteBundle);
	} else if (reply) {
		dbgMsg("PBD: reply required", LogMessage::SEV_INFO);

		string localBundle;
		bool sendNotify = false, sendAdd = false;
		if (QueueManager::getInstance()->checkPBDReply(u, TTHValue(tth), localBundle, sendNotify, sendAdd, remoteBundle)) {
			AdcCommand cmd = toPBD(hubIpPort, localBundle, tth, false, sendAdd, sendNotify);
			if (!ClientManager::getInstance()->sendUDP(cmd, from->getCID(), false, true)) {
				dbgMsg("PBD: reply sent", LogMessage::SEV_INFO);
			} else {
				dbgMsg("PBD: could not send reply (UDP error)", LogMessage::SEV_WARNING);
			}
		} else {
			dbgMsg("PBD: can't send reply (no bundle)", LogMessage::SEV_INFO);
		}
	}

	if (add) {
		try {
			QueueManager::getInstance()->addBundleTTHListHooked(u, remoteBundle, TTHValue(tth));
			dbgMsg("PBD: TTH list queued", LogMessage::SEV_INFO);
		} catch (const Exception& e) {
			dbgMsg("PBD: error when queueing TTH list: " + string(e.what()), LogMessage::SEV_WARNING);
		}
	}
}

// NMDC/ADC
void SearchManager::onPSR(const AdcCommand& aCmd, UserPtr from, const string& aRemoteIp) {
	if (!SETTING(USE_PARTIAL_SHARING)) {
		return;
	}

	string udpPort;
	uint32_t partialCount = 0;
	string tth;
	string hubIpPort;
	string nick;
	PartsInfo partialInfo;

	for (auto& str: aCmd.getParameters()) {
		if (str.compare(0, 2, "U4") == 0) {
			udpPort = str.substr(2);
		} else if (str.compare(0, 2, "NI") == 0) {
			nick = str.substr(2);
		} else if (str.compare(0, 2, "HI") == 0) {
			hubIpPort = str.substr(2);
		} else if (str.compare(0, 2, "TR") == 0) {
			tth = str.substr(2);
		} else if (str.compare(0, 2, "PC") == 0) {
			partialCount = Util::toUInt32(str.substr(2))*2;
		} else if (str.compare(0, 2, "PI") == 0) {
			StringTokenizer<string> tok(str.substr(2), ',');
			for (auto& i: tok.getTokens()) {
				partialInfo.push_back((uint16_t)Util::toInt(i));
			}
		}
	}

	string hubUrl;
	if (!from || from == ClientManager::getInstance()->getMe()) {
		// for NMDC support
		if (nick.empty() || hubIpPort.empty()) {
			dbgMsg("PSR: NMDC nick/hub ip:port empty", LogMessage::SEV_WARNING);
			return;
		}
		
		auto u = ClientManager::getInstance()->getNmdcSearchHintedUserUtf8(nick, hubIpPort, aRemoteIp);
		if (!u) {
			dbgMsg("PSR: result from an unknown NMDC user", LogMessage::SEV_WARNING);
			return;
		}

		dcassert(!u.hint.empty());
		from = u.user;
		hubUrl = u.hint;
	} else {
		// ADC
		hubUrl = ClientManager::getInstance()->getADCSearchHubUrl(from->getCID(), hubIpPort);
		if (hubUrl.empty()) {
			dbgMsg("PSR: result from an unknown ADC hub", LogMessage::SEV_WARNING);
			return;
		}
	}

	if (partialInfo.size() != partialCount) {
		dbgMsg("PSR: invalid size", LogMessage::SEV_WARNING);
		// what to do now ? just ignore partial search result :-/
		return;
	}

	PartsInfo outPartialInfo;
	QueueItem::PartialSource ps(from->isNMDC() ? ClientManager::getInstance()->getMyNick(hubUrl) : Util::emptyString, hubIpPort, aRemoteIp, udpPort);
	ps.setPartialInfo(partialInfo);

	QueueManager::getInstance()->handlePartialResultHooked(HintedUser(from, hubUrl), TTHValue(tth), ps, outPartialInfo);
	
	if(Util::toInt(udpPort) > 0 && !outPartialInfo.empty()) {
		try {
			AdcCommand cmd = SearchManager::getInstance()->toPSR(false, ps.getMyNick(), hubIpPort, tth, outPartialInfo);
			ClientManager::getInstance()->sendUDP(cmd, from->getCID(), false, true, Util::emptyString, hubUrl);
		} catch (const Exception& e) {
			dbgMsg("PSR: failed to send reply (" + string(e.what()) + ")", LogMessage::SEV_WARNING);
		}
	}

}

void SearchManager::respond(const AdcCommand& adc, OnlineUser& aUser, bool isUdpActive, const string& hubIpPort, ProfileToken aProfile) {
	auto isDirect = adc.getType() == 'D';
	string path = ADC_ROOT_STR, key;
	int maxResults = isUdpActive ? 10 : 5;

	bool replyDirect = false;
	if (isDirect) {
		adc.getParam("PA", 0, path);
		replyDirect = adc.hasFlag("RE", 0);

		string tmp;
		if (adc.getParam("MR", 0, tmp)) 
			maxResults = min(isUdpActive ? 20 : 10, Util::toInt(tmp));
	}

	SearchResultList results;
	SearchQuery srch(adc.getParameters(), maxResults);

	string token;
	adc.getParam("TO", 0, token);

	try {
		ShareManager::getInstance()->adcSearch(results, srch, aProfile, aUser.getUser()->getCID(), path, token.find("/as") != string::npos);
	} catch(const ShareException& e) {
		if (replyDirect) {
			//path not found (direct search)
			AdcCommand c(AdcCommand::SEV_FATAL, AdcCommand::ERROR_FILE_NOT_AVAILABLE, e.getError(), AdcCommand::TYPE_DIRECT);
			c.setTo(aUser.getIdentity().getSID());
			c.addParam("TO", token);

			aUser.getClient()->send(c);
		}
		return;
	}

	// TODO: don't send replies to passive users
	if(results.empty() && SETTING(USE_PARTIAL_SHARING) && aProfile != SP_HIDDEN) {
		string tth;
		if(!adc.getParam("TR", 0, tth))
			goto end;
			
		PartsInfo partialInfo;
		string bundle;
		bool reply = false, add = false;
		QueueManager::getInstance()->handlePartialSearch(aUser.getUser(), TTHValue(tth), partialInfo, bundle, reply, add);

		if (!partialInfo.empty()) {
			AdcCommand cmd = toPSR(isUdpActive, Util::emptyString, hubIpPort, tth, partialInfo);
			if (!ClientManager::getInstance()->sendUDP(cmd, aUser.getUser()->getCID(), false, true, Util::emptyString, aUser.getHubUrl())) {
				dbgMsg("ADC response: partial file info not empty, failed to send response", LogMessage::SEV_WARNING);
			} else {
				dbgMsg("ADC respond: partial file info not empty, response sent", LogMessage::SEV_INFO);
			}
		}
		
		if (!bundle.empty()) {
			AdcCommand cmd = toPBD(hubIpPort, bundle, tth, reply, add);
			if (!ClientManager::getInstance()->sendUDP(cmd, aUser.getUser()->getCID(), false, true, Util::emptyString, aUser.getHubUrl())) {
				dbgMsg("ADC respond: matching bundle in queue, failed to send PBD response", LogMessage::SEV_WARNING);
			} else {
				dbgMsg("ADC respond: matching bundle in queue, PBD response sent", LogMessage::SEV_INFO);
			}
		}

		goto end;
	}


	adc.getParam("KY", 0, key);
	for(const auto& sr: results) {
		AdcCommand cmd = sr->toRES(AdcCommand::TYPE_UDP);
		if(!token.empty())
			cmd.addParam("TO", token);
		ClientManager::getInstance()->sendUDP(cmd, aUser.getUser()->getCID(), false, false, key, aUser.getHubUrl());
	}

end:
	if (replyDirect) {
		AdcCommand c(AdcCommand::SEV_SUCCESS, AdcCommand::SUCCESS, "Succeed", AdcCommand::TYPE_DIRECT);
		c.setTo(aUser.getIdentity().getSID());
		c.addParam("FC", adc.getFourCC());
		c.addParam("TO", token);
		c.addParam("RC", Util::toString(results.size()));

		aUser.getClient()->send(c);
	}
}

string SearchManager::getPartsString(const PartsInfo& partsInfo) const {
	string ret;

	for(auto i = partsInfo.begin(); i < partsInfo.end(); i+=2){
		ret += Util::toString(*i) + "," + Util::toString(*(i+1)) + ",";
	}

	return ret.substr(0, ret.size()-1);
}


AdcCommand SearchManager::toPSR(bool aWantResponse, const string& aMyNick, const string& aHubIpPort, const string& aTTH, const vector<uint16_t>& aPartialInfo) const {
	AdcCommand cmd(AdcCommand::CMD_PSR, AdcCommand::TYPE_UDP);
		
	if (!aMyNick.empty()) {
		// NMDC
		auto hubUrl = ClientManager::getInstance()->findHub(aHubIpPort, true);
		cmd.addParam("NI", Text::fromUtf8(aMyNick, ClientManager::getInstance()->findHubEncoding(hubUrl)));
	}
		
	cmd.addParam("HI", aHubIpPort);
	cmd.addParam("U4", aWantResponse ? getPort() : "0");
	cmd.addParam("TR", aTTH);
	cmd.addParam("PC", Util::toString(aPartialInfo.size() / 2));
	cmd.addParam("PI", getPartsString(aPartialInfo));
	
	return cmd;
}

AdcCommand SearchManager::toPBD(const string& hubIpPort, const string& bundle, const string& aTTH, bool reply, bool add, bool notify) const {
	AdcCommand cmd(AdcCommand::CMD_PBD, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", hubIpPort);
	cmd.addParam("BU", bundle);
	cmd.addParam("TH", aTTH);
	if (notify) {
		cmd.addParam("NO1");
	} else if (reply) {
		cmd.addParam("RE1");
	}

	if (add) {
		cmd.addParam("AD1");
	}
	return cmd;
}

void SearchManager::validateSearchTypeName(const string& aName) {
	if (aName.empty() || isDefaultTypeStr(aName)) {
		throw SearchTypeException("Invalid search type name"); // TODO: localize
	}

	for (int type = Search::TYPE_ANY; type != Search::TYPE_LAST; ++type) {
		if (getTypeStr(type) == aName) {
			throw SearchTypeException("This search type already exists"); // TODO: localize
		}
	}
}

SearchTypeList SearchManager::getSearchTypes() const noexcept {
	SearchTypeList ret;

	{
		RLock l(cs);
		boost::copy(searchTypes | map_values, back_inserter(ret));
	}

	return ret;
}

void SearchManager::setSearchTypeDefaults() {
	{
		WLock l(cs);
		searchTypes.clear();

		// for conveniency, the default search exts will be the same as the ones defined by SEGA.
		const auto& searchExts = AdcHub::getSearchExts();
		for (size_t i = 0, n = searchExts.size(); i < n; ++i) {
			const auto id = string(1, '1' + i);
			searchTypes[id] = make_shared<SearchType>(id, id, searchExts[i]);
		}
	}

	fire(SearchManagerListener::SearchTypesChanged());
}

SearchTypePtr SearchManager::addSearchType(const string& aName, const StringList& aExtensions) {
	validateSearchTypeName(aName);

	auto searchType = make_shared<SearchType>(Util::toString(Util::rand()), aName, aExtensions);

	{
		WLock l(cs);
		searchTypes[searchType->getId()] = searchType;
	}

	fire(SearchManagerListener::SearchTypesChanged());
	return searchType;
}

void SearchManager::delSearchType(const string& aId) {
	validateSearchTypeName(aId);
	{
		WLock l(cs);
		searchTypes.erase(aId);
	}
	fire(SearchManagerListener::SearchTypesChanged());
}

SearchTypePtr SearchManager::modSearchType(const string& aId, const optional<string>& aName, const optional<StringList>& aExtensions) {
	auto type = getSearchType(aId);

	if (aName && !type->isDefault()) {
		type->setName(*aName);
	}

	if (aExtensions) {
		type->setExtensions(*aExtensions);
	}

	fire(SearchManagerListener::SearchTypesChanged());
	return type;
}

SearchTypePtr SearchManager::getSearchType(const string& aId) const {
	RLock l(cs);
	auto ret = searchTypes.find(aId);
	if(ret == searchTypes.end()) {
		throw SearchTypeException("No such search type"); // TODO: localize
	}
	return ret->second;
}

void SearchManager::getSearchType(int aPos, Search::TypeModes& type_, StringList& extList_, string& typeId_) {
	// Any, directory or TTH
	if (aPos < 4) {
		if (aPos == 0) {
			typeId_ = SEARCH_TYPE_ANY;
			type_ = Search::TYPE_ANY;
		} else if (aPos == 1) {
			typeId_ = SEARCH_TYPE_DIRECTORY;
			type_ = Search::TYPE_DIRECTORY;
		} else if (aPos == 2) {
			typeId_ = SEARCH_TYPE_TTH;
			type_ = Search::TYPE_TTH;
		} else if (aPos == 3) {
			typeId_ = SEARCH_TYPE_FILE;
			type_ = Search::TYPE_FILE;
		}
		return;
	}

	{
		auto typeIndex = aPos - 4;
		int counter = 0;

		RLock l(cs);
		for (auto& i : searchTypes) {
			if (counter++ == typeIndex) {
				type_ = i.second->getTypeMode();
				typeId_ = i.second->getId();
				extList_ = i.second->getExtensions();
				return;
			}
		}
	}

	throw SearchTypeException("No such search type"); 
}

void SearchManager::getSearchType(const string& aId, Search::TypeModes& type_, StringList& extList_, string& name_) {
	if (aId.empty())
		throw SearchTypeException("No such search type"); 

	// Any, directory or TTH
	if (aId[0] == SEARCH_TYPE_ANY[0] || aId[0] == SEARCH_TYPE_DIRECTORY[0] || aId[0] == SEARCH_TYPE_TTH[0]  || aId[0] == SEARCH_TYPE_FILE[0]) {
		type_ = static_cast<Search::TypeModes>(aId[0] - '0');
		name_ = getTypeStr(aId[0] - '0');
		return;
	}

	auto type = getSearchType(aId);
	extList_ = type->getExtensions();
	type_ = type->getTypeMode();
	name_ = type->getDisplayName();
}

string SearchManager::getTypeIdByExtension(const string& aExtension, bool aDefaultsOnly) const noexcept {
	auto extensionLower = Text::toLower(aExtension);

	RLock l(cs);
	for (const auto& type : searchTypes | map_values) {
		if (aDefaultsOnly && !type->isDefault()) {
			continue;
		}

		auto i = boost::find(type->getExtensions(), extensionLower);
		if (i != type->getExtensions().end()) {
			return type->getId();
		}
	}

	return Util::emptyString;
}


void SearchManager::on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
	xml.addTag("SearchTypes");
	xml.stepIn();
	{
		RLock l(cs);
		for(auto& t: searchTypes | map_values) {
			xml.addTag("SearchType", Util::toString(";", t->getExtensions()));
			xml.addChildAttrib("Id", t->getName());
			if (!t->isDefault()) {
				xml.addChildAttrib("UniqueId", t->getId());
			}
		}
	}
	xml.stepOut();
}

void SearchManager::on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
	xml.resetCurrentChild();
	if(xml.findChild("SearchTypes")) {
		searchTypes.clear();
		xml.stepIn();
		while(xml.findChild("SearchType")) {
			const string& extensions = xml.getChildData();
			if(extensions.empty()) {
				continue;
			}
			const string& name = xml.getChildAttrib("Id");
			if(name.empty()) {
				continue;
			}

			auto id = xml.getChildAttrib("UniqueId");
			if (id.empty()) {
				// Legacy/default type
				id = name;
			}

			searchTypes[id] = make_shared<SearchType>(id, name, StringTokenizer<string>(extensions, ';').getTokens());
		}
		xml.stepOut();
	}
}

} // namespace dcpp
