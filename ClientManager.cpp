/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#include <openssl/aes.h>
#include <openssl/rand.h>

namespace dcpp {

using boost::find_if;

ClientManager::ClientManager() : udp(Socket::TYPE_UDP), lastOfflineUserCleanup(GET_TICK()) {
	TimerManager::getInstance()->addListener(this);
}

ClientManager::~ClientManager() {
	TimerManager::getInstance()->removeListener(this);
}

Client* ClientManager::createClient(const RecentHubEntryPtr& aEntry, ProfileToken aProfile) noexcept {
	auto url = aEntry->getServer();

	Client* c;
	if (AirUtil::isAdcHub(url)) {
		c = new AdcHub(url);
	} else {
		c = new NmdcHub(url);
	}

	c->setShareProfile(aProfile);
	bool added = true;

	{
		WLock l(cs);
		auto ret = clients.emplace(const_cast<string*>(&c->getHubUrl()), c);
		if (!ret.second) {
			added = false;
			ret.first->second->setActive();
		}
	}

	if (!added) {
		c->shutdown();
		return nullptr;
	}

	c->addListener(this);

	FavoriteManager::getInstance()->addRecent(aEntry);
	fire(ClientManagerListener::ClientCreated(), c);
	return c;
}

Client* ClientManager::getClient(const string& aHubURL) noexcept {
	RLock l (cs);
	auto p = clients.find(const_cast<string*>(&aHubURL));
	return p != clients.end() ? p->second : nullptr;
}

void ClientManager::putClient(Client* aClient) noexcept {
	fire(ClientManagerListener::ClientDisconnected(), aClient->getHubUrl());
	aClient->removeListeners();

	{
		WLock l(cs);
		clients.erase(const_cast<string*>(&aClient->getHubUrl()));
	}
	aClient->shutdown();
}

void ClientManager::setClientUrl(const string& aOldUrl, const string& aNewUrl) noexcept {
	WLock l (cs);
	auto p = clients.find(const_cast<string*>(&aOldUrl));
	if (p != clients.end()) {
		auto c = p->second;
		clients.erase(p);
		c->setHubUrl(aNewUrl);
		clients.emplace(const_cast<string*>(&c->getHubUrl()), c);
	}
}

StringList ClientManager::getHubUrls(const CID& cid) const noexcept {
	StringList lst;

	RLock l(cs);
	OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&cid));
	for(auto i = op.first; i != op.second; ++i) {
		lst.push_back(i->second->getClientBase().getHubUrl());
	}
	return lst;
}

OrderedStringSet ClientManager::getHubSet(const CID& cid) const noexcept {
	OrderedStringSet lst;

	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&cid));
	for(auto i = op.first; i != op.second; ++i) {
		lst.insert(i->second->getClientBase().getHubUrl());
	}
	return lst;
}

StringList ClientManager::getHubNames(const CID& cid) const noexcept {
	StringList lst;

	RLock l(cs);
	OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&cid));
	for(auto i = op.first; i != op.second; ++i) {
		lst.push_back(i->second->getClientBase().getHubName());		
	}

	sort(lst.begin(), lst.end());
	return lst;
}

StringPairList ClientManager::getHubs(const CID& cid) const noexcept {
	RLock l(cs);
	StringPairList lst;
	auto op = onlineUsers.equal_range(const_cast<CID*>(&cid));
	for(auto i = op.first; i != op.second; ++i) {
		lst.emplace_back(i->second->getClient().getHubUrl(), i->second->getClient().getHubName());
	}
	return lst;
}

string ClientManager::getHubName(const string& aHubUrl) const noexcept{
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&aHubUrl));
	if (i != clients.end()) {
		return i->second->getHubName();
	}

	return Util::emptyString;
}

