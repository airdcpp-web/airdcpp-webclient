/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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
#include "ClientManager.h"

#include "ConnectivityManager.h"
#include "ConnectionManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "FavoriteManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "SearchManager.h"
#include "SearchResult.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "UserCommand.h"

#include "AdcHub.h"
#include "NmdcHub.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <openssl/aes.h>
#include <openssl/rand.h>


namespace dcpp {


ClientManager::ClientManager() : udp(Socket::TYPE_UDP) {
	TimerManager::getInstance()->addListener(this);
}

ClientManager::~ClientManager() {
	TimerManager::getInstance()->removeListener(this);
}

Client* ClientManager::createClient(const string& aHubURL) {
	Client* c;
	if(strnicmp("adc://", aHubURL.c_str(), 6) == 0 || strnicmp("adcs://", aHubURL.c_str(), 7) == 0) {
		c = new AdcHub(aHubURL);
	} else {
		c = new NmdcHub(aHubURL);
	}

	{
		WLock l(cs);
		clients.emplace(const_cast<string*>(&c->getHubUrl()), c);
	}

	c->addListener(this);

	return c;
}

Client* ClientManager::getClient(const string& aHubURL) {
	RLock l (cs);
	auto p = clients.find(const_cast<string*>(&aHubURL));
	return p != clients.end() ? p->second : nullptr;
}

void ClientManager::putClient(Client* aClient) {
	fire(ClientManagerListener::ClientDisconnected(), aClient->getHubUrl());
	aClient->removeListeners();

	{
		WLock l(cs);
		clients.erase(const_cast<string*>(&aClient->getHubUrl()));
	}
	aClient->shutdown();
}

void ClientManager::setClientUrl(const string& aOldUrl, const string& aNewUrl) {
	WLock l (cs);
	auto p = clients.find(const_cast<string*>(&aOldUrl));
	if (p != clients.end()) {
		auto c = p->second;
		clients.erase(p);
		c->setHubUrl(aNewUrl);
		clients.emplace(const_cast<string*>(&c->getHubUrl()), c);
	}
}

StringList ClientManager::getHubUrls(const CID& cid) const {
	StringList lst;

	RLock l(cs);
	OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&cid));
	for(auto i = op.first; i != op.second; ++i) {
		lst.push_back(i->second->getClientBase().getHubUrl());
	}
	return lst;
}

OrderedStringSet ClientManager::getHubSet(const CID& cid) const {
	OrderedStringSet lst;

	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&cid));
	for(auto i = op.first; i != op.second; ++i) {
		lst.insert(i->second->getClientBase().getHubUrl());
	}
	return lst;
}

StringList ClientManager::getHubNames(const CID& cid) const {
	StringList lst;

	RLock l(cs);
	OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&cid));
	for(auto i = op.first; i != op.second; ++i) {
		lst.push_back(i->second->getClientBase().getHubName());		
	}

	sort(lst.begin(), lst.end());
	return lst;
}

StringPairList ClientManager::getHubs(const CID& cid) {
	RLock l(cs);
	StringPairList lst;
	auto op = onlineUsers.equal_range(const_cast<CID*>(&cid));
	for(auto i = op.first; i != op.second; ++i) {
		lst.emplace_back(i->second->getClient().getHubUrl(), i->second->getClient().getHubName());
	}
	return lst;
}

StringList ClientManager::getNicks(const CID& cid) const {
	set<string> ret;

	{
		RLock l(cs);
		OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&cid));
		for(auto i = op.first; i != op.second; ++i) {
			ret.insert(i->second->getIdentity().getNick());
		}

		if(ret.empty()) {
			// offline
			auto i = nicks.find(const_cast<CID*>(&cid));
			if(i != nicks.end()) {
				ret.insert(i->second);
			} else {
				ret.insert('{' + cid.toBase32() + '}');
			}
		}
	}

	return StringList(ret.begin(), ret.end());
}

map<string, Identity> ClientManager::getIdentities(const UserPtr &u) const {
	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&u->getCID()));
	auto ret = map<string, Identity>();
	for(auto i = op.first; i != op.second; ++i) {
		ret.insert(make_pair(i->second->getHubUrl(), i->second->getIdentity()));
	}

	return ret;
}

string ClientManager::getNick(const UserPtr& u, const string& hint, bool allowFallback /*true*/) const {
	RLock l(cs);
	OnlinePairC p;
	auto ou = findOnlineUserHint(u->getCID(), hint, p);
	if(ou)
		return ou->getIdentity().getNick();

	if(allowFallback && p.first != p.second){
		return p.first->second->getIdentity().getNick();
	}

	return Util::emptyString;

}

string ClientManager::getField(const CID& cid, const string& hint, const char* field) const {
	OnlinePairC p;

	RLock l(cs);
	auto u = findOnlineUserHint(cid, hint, p);
	if(u) {
		auto value = u->getIdentity().get(field);
		if(!value.empty()) {
			return value;
		}
	}

	for(auto i = p.first; i != p.second; ++i) {
		auto value = i->second->getIdentity().get(field);
		if(!value.empty()) {
			return value;
		}
	}

	return Util::emptyString;
}

string ClientManager::getConnection(const HintedUser& aUser) const {
	RLock l(cs);
	auto ou = findOnlineUser(aUser);
	if (ou) {
		if(ou->getIdentity().get("US").empty())
			return ou->getIdentity().getConnection();
		else
			return Util::formatBytes(ou->getIdentity().get("US")) + "/s";
	}

	return STRING(OFFLINE);
}

string ClientManager::getDLSpeed(const CID& cid) const {
	RLock l(cs);
	OnlineIterC i = onlineUsers.find(const_cast<CID*>(&cid));
	if(i != onlineUsers.end()) {
		return Util::formatBytes(i->second->getIdentity().get("DS")) + "/s";
	}
	return STRING(OFFLINE);
}

uint8_t ClientManager::getSlots(const CID& cid) const
{
	RLock l(cs);
	OnlineIterC i = onlineUsers.find(const_cast<CID*>(&cid));
	if(i != onlineUsers.end()) {
		return static_cast<uint8_t>(Util::toInt(i->second->getIdentity().get("SL")));
	}
	return 0;
}

bool ClientManager::isConnected(const string& aUrl) const {
	RLock l(cs);

	auto i = clients.find(const_cast<string*>(&aUrl));
	return i != clients.end();
}

string ClientManager::findHub(const string& ipPort) const {
	string ip;
	string port = "411";
	string::size_type i = ipPort.rfind(':');
	if(i == string::npos) {
		ip = ipPort;
	} else {
		ip = ipPort.substr(0, i);
		port = ipPort.substr(i+1);
	}

	string url;

	RLock l(cs);
	for(const auto c: clients | map_values) {
		if(c->getIp() == ip) {
			// If exact match is found, return it
			if(c->getPort() == port)
				return c->getHubUrl();

			// Port is not always correct, so use this as a best guess...
			url = c->getHubUrl();
		}
	}

	return url;
}

const string& ClientManager::findHubEncoding(const string& aUrl) const {
	RLock l(cs);

	auto i = clients.find(const_cast<string*>(&aUrl));
	if(i != clients.end()) {
		return i->second->getEncoding();
	}
	return Text::systemCharset;
}

UserPtr ClientManager::findLegacyUser(const string& aNick) const noexcept {
	if (aNick.empty())
		return UserPtr();

	RLock l(cs);
	for(const auto& ou: onlineUsers | map_values) {
		if(ou->getUser()->isSet(User::NMDC) && Util::stricmp(ou->getIdentity().getNick(), aNick) == 0)
			return ou->getUser();
	}
	return UserPtr();
}

UserPtr ClientManager::getUser(const string& aNick, const string& aHubUrl) noexcept {
	CID cid = makeCid(aNick, aHubUrl);
	{
		RLock l(cs);
		auto ui = users.find(const_cast<CID*>(&cid));
		if(ui != users.end()) {
			ui->second->setFlag(User::NMDC);
			return ui->second;
		}
	}

	UserPtr p(new User(cid));
	p->setFlag(User::NMDC);

	WLock l(cs);
	users.emplace(const_cast<CID*>(&p->getCID()), p);

	return p;
}