StringList ClientManager::getNicks(const CID& cid, bool allowCID/*true*/) const noexcept {
	set<string> ret;

	{
		RLock l(cs);
		OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&cid));
		for(auto i = op.first; i != op.second; ++i) {
			ret.insert(i->second->getIdentity().getNick());
		}

		if(ret.empty()) {
			// offline
			auto i = offlineUsers.find(const_cast<CID*>(&cid));
			if(i != offlineUsers.end()) {
				ret.insert(i->second.getNick());
			} else if(allowCID) {
				ret.insert('{' + cid.toBase32() + '}');
			}
		}
	}

	return StringList(ret.begin(), ret.end());
}

map<string, Identity> ClientManager::getIdentities(const UserPtr &u) const noexcept {
	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&u->getCID()));
	auto ret = map<string, Identity>();
	for(auto i = op.first; i != op.second; ++i) {
		ret.insert(make_pair(i->second->getHubUrl(), i->second->getIdentity()));
	}

	return ret;
}

string ClientManager::getNick(const UserPtr& u, const string& hint, bool allowFallback /*true*/) const noexcept {
	RLock l(cs);
	OnlinePairC p;
	auto ou = findOnlineUserHint(u->getCID(), hint, p);
	if(ou)
		return ou->getIdentity().getNick();

	if(allowFallback) {
		if (p.first != p.second){
			return p.first->second->getIdentity().getNick();
		} else {
			// offline
			auto i = offlineUsers.find(const_cast<CID*>(&u->getCID()));
			if(i != offlineUsers.end()) {
				return i->second.getNick();
			}
		}
	}

	return Util::emptyString;

}

OnlineUserPtr ClientManager::getUsers(const HintedUser& user, OnlineUserList& ouList) const noexcept {
	OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&user.user->getCID()));
	for(auto i = op.first; i != op.second; ++i) {
		ouList.push_back(i->second);
	}

	sort(ouList.begin(), ouList.end(), OnlineUser::NickSort());

	auto p = find_if(ouList, OnlineUser::UrlCompare(user.hint));
	if (p != ouList.end()) {
		auto hinted = *p;
		ouList.erase(p);
		return hinted;
	}

	return nullptr;
}

string ClientManager::getFormatedNicks(const HintedUser& user) const noexcept {
	auto ret = formatUserList<OnlineUser::Nick>(user, true);
	if (ret.empty()) {
		// offline
		RLock l(cs);
		auto i = offlineUsers.find(const_cast<CID*>(&user.user->getCID()));
		//dcassert(i != offlineUsers.end());
		if(i != offlineUsers.end()) {
			return i->second.getNick();
		}
	}
	return ret;
}

string ClientManager::getFormatedHubNames(const HintedUser& user) const noexcept {
	auto ret = formatUserList<OnlineUser::HubName>(user, false);
	return ret.empty() ? STRING(OFFLINE) : ret;
}

optional<OfflineUser> ClientManager::getOfflineUser(const CID& cid) {
	RLock l(cs);
	auto i = offlineUsers.find(const_cast<CID*>(&cid));
	if (i != offlineUsers.end()) {
		return i->second;
	}
	return boost::none;
}