UserPtr ClientManager::getUser(const CID& cid) noexcept {
	{
		RLock l(cs);
		auto ui = users.find(const_cast<CID*>(&cid));
		if(ui != users.end()) {
			return ui->second;
		}
	}

	UserPtr p(new User(cid));

	WLock l(cs);
	users.emplace(const_cast<CID*>(&p->getCID()), p);
	return p;
}

UserPtr ClientManager::findUser(const CID& cid) const noexcept {
	RLock l(cs);
	auto ui = users.find(const_cast<CID*>(&cid));
	if(ui != users.end()) {
		return ui->second;
	}
	return 0;
}

UserPtr ClientManager::findUserByNick(const string& aNick, const string& aHubUrl) const noexcept {
	RLock l(cs);
	for(const auto c: clients | map_values) {
		if(c->getHubUrl() == aHubUrl) {
			return c->findUser(aNick)->getUser();
		}
	}
	return UserPtr();
}

// deprecated
bool ClientManager::isOp(const UserPtr& user, const string& aHubUrl) const {
	RLock l(cs);
	OnlinePairC p = onlineUsers.equal_range(const_cast<CID*>(&user->getCID()));
	for(auto i = p.first; i != p.second; ++i) {
		if(i->second->getClient().getHubUrl() == aHubUrl) {
			return i->second->getIdentity().isOp();
		}
	}
	return false;
}

bool ClientManager::isStealth(const string& aHubUrl) const {
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&aHubUrl));
	if(i != clients.end()) {
		return i->second->getStealth();
	}
	return false;
}

CID ClientManager::makeCid(const string& aNick, const string& aHubUrl) const noexcept {
	string n = Text::toLower(aNick);
	TigerHash th;
	th.update(n.c_str(), n.length());
	th.update(Text::toLower(aHubUrl).c_str(), aHubUrl.length());
	// Construct hybrid CID from the bits of the tiger hash - should be
	// fairly random, and hopefully low-collision
	return CID(th.finalize());
}

void ClientManager::putOnline(OnlineUser* ou) noexcept {
	{
		WLock l(cs);
		onlineUsers.emplace(const_cast<CID*>(&ou->getUser()->getCID()), ou);
	}
	
	if(!ou->getUser()->isOnline()) {
		ou->getUser()->setFlag(User::ONLINE);
		fire(ClientManagerListener::UserConnected(), *ou, true);
	} else {
		fire(ClientManagerListener::UserConnected(), *ou, false);
	}
}

void ClientManager::putOffline(OnlineUser* ou, bool disconnect) noexcept {
	OnlineIter::difference_type diff = 0;
	{
		WLock l(cs);
		auto op = onlineUsers.equal_range(const_cast<CID*>(&ou->getUser()->getCID()));
		dcassert(op.first != op.second);
		for(auto i = op.first; i != op.second; ++i) {
			auto ou2 = i->second;
			if(ou == ou2) {
				diff = distance(op.first, op.second);
				onlineUsers.erase(i);
				break;
			}
		}
	}

	if(diff == 1) { //last user
		UserPtr& u = ou->getUser();
		u->unsetFlag(User::ONLINE);
		updateUser(*ou);
		if(disconnect)
			ConnectionManager::getInstance()->disconnect(u);
		fire(ClientManagerListener::UserDisconnected(), u, true);
	} else if(diff > 1) {
		fire(ClientManagerListener::UserDisconnected(), *ou, false);
	}
}

void ClientManager::listProfiles(const UserPtr& aUser, ProfileTokenSet& profiles) {
	RLock l(cs);
	OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));
	for(auto i = op.first; i != op.second; ++i) {
		profiles.insert(i->second->getClient().getShareProfile());
	}
}

optional<ProfileToken> ClientManager::findProfile(UserConnection& p, const string& userSID) {
	optional<ProfileToken> ret;

	if(!userSID.empty()) {
		RLock l(cs);
		auto op = onlineUsers.equal_range(const_cast<CID*>(&p.getUser()->getCID())) | map_values;
		for(const auto& ou: op) {
			if(compare(ou->getIdentity().getSIDString(), userSID) == 0) {
				p.setHubUrl(ou->getClient().getAddress());
				ret = ou->getClient().getShareProfile();
				break;
			}
		}

		//don't accept invalid SIDs
		return ret;
	}

	//no SID specified, find with hint.
	OnlinePairC op;

	RLock l(cs);
	auto ou = findOnlineUserHint(p.getUser()->getCID(), p.getHubUrl(), op);
	if(ou) {
		ret = ou->getClient().getShareProfile();
	} else if(op.first != op.second) {
		//pick a random profile
		ret = op.first->second->getClient().getShareProfile();
	}

	return ret;
}

bool ClientManager::isActive() const {
	if (CONNSETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_PASSIVE && CONNSETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_DISABLED)
		return true;

	if (CONNSETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_PASSIVE && CONNSETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_DISABLED)
		return true;

	return FavoriteManager::getInstance()->hasActiveHubs();
}

bool ClientManager::isActive(const string& aHubUrl) const {
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&aHubUrl));
	if(i != clients.end() && i->second->isConnected()) {
		return i->second->isActive();		
	}
	return false;
}

string ClientManager::findMySID(const UserPtr& aUser, string& aHubUrl, bool allowFallback) {
	if(!aHubUrl.empty()) { // we cannot find the correct SID without a hubUrl
		OnlinePairC op;

		RLock l(cs);
		OnlineUser* u = findOnlineUserHint(aUser->getCID(), aHubUrl, op);
		if(u) {
			return (&u->getClient())->getMyIdentity().getSIDString();
		} else if (allowFallback) {
			aHubUrl = op.first->second->getClient().getHubUrl();
			return op.first->second->getClient().getMyIdentity().getSIDString();
		}
	}

	return Util::emptyString;
}

OnlineUser* ClientManager::findOnlineUserHint(const CID& cid, const string& hintUrl, OnlinePairC& p) const {
	p = onlineUsers.equal_range(const_cast<CID*>(&cid));
	if(p.first == p.second) // no user found with the given CID.
		return 0;

	if(!hintUrl.empty()) {
		for(auto i = p.first; i != p.second; ++i) {
			OnlineUser* u = i->second;
			if(u->getClientBase().getHubUrl() == hintUrl) {
				return u;
			}
		}
	}

	return 0;
}

pair<int64_t, int> ClientManager::getShareInfo(const HintedUser& user) const {
	RLock l (cs);
	auto ou = findOnlineUser(user);
	if (ou) {
		return make_pair(Util::toInt64(ou->getIdentity().getShareSize()), Util::toInt(ou->getIdentity().getSharedFiles()));
	}

	return make_pair(0, 0);
}

void ClientManager::getUserInfoList(const UserPtr user, User::UserInfoList& aList_) const {
	RLock l(cs);
	auto p = onlineUsers.equal_range(const_cast<CID*>(&user->getCID()));

	for(auto i = p.first; i != p.second; ++i) {
		auto ou = i->second;
		aList_.emplace_back(ou->getHubUrl(), ou->getClient().getHubName(), Util::toInt64(ou->getIdentity().getShareSize()));
	}
}

OnlineUser* ClientManager::findOnlineUser(const HintedUser& user) const {
	return findOnlineUser(user.user->getCID(), user.hint);
}

OnlineUser* ClientManager::findOnlineUser(const CID& cid, const string& hintUrl) const {
	OnlinePairC p;
	OnlineUser* u = findOnlineUserHint(cid, hintUrl, p);
	if(u) // found an exact match (CID + hint).
		return u;

	if(p.first == p.second) // no user found with the given CID.
		return 0;

	// ok, hub not private, return a random user that matches the given CID but not the hint.
	return p.first->second;
}