string ClientManager::getField(const CID& cid, const string& hint, const char* field) const noexcept {
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

string ClientManager::getDLSpeed(const CID& cid) const noexcept {
	RLock l(cs);
	OnlineIterC i = onlineUsers.find(const_cast<CID*>(&cid));
	if(i != onlineUsers.end()) {
		return Util::formatBytes(i->second->getIdentity().get("DS")) + "/s";
	}
	return STRING(OFFLINE);
}

uint8_t ClientManager::getSlots(const CID& cid) const noexcept {
	RLock l(cs);
	OnlineIterC i = onlineUsers.find(const_cast<CID*>(&cid));
	if(i != onlineUsers.end()) {
		return static_cast<uint8_t>(Util::toInt(i->second->getIdentity().get("SL")));
	}
	return 0;
}

bool ClientManager::hasClient(const string& aUrl) const noexcept{
	RLock l(cs);

	auto i = clients.find(const_cast<string*>(&aUrl));
	return i != clients.end();
}

string ClientManager::findHub(const string& ipPort, bool nmdc) const noexcept {
	string ip;
	string port = "411";
	Util::parseIpPort(ipPort, ip, port);

	string url;

	RLock l(cs);
	for(const auto c: clients | map_values) {
		if(c->getIp() == ip && AirUtil::isAdcHub(c->getHubUrl()) == !nmdc) {
			// If exact match is found, return it
			if(c->getPort() == port)
				return c->getHubUrl();

			// Port is not always correct, so use this as a best guess...
			url = c->getHubUrl();
		}
	}

	return url;
}

const string& ClientManager::findHubEncoding(const string& aUrl) const noexcept {
	RLock l(cs);

	auto i = clients.find(const_cast<string*>(&aUrl));
	if(i != clients.end()) {
		return i->second->get(HubSettings::NmdcEncoding);
	}
	return SETTING(NMDC_ENCODING);
}

HintedUser ClientManager::findLegacyUser(const string& nick) const noexcept {
	if(nick.empty())
		return HintedUser();

	RLock l(cs);

	for(auto i: clients | map_values) {
		if (!AirUtil::isAdcHub(i->getHubUrl())) {
			auto nmdc = static_cast<NmdcHub*>(i);
			//if(nmdc) {
			/** @todo run the search directly on non-UTF-8 nicks when we store them. */
			auto ou = nmdc->findUser(nmdc->toUtf8(nick));
			if(ou) {
				return HintedUser(*ou);
			}
			//}
		}
	}

	return HintedUser();
}

UserPtr ClientManager::getUser(const string& aNick, const string& aHubUrl) noexcept {
	CID cid = makeCid(aNick, aHubUrl);
	{
		RLock l(cs);
		auto ui = users.find(const_cast<CID*>(&cid));
		if(ui != users.end()) {
			dcassert(ui->second->getCID() == cid);
			ui->second->setFlag(User::NMDC);
			return ui->second;
		}
	}

	if(cid == getMe()->getCID()) {
		return getMe();
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
			dcassert(ui->second->getCID() == cid);
			return ui->second;
		}
	}

	if (cid == getMe()->getCID()) {
		return getMe();
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
bool ClientManager::isOp(const UserPtr& user, const string& aHubUrl) const noexcept {
	RLock l(cs);
	OnlinePairC p = onlineUsers.equal_range(const_cast<CID*>(&user->getCID()));
	for(auto i = p.first; i != p.second; ++i) {
		if(i->second->getClient().getHubUrl() == aHubUrl) {
			return i->second->getIdentity().isOp();
		}
	}
	return false;
}

bool ClientManager::isStealth(const string& aHubUrl) const noexcept {
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
		{
			WLock l(cs);
			//user came online, remove him from offlineUsers list.
			updateUser(*ou, false);
		}
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
				
				/*
				User went offline, cache his information in offlineUsers map.
				This needs to be done inside the same WLock that removes the onlineUser, 
				so we ensure that we should allways find the user in atleast one of the lists.
				*/
				if (diff == 1) 
					updateUser(*ou, true);
				onlineUsers.erase(i);
				break;
			}
		}
	}

	if(diff == 1) { //last user
		UserPtr& u = ou->getUser();
		u->unsetFlag(User::ONLINE);
		//updateUser(*ou);
		if(disconnect)
			ConnectionManager::getInstance()->disconnect(u);
		fire(ClientManagerListener::UserDisconnected(), u, true);
	} else if(diff > 1) {
		fire(ClientManagerListener::UserDisconnected(), *ou, false);
	}
}

void ClientManager::listProfiles(const UserPtr& aUser, ProfileTokenSet& profiles) const noexcept {
	RLock l(cs);
	OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));
	for(auto i = op.first; i != op.second; ++i) {
		profiles.insert(i->second->getClient().getShareProfile());
	}
}