bool ClientManager::connect(const UserPtr& aUser, const string& aToken, bool allowUrlChange, string& lastError_, string& hubHint_, bool& isProtocolError) {
	RLock l(cs);
	OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));

	auto connectUser = [&] (OnlineUser* ou) -> bool {
		isProtocolError = false;
		auto ret = ou->getClientBase().connect(*ou, aToken, lastError_);
		if (ret == AdcCommand::SUCCESS) {
			return true;
		}

		//get the error string
		if (ret == AdcCommand::ERROR_TLS_REQUIRED) {
			isProtocolError = true;
			lastError_ = STRING(SOURCE_NO_ENCRYPTION);
		} else if (ret == AdcCommand::ERROR_PROTOCOL_UNSUPPORTED) {
			isProtocolError = true;
			lastError_ = STRING_F(REMOTE_PROTOCOL_UNSUPPORTED, lastError_);
		} else if (ret == AdcCommand::ERROR_BAD_STATE) {
			lastError_ = STRING(CONNECTING_IN_PROGRESS);
		} else if (ret == AdcCommand::ERROR_PROTOCOL_GENERIC) {
			isProtocolError = true;
			lastError_ = STRING(NO_NATT_SUPPORT);
		}

		return false;
	};

	//prefer the hinted hub
	auto p = boost::find_if(op, [&hubHint_](pair<CID*, OnlineUser*> ouc) { return ouc.second->getHubUrl() == hubHint_; });
	if (p != op.second && connectUser(p->second)) {
		return true;
	}

	if (!allowUrlChange) {
		return false;
	}

	//connect via any available hub
	for(auto i = op.first; i != op.second; ++i) {
		if (connectUser(p->second)) {
			hubHint_ = p->second->getHubUrl();
			return true;
		}
	}

	return false;
}

void ClientManager::privateMessage(const HintedUser& user, const string& msg, bool thirdPerson) {
	RLock l(cs);
	OnlineUser* u = findOnlineUser(user);
	
	if(u) {
		u->getClientBase().privateMessage(u, msg, thirdPerson);
	}
}

void ClientManager::userCommand(const HintedUser& user, const UserCommand& uc, ParamMap& params, bool compatibility) {
	RLock l(cs);
	/** @todo we allow wrong hints for now ("false" param of findOnlineUser) because users
	 * extracted from search results don't always have a correct hint; see
	 * SearchManager::onRES(const AdcCommand& cmd, ...). when that is done, and SearchResults are
	 * switched to storing only reliable HintedUsers (found with the token of the ADC command),
	 * change this call to findOnlineUserHint. */
	OnlineUser* ou = findOnlineUser(user.user->getCID(), user.hint.empty() ? uc.getHub() : user.hint);
	if(!ou)
		return;

	ou->getIdentity().getParams(params, "user", compatibility);
	ou->getClient().getHubIdentity().getParams(params, "hub", false);
	ou->getClient().getMyIdentity().getParams(params, "my", compatibility);
	ou->getClient().sendUserCmd(uc, params);
}

bool ClientManager::sendUDP(AdcCommand& cmd, const CID& cid, bool noCID /*false*/, bool noPassive /*false*/, const string& aKey /*Util::emptyString*/, const string& aHubUrl /*Util::emptyString*/) {
	RLock l(cs);
	auto u = findOnlineUser(cid, aHubUrl);
	if(u) {
		if(cmd.getType() == AdcCommand::TYPE_UDP && !u->getIdentity().isUdpActive()) {
			if(u->getUser()->isNMDC() || noPassive)
				return false;
			cmd.setType(AdcCommand::TYPE_DIRECT);
			cmd.setTo(u->getIdentity().getSID());
			u->getClient().send(cmd);
		} else {
			try {
				COMMAND_DEBUG(cmd.toString(), DebugManager::TYPE_CLIENT_UDP, DebugManager::OUTGOING, u->getIdentity().getIp());
				auto cmdStr = noCID ? cmd.toString() : cmd.toString(getMe()->getCID());
				if (!aKey.empty() && Encoder::isBase32(aKey.c_str())) {
					uint8_t keyChar[16];
					Encoder::fromBase32(aKey.c_str(), keyChar, 16);

					uint8_t ivd[16] = { };

					// prepend 16 random bytes to message
					RAND_bytes(ivd, 16);
					cmdStr.insert(0, (char*)ivd, 16);
					
					// use PKCS#5 padding to align the message length to the cypher block size (16)
					uint8_t pad = 16 - (cmdStr.length() & 15);
					cmdStr.append(pad, (char)pad);

					// encrypt it
					uint8_t* out = new uint8_t[cmdStr.length()];
					memset(ivd, 0, 16);
					int aLen = cmdStr.length();

					AES_KEY key;
					AES_set_encrypt_key(keyChar, 128, &key);
					AES_cbc_encrypt((unsigned char*)cmdStr.c_str(), out, cmdStr.length(), &key, ivd, AES_ENCRYPT);

					dcassert((aLen & 15) == 0);

					cmdStr.clear();
					cmdStr.insert(0, (char*)out, aLen);
					delete[] out;
				}
				udp.writeTo(u->getIdentity().getIp(), u->getIdentity().getUdpPort(), cmdStr);
			} catch(const SocketException&) {
				dcdebug("Socket exception sending ADC UDP command\n");
			}
		}
		return true;
	}
	return false;
}