optional<ProfileToken> ClientManager::findProfile(UserConnection& p, const string& userSID) const noexcept {
	if(!userSID.empty()) {
		RLock l(cs);
		auto op = onlineUsers.equal_range(const_cast<CID*>(&p.getUser()->getCID())) | map_values;
		for(const auto& ou: op) {
			if(compare(ou->getIdentity().getSIDString(), userSID) == 0) {
				p.setHubUrl(ou->getClient().getHubUrl());
				return ou->getClient().getShareProfile();
			}
		}

		//don't accept invalid SIDs
		return optional<ProfileToken>();
	}

	//no SID specified, find with hint.
	OnlinePairC op;

	RLock l(cs);
	auto ou = findOnlineUserHint(p.getUser()->getCID(), p.getHubUrl(), op);
	if(ou) {
		return ou->getClient().getShareProfile();
	} else if(op.first != op.second) {
		//pick a random profile
		return op.first->second->getClient().getShareProfile();
	}

	return boost::none;
}

bool ClientManager::isActive() const noexcept {
	if (CONNSETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_PASSIVE && CONNSETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_DISABLED)
		return true;

	if (CONNSETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_PASSIVE && CONNSETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_DISABLED)
		return true;

	return FavoriteManager::getInstance()->hasActiveHubs();
}

bool ClientManager::isActive(const string& aHubUrl) const noexcept {
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&aHubUrl));
	if(i != clients.end() && i->second->isConnected()) {
		return i->second->isActive();		
	}
	return false;
}

string ClientManager::findMySID(const UserPtr& aUser, string& aHubUrl, bool allowFallback) const noexcept {
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

OnlineUser* ClientManager::findOnlineUserHint(const CID& cid, const string& hintUrl, OnlinePairC& p) const noexcept {
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

pair<int64_t, int> ClientManager::getShareInfo(const HintedUser& user) const noexcept {
	RLock l (cs);
	auto ou = findOnlineUser(user);
	if (ou) {
		return { Util::toInt64(ou->getIdentity().getShareSize()), Util::toInt(ou->getIdentity().getSharedFiles()) };
	}

	return { 0, 0 };
}

void ClientManager::getUserInfoList(const UserPtr& user, User::UserInfoList& aList_) const noexcept {
	RLock l(cs);
	auto p = onlineUsers.equal_range(const_cast<CID*>(&user->getCID()));

	for(auto i = p.first; i != p.second; ++i) {
		auto ou = i->second;
		aList_.emplace_back(ou->getHubUrl(), ou->getClient().getHubName(), Util::toInt64(ou->getIdentity().getShareSize()));
	}
}

bool ClientManager::getSupportsCCPM(const UserPtr& aUser, string& _error) {
	if (!aUser->isOnline()) {
		_error = STRING(USER_OFFLINE);
		return false;
	}
	else if (aUser->isNMDC()) {
		_error = STRING(CCPM_NOT_SUPPORTED_NMDC);
		return false;
	}
	else if (aUser->isSet(User::BOT)) {
		_error = STRING(CCPM_NOT_SUPPORTED);
		return false;
	}


	RLock l(cs);
	OnlinePair op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));
	for (auto u : op | map_values) {
		if (u->supportsCCPM(_error))
			return true;
	}
	return false;
}


OnlineUser* ClientManager::findOnlineUser(const HintedUser& user) const noexcept {
	return findOnlineUser(user.user->getCID(), user.hint);
}

OnlineUser* ClientManager::findOnlineUser(const CID& cid, const string& hintUrl) const noexcept {
	OnlinePairC p;
	OnlineUser* u = findOnlineUserHint(cid, hintUrl, p);
	if(u) // found an exact match (CID + hint).
		return u;

	if(p.first == p.second) // no user found with the given CID.
		return 0;

	// return a random user
	return p.first->second;
}

bool ClientManager::connect(const UserPtr& aUser, const string& aToken, bool allowUrlChange, string& lastError_, string& hubHint_, bool& isProtocolError, ConnectionType aConnType) noexcept {
	RLock l(cs);
	OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));

	auto connectUser = [&] (OnlineUser* ou) -> bool {
		isProtocolError = false;
		if (aConnType == CONNECTION_TYPE_PM) {
			if (!ou->supportsCCPM(lastError_)) {
				isProtocolError = true;
				return false;
			}
		}

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
		} else if (ret == AdcCommand::ERROR_FEATURE_MISSING) {
			isProtocolError = true;
			lastError_ = STRING(NO_NATT_SUPPORT);
		} else if (ret == AdcCommand::ERROR_PROTOCOL_GENERIC) {
			isProtocolError = true;
			lastError_ = STRING(UNABLE_CONNECT_USER);
		}

		return false;
	};

	//prefer the hinted hub
	auto p = find_if(op, [&hubHint_](const pair<CID*, OnlineUser*>& ouc) { return ouc.second->getHubUrl() == hubHint_; });
	if (p != op.second && connectUser(p->second)) {
		return true;
	}

	if (!allowUrlChange) {
		return false;
	}

	//connect via any available hub
	for(auto i = op.first; i != op.second; ++i) {
		if (connectUser(i->second)) {
			hubHint_ = i->second->getHubUrl();
			return true;
		}
	}

	return false;
}

bool ClientManager::privateMessage(const HintedUser& user, const string& msg, string& error_, bool thirdPerson) noexcept {
	RLock l(cs);
	OnlineUser* u = findOnlineUser(user);
	
	if(u) {
		return u->getClientBase().privateMessage(u, msg, error_, thirdPerson);
	}
	error_ = STRING(USER_OFFLINE);
	return false;
}