void ClientManager::infoUpdated() {
	RLock l(cs);
	for(auto c: clients | map_values) {
		if(c->isConnected()) {
			c->callAsync([c] { c->info(false); });
		}
	}
}

void ClientManager::resetProfiles(const ProfileTokenList& aProfiles, ShareProfilePtr aDefaultProfile) {
	RLock l(cs);
	for(auto pt: aProfiles) {
		for(auto c: clients | map_values) {
			if (c->getShareProfile() == pt) {
				c->setShareProfile(SP_DEFAULT);
				c->callAsync([c] { c->info(false); });
			}
		}
	}
}

void ClientManager::on(NmdcSearch, Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize, 
									int aFileType, const string& aString, bool isPassive) noexcept
{
	fire(ClientManagerListener::IncomingSearch(), aString);

	bool hideShare = aClient->getShareProfile() == SP_HIDDEN;

	SearchResultList l;
	ShareManager::getInstance()->search(l, aString, aSearchType, aSize, aFileType, isPassive ? 5 : 10, hideShare);
	if(l.size() > 0) {
		if(isPassive) {
			string name = aSeeker.substr(4);
			// Good, we have a passive seeker, those are easier...
			string str;
			for(const auto& sr: l) {
				str += sr->toSR(*aClient);
				str[str.length()-1] = 5;
				str += Text::fromUtf8(name, aClient->getEncoding());
				str += '|';
			}
			
			if(str.size() > 0)
				aClient->send(str);
			
		} else {
			try {
				string ip, file, proto, query, fragment, port;

				Util::decodeUrl(aSeeker, proto, ip, port, file, query, fragment);
				ip = Socket::resolve(ip);
				
				if(port.empty()) 
					port = "412";

				for(const auto& sr: l)
					udp.writeTo(ip, port, sr->toSR(*aClient));

			} catch(...) {
				dcdebug("Search caught error\n");
			}
		}
	} else if(!isPassive && (aFileType == SearchManager::TYPE_TTH) && (aString.compare(0, 4, "TTH:") == 0)) {
		if(SETTING(EXTRA_PARTIAL_SLOTS) == 0) //disable partial uploads by setting 0
			return;

		PartsInfo partialInfo;
		string bundle;
		bool add=false, reply=false;
		TTHValue aTTH(aString.substr(4));
		if(!QueueManager::getInstance()->handlePartialSearch(NULL, aTTH, partialInfo, bundle, reply, add)) {
			return;
		}
		
		string ip, file, proto, query, fragment, port;
		Util::decodeUrl(aSeeker, proto, ip, port, file, query, fragment);
		
		try {
			AdcCommand cmd = SearchManager::getInstance()->toPSR(true, aClient->getMyNick(), aClient->getIpPort(), aTTH.toBase32(), partialInfo);
			udp.writeTo(Socket::resolve(ip), port, cmd.toString(getMe()->getCID()));
		} catch(...) {
			dcdebug("Partial search caught error\n");		
		}
	}
}
void ClientManager::onSearch(const Client* c, const AdcCommand& adc, const OnlineUser& from, bool directSearch) noexcept {
	// Filter own searches
	fire(ClientManagerListener::IncomingADCSearch(), adc);
	if(from.getUser() == me)
		return;

	bool isUdpActive = from.getIdentity().isUdpActive();
	if (isUdpActive) {
		//check that we have a common IP protocol available (we don't want to send responses via wrong hubs)
		const auto& me = c->getMyIdentity();
		if (me.getIp4().empty() || !from.getIdentity().isUdp4Active()) {
			if (me.getIp6().empty() || !from.getIdentity().isUdp6Active()) {
				return;
			}
		}
	}

	if (directSearch)
		SearchManager::getInstance()->respondDirect(adc, from, isUdpActive, c->getShareProfile());
	else
		SearchManager::getInstance()->respond(adc, from, isUdpActive, c->getIpPort(), c->getShareProfile());
}