void ClientManager::userCommand(const HintedUser& user, const UserCommand& uc, ParamMap& params, bool compatibility) noexcept {
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

bool ClientManager::sendUDP(AdcCommand& cmd, const CID& cid, bool noCID /*false*/, bool noPassive /*false*/, const string& aKey /*Util::emptyString*/, const string& aHubUrl /*Util::emptyString*/) noexcept {
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

void ClientManager::infoUpdated() noexcept {
	RLock l(cs);
	for(auto c: clients | map_values) {
		if(c->isConnected()) {
			c->info();
		}
	}
}

void ClientManager::resetProfile(ProfileToken oldProfile, ProfileToken newProfile, bool nmdcOnly) noexcept {
	RLock l(cs);
	for (auto c : clients | map_values) {
		if (c->getShareProfile() == oldProfile && (!nmdcOnly || !AirUtil::isAdcHub(c->getHubUrl()))) {
			c->setShareProfile(newProfile);
			c->info();
		}
	}
}

void ClientManager::resetProfiles(const ShareProfileInfo::List& aProfiles, ProfileToken aDefaultProfile) noexcept {
	RLock l(cs);
	for(auto sp: aProfiles) {
		for(auto c: clients | map_values) {
			if (c->getShareProfile() == sp->token) {
				c->setShareProfile(aDefaultProfile);
				c->info();
			}
		}
	}
}

bool ClientManager::hasAdcHubs() const noexcept {
	RLock l(cs);
	return find_if(clients | map_values, [](const Client* c) { return AirUtil::isAdcHub(c->getHubUrl()); }).base() != clients.end();
}

pair<size_t, size_t> ClientManager::countAschSupport(const OrderedStringSet& hubs) const noexcept {
	size_t found = 0;
	size_t total = 0;

	RLock l(cs);
	for (const auto& u : onlineUsers | map_values) {
		if (!u->getUser()->isSet(User::BOT) && hubs.find(u->getHubUrl()) != hubs.end()) {
			total++;
			if (u->getUser()->isSet(User::ASCH))
				found++;
		}
	}

	return { found, total };
}

void ClientManager::on(NmdcSearch, Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize, 
									int aFileType, const string& aString, bool isPassive) noexcept
{
	fire(ClientManagerListener::IncomingSearch(), aString);

	bool hideShare = aClient->getShareProfile() == SP_HIDDEN;

	SearchResultList l;
	ShareManager::getInstance()->nmdcSearch(l, aString, aSearchType, aSize, aFileType, isPassive ? 5 : 10, hideShare);
	if(l.size() > 0) {
		if(isPassive) {
			string name = aSeeker.substr(4);
			// Good, we have a passive seeker, those are easier...
			string str;
			for(const auto& sr: l) {
				str += sr->toSR(*aClient);
				str[str.length()-1] = 5;
				str += Text::fromUtf8(name, aClient->get(HubSettings::NmdcEncoding));
				str += '|';
			}
			
			if(str.size() > 0)
				aClient->send(str);
			
		} else {
			try {
				string ip, port;

				Util::parseIpPort(aSeeker, ip, port);
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
		
		string ip, port;
		Util::parseIpPort(aSeeker, ip, port);

		if (port.empty())
			return;
		
		try {
			AdcCommand cmd = SearchManager::getInstance()->toPSR(true, aClient->getMyNick(), aClient->getIpPort(), aTTH.toBase32(), partialInfo);
			udp.writeTo(Socket::resolve(ip), port, cmd.toString(getMe()->getCID()));
		} catch(...) {
			dcdebug("Partial search caught error\n");		
		}
	}
}
void ClientManager::onSearch(const Client* c, const AdcCommand& adc, OnlineUser& from) noexcept {
	// Filter own searches
	fire(ClientManagerListener::IncomingADCSearch(), adc);
	if(from.getUser() == me)
		return;

	bool isUdpActive = from.getIdentity().isUdpActive();
	if (isUdpActive) {
		//check that we have a common IP protocol available (we don't want to send responses via wrong hubs)
		const auto& myIdentity = c->getMyIdentity();
		if (myIdentity.getIp4().empty() || !from.getIdentity().isUdp4Active()) {
			if (myIdentity.getIp6().empty() || !from.getIdentity().isUdp6Active()) {
				return;
			}
		}
	}

	SearchManager::getInstance()->respond(adc, from, isUdpActive, c->getIpPort(), c->getShareProfile());
}

uint64_t ClientManager::search(string& who, SearchPtr aSearch) noexcept {
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&who));
	if(i != clients.end() && i->second->isConnected()) {
		return i->second->queueSearch(move(aSearch));		
	}
	return 0;
}

void ClientManager::directSearch(const HintedUser& user, int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, 
	const StringList& aExtList, const string& aDir, time_t aDate, int aDateMode) noexcept {

	RLock l (cs);
	auto ou = findOnlineUser(user);
	if (ou) {
		ou->getClientBase().directSearch(*ou, aSizeMode, aSize, aFileType, aString, aToken, aExtList, aDir, aDate, aDateMode);
	}
}

void ClientManager::getOnlineClients(StringList& onlineClients) const noexcept {
	RLock l (cs);
	for (auto c: clients | map_values) {
		if (c->isConnected())
			onlineClients.push_back(c->getHubUrl());
	}
}

void ClientManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	
	//store offline users information for approx 10minutes, no need to be accurate.
	if(aTick > (lastOfflineUserCleanup + 10*60*1000)) { 
		WLock l(cs);

		// Collect some garbage...
		auto i = users.begin();
		while(i != users.end()) {
			dcassert(i->second->getCID() == *i->first);
			if(i->second->unique()) {
				auto n = offlineUsers.find(const_cast<CID*>(&i->second->getCID()));
				if(n != offlineUsers.end()) 
					offlineUsers.erase(n);
				users.erase(i++);
			} else {
				++i;
			}
		}
		lastOfflineUserCleanup = aTick;
	}

	RLock l (cs);
	for(auto c: clients | map_values)
		c->info();
}

string ClientManager::getClientStats() const noexcept {
	RLock l(cs);
	map<CID, OnlineUser*> uniqueUserMap;
	for(const auto& ou: onlineUsers | map_values) {
		uniqueUserMap.insert(make_pair(ou->getUser()->getCID(), ou));
	}

	int allUsers = onlineUsers.size();
	int uniqueUsers = uniqueUserMap.size();
	if (uniqueUsers == 0) {
		return "No users";
	}

	int64_t totalShare = 0;
	int64_t uploadSpeed = 0;
	int64_t downloadSpeed = 0;
	int64_t nmdcConnection = 0;
	int nmdcUsers = 0, adcUsers = 0, adcHasDownload = 0, adcHasUpload = 0, nmdcHasConnection = 0;
	int hiddenUsers = 0, bots = 0, activeUsers = 0, operators = 0;
	for(const auto& ou: uniqueUserMap | map_values) {
		totalShare += Util::toInt64(ou->getIdentity().getShareSize());
		if (ou->isHidden()) {
			hiddenUsers++;
			continue;
		}

		if (ou->getIdentity().isBot()) {
			bots++;
			if (!ou->getUser()->isNMDC()) {
				continue;
			}
		}

		if (ou->getIdentity().isOp()) {
			operators++;
		}

		if (ou->getIdentity().isTcpActive()) {
			activeUsers++;
		}

		if (ou->getUser()->isNMDC()) {
			auto speed = Util::toDouble(ou->getIdentity().getNmdcConnection());
			if (speed > 0) {
				nmdcConnection += (speed * 1000.0 * 1000.0) / 8.0;
				nmdcHasConnection++;
			}
			nmdcUsers++;
		} else {
			auto up = ou->getIdentity().getAdcConnectionSpeed(false);
			if (up > 0) {
				uploadSpeed += up;
				adcHasUpload++;
			}

			auto down = ou->getIdentity().getAdcConnectionSpeed(true);
			if (down > 0) {
				downloadSpeed += ou->getIdentity().getAdcConnectionSpeed(true);
				adcHasDownload++;
			}
			adcUsers++;
		}
	}

	string lb = "\n";
	string ret;
	ret += lb;
	ret += lb;
	ret += "All users: " + Util::toString(allUsers) + lb;
	ret += "Unique users: " + Util::toString(uniqueUsers) + " (" + Util::toString(((double)uniqueUsers/(double)allUsers)*100.00) + "%)" + lb;
	ret += "Active/operators/bots/hidden: " + Util::toString(activeUsers) + " (" + Util::toString(((double) activeUsers / (double) uniqueUsers)*100.00) + "%) / " +
		Util::toString(operators) + " (" + Util::toString(((double) operators / (double) uniqueUsers)*100.00) + "%) / " +
		Util::toString(bots) + " (" + Util::toString(((double) bots / (double) uniqueUsers)*100.00) + "%) / " +
		Util::toString(hiddenUsers) + " (" + Util::toString(((double) hiddenUsers / (double) uniqueUsers)*100.00) + "%)" + lb;
	ret += "Protocol users (ADC/NMDC): " + Util::toString(adcUsers) + "/" + Util::toString(nmdcUsers) + lb;
	ret += "Total share: " + Util::formatBytes(totalShare) + " (" + Util::formatBytes((double)totalShare / (double)uniqueUsers) + " per user)" + lb;
	ret += "Average ADC connection speed: " + Util::formatConnectionSpeed((double) downloadSpeed / (double) adcUsers) + " down, " + Util::formatConnectionSpeed((double) uploadSpeed / (double) adcUsers) + " up" + lb;
	ret += "Average NMDC connection speed: " + Util::formatConnectionSpeed((double) nmdcConnection / (double) nmdcUsers) + lb;
	ret += lb;
	ret += lb;
	ret += "Clients (from unique users)";
	ret += lb;

	map<string, double> clientNames;
	for(const auto& ou: uniqueUserMap | map_values) {
		auto app = ou->getIdentity().getApplication();
		auto pos = app.find(" ");

		if (pos != string::npos) {
			clientNames[app.substr(0, pos)]++;
		} else {
			clientNames["Unknown"]++;
		}
	}

	auto countCompare = [] (const pair<string, int>& i, const pair<string, int>& j) -> bool {
		return (i.second > j.second);
	};

	vector<pair<string, int> > print(clientNames.begin(), clientNames.end());
	sort(print.begin(), print.end(), countCompare);
	for(auto& p: print) {
		ret += p.first + ":\t\t" + Util::toString(p.second) + " (" + Util::toString(((double)p.second/(double)uniqueUsers)*100.00) + "%)" + lb;
	}

	return ret;
}

UserPtr& ClientManager::getMe() noexcept {
	if(!me) {
		me = new User(getMyCID());

		WLock l(cs);
		users.emplace(const_cast<CID*>(&me->getCID()), me);
	}
	return me;
}

const CID& ClientManager::getMyPID() noexcept {
	if(!pid)
		pid = CID(SETTING(PRIVATE_ID));
	return pid;
}

CID ClientManager::getMyCID() noexcept {
	TigerHash tiger;
	tiger.update(getMyPID().data(), CID::SIZE);
	return CID(tiger.finalize());
}

void ClientManager::updateUser(const OnlineUser& user, bool wentOffline) noexcept {
	if (wentOffline)
		addOfflineUser(user.getUser(), user.getIdentity().getNick(), user.getHubUrl());
	else {
		//user came online
		auto i = offlineUsers.find(const_cast<CID*>(&user.getUser()->getCID()));
		if (i != offlineUsers.end())
			offlineUsers.erase(i);
	}
}

void ClientManager::addOfflineUser(const UserPtr& user, const string& nick, const string& url, uint32_t lastSeen/*GET_TIME()*/) noexcept{
	offlineUsers.emplace(const_cast<CID*>(&user->getCID()), OfflineUser(nick, url, lastSeen));
}

string ClientManager::getMyNick(const string& hubUrl) const noexcept {
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&hubUrl));
	if(i != clients.end()) {
		return i->second->getMyIdentity().getNick();
	}
	return Util::emptyString;
}