uint64_t ClientManager::search(string& who, SearchPtr aSearch) {
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&who));
	if(i != clients.end() && i->second->isConnected()) {
		return i->second->queueSearch(move(aSearch));		
	}
	return 0;
}

void ClientManager::directSearch(const HintedUser& user, int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList, const string& aDir) {
	RLock l (cs);
	auto ou = findOnlineUser(user);
	if (ou) {
		ou->getClientBase().directSearch(*ou, aSizeMode, aSize, aFileType, aString, aToken, aExtList, aDir);
	}
}

void ClientManager::getOnlineClients(StringList& onlineClients) {
	RLock l (cs);
	for (auto c: clients | map_values) {
		if (c->isConnected())
			onlineClients.push_back(c->getHubUrl());
	}
}

void ClientManager::on(TimerManagerListener::Minute, uint64_t /*aTick*/) noexcept {
	{
		WLock l(cs);

		// Collect some garbage...
		auto i = users.begin();
		while(i != users.end()) {
			if(i->second->unique()) {
				auto n = nicks.find(const_cast<CID*>(&i->second->getCID()));
				if(n != nicks.end()) 
					nicks.erase(n);
				users.erase(i++);
			} else {
				++i;
			}
		}
	}

	RLock l (cs);
	for(auto c: clients | map_values)
		c->callAsync([c] { c->info(false); });
}

string ClientManager::getClientStats() {
	RLock l(cs);
	int allUsers = onlineUsers.size();
	int uniqueUsers = users.size();
	string lb = "\n";
	string ret;
	ret += lb;
	ret += lb;
	ret += "All users: " + Util::toString(allUsers) + lb;
	ret += "Unique users: " + Util::toString(uniqueUsers) + " (" + Util::toString(((double)uniqueUsers/(double)allUsers)*100.00) + "%)" + lb;
	ret += lb;
	ret += lb;
	ret += "Clients";
	ret += lb;

	//typedef boost::bimaps::bimap<string, int> results_bimap;
    //typedef results_bimap::value_type position;
	//boost::bimaps::bimap<string, double> clientNames;

	//results_bimap clientNames;
	map<string, double> clientNames;
	for(auto ou: onlineUsers | map_values) {
		if (!ou->getIdentity().isBot()) {
			auto app = ou->getIdentity().getApplication();
			auto pos = app.find(" ");

			if (pos != string::npos) {
				clientNames[app.substr(0, pos)]++;
				//clientNames.value_comp();
				//clientNames.insert(position(app, 0));
			} else {
				clientNames["Unknown"]++;
			}
		}
	}

	/*auto countCompare = [] (int i,int j) -> bool {
		return (i<j);
	};*/

	auto countCompare = [] (pair<string, int> i, pair<string, int> j) -> bool {
		return (i.second > j.second);
	};
	//bool myfunction (int i,int j) { return (i<j); }

	//sort(clientNames.begin(), clientNames.end(), countCompare);
	vector<double> dv;
	//boost::copy(countCompare | boost::adaptors::map_values, dv.begin());

	vector<pair<string, int> > print(clientNames.begin(), clientNames.end());
	sort(print.begin(), print.end(), countCompare);
	//boost::sort(clientNames | boost::adaptors::map_values, countCompare);
	for(auto& p: print) {
		//std::stringstream a_stream;
		//std::string the_string = Util::toString(i->second) + " (" + Util::toString((i->second/allUsers)*100.00) + "%)" + lb;
		//a_stream << std::setiosflags ( std::ios_base::left ) << std::setw ( 20 ) << i->first << the_string.c_str() << std::endl;
		//std::cout << the_string.c_str();
		//ret += a_stream.get();
		//ret += a_stream.str();
		ret += p.first + ":\t\t" + Util::toString(p.second) + " (" + Util::toString(((double)p.second/(double)allUsers)*100.00) + "%)" + lb;
		//printf("%-20s", ret);
	}

	/*for(auto i = clientNames.begin(); i != clientNames.end(); ++i) {
		ret += i->first + ": " + Util::toString(i->second) + " (" + Util::toString((i->second/allUsers)*100.00) + "%)" + lb;
	}*/

	return ret;
}