void ClientManager::cancelSearch(void* aOwner) noexcept {
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
		//updateUser(**i);
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

void ClientManager::setIPUser(const UserPtr& user, const string& IP, const string& udpPort /*emptyString*/) noexcept {
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

bool ClientManager::connectADCSearchResult(const CID& aCID, string& token_, string& hubUrl_, string& connection_, uint8_t& slots_) const noexcept {
	RLock l(cs);

	// token format: [per-hub unique id] "/" [per-search actual token] (see AdcHub::search)
	auto slash = token_.find('/');
	if(slash == string::npos) { return false; }

	auto uniqueId = Util::toUInt32(token_.substr(0, slash));
	auto client = find_if(clients | map_values, [uniqueId](const Client* client) { return client->getUniqueId() == uniqueId; });
	if(client.base() == clients.end()) { return false; }
	hubUrl_ = (*client)->getHubUrl();

	token_.erase(0, slash + 1);


	// get the connection and total slots
	OnlinePairC p;
	auto ou = findOnlineUserHint(aCID, hubUrl_, p);
	if (ou) {
		slots_ = ou->getIdentity().getSlots();
		connection_ = ou->getIdentity().getConnectionString();
		return true;
	} else {
		// some hubs may hide this information...
		for (auto i = p.first; i != p.second; i++) {
			if (slots_ == 0)
				slots_ = i->second->getIdentity().getSlots();

			const auto& conn = i->second->getIdentity().getConnectionString();
			if (!conn.empty()) {
				connection_ = conn;
				break;
			}
		}
	}

	return true;
}

bool ClientManager::connectNMDCSearchResult(const string& userIP, const string& hubIpPort, HintedUser& user, string& nick, string& connection_, string& file, string& hubName) noexcept {
	//RLock l(cs);
	user.hint = findHub(hubIpPort, true);
	if(user.hint.empty()) {
		// Could happen if hub has multiple URLs / IPs
		user = findLegacyUser(nick);
		if(!user.user)
			return false;
	}

	string encoding = findHubEncoding(user.hint);
	nick = Text::toUtf8(nick, encoding);
	file = Text::toUtf8(file, encoding);
	hubName = Text::toUtf8(hubName, encoding);

	if(!user.user) {
		user.user = findUser(nick, user.hint);
		if(!user.user)
			return false;
	}

	setIPUser(user, userIP);

	RLock l(cs);
	auto ou = findOnlineUser(user);
	if (ou)
		connection_ = ou->getIdentity().getConnectionString();

	return true;
}

} // namespace dcpp