UserPtr& ClientManager::getMe() {
	if(!me) {
		me = new User(getMyCID());

		WLock l(cs);
		users.emplace(const_cast<CID*>(&me->getCID()), me);
	}
	return me;
}

const CID& ClientManager::getMyPID() {
	if(pid.isZero())
		pid = CID(SETTING(PRIVATE_ID));
	return pid;
}

CID ClientManager::getMyCID() {
	TigerHash tiger;
	tiger.update(getMyPID().data(), CID::SIZE);
	return CID(tiger.finalize());
}

void ClientManager::updateUser(const OnlineUser& user) noexcept {
	updateNick(user.getUser(), user.getIdentity().getNick());
}

void ClientManager::updateNick(const UserPtr& user, const string& nick) noexcept {
	if(!nick.empty()) {
		{
			RLock l (cs);
			auto i = nicks.find(const_cast<CID*>(&user->getCID()));
			if(i != nicks.end()) {
				i->second = nick;
				return;
			}
		}

		//not found, insert new
		WLock l(cs);
		nicks[const_cast<CID*>(&user->getCID())] = nick;
	}
}

string ClientManager::getMyNick(const string& hubUrl) const {
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&hubUrl));
	if(i != clients.end()) {
		return i->second->getMyIdentity().getNick();
	}
	return Util::emptyString;
}

void ClientManager::cancelSearch(void* aOwner) {
	RLock l(cs);
	for(auto c: clients | map_values)
		c->cancelSearch(aOwner);
}


void ClientManager::on(Connected, const Client* c) noexcept {
	fire(ClientManagerListener::ClientConnected(), c);
}

void ClientManager::on(UserUpdated, const Client*, const OnlineUserPtr& user) noexcept {
	fire(ClientManagerListener::UserUpdated(), *user);
}

void ClientManager::on(UsersUpdated, const Client*, const OnlineUserList& l) noexcept {
	for(auto i = l.begin(), iend = l.end(); i != iend; ++i) {
		updateUser(**i);
		fire(ClientManagerListener::UserUpdated(), *(*i)); 
	}
}

void ClientManager::on(HubUpdated, const Client* c) noexcept {
	fire(ClientManagerListener::ClientUpdated(), c);
}

void ClientManager::on(Failed, const string& aHubUrl, const string& /*aLine*/) noexcept {
	fire(ClientManagerListener::ClientDisconnected(), aHubUrl);
}

void ClientManager::on(HubUserCommand, const Client* client, int aType, int ctx, const string& name, const string& command) noexcept {
	if(SETTING(HUB_USER_COMMANDS)) {
		if(aType == UserCommand::TYPE_REMOVE) {
			int cmd = FavoriteManager::getInstance()->findUserCommand(name, client->getHubUrl());
			if(cmd != -1)
				FavoriteManager::getInstance()->removeUserCommand(cmd);
		} else if(aType == UserCommand::TYPE_CLEAR) {
 			FavoriteManager::getInstance()->removeHubUserCommands(ctx, client->getHubUrl());
 		} else {
			FavoriteManager::getInstance()->addUserCommand(aType, ctx, UserCommand::FLAG_NOSAVE, name, command, "", client->getHubUrl());
		}
	}
}

void ClientManager::setIPUser(const UserPtr& user, const string& IP, const string& udpPort /*emptyString*/) {
	if(IP.empty())
		return;
			
	RLock l(cs);
	OnlinePairC p = onlineUsers.equal_range(const_cast<CID*>(&user->getCID()));
	for (auto i = p.first; i != p.second; i++) {
		i->second->getIdentity().setIp4(IP);
		if(!udpPort.empty())
			i->second->getIdentity().setUdp4Port(udpPort);
	}
}

} // namespace dcpp
