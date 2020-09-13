/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#include "AirUtil.h"
#include "ConnectivityManager.h"
#include "ConnectionManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "FavoriteManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "RelevanceSearch.h"
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

#include <boost/range/algorithm/copy.hpp>
#include <boost/algorithm/string/trim.hpp>

namespace dcpp {

using boost::find_if;

ClientManager::ClientManager() : udp(make_unique<Socket>(Socket::TYPE_UDP)), lastOfflineUserCleanup(GET_TICK()) {
	TimerManager::getInstance()->addListener(this);
}

ClientManager::~ClientManager() {
	TimerManager::getInstance()->removeListener(this);
}

ClientPtr ClientManager::makeClient(const string& aHubURL, const ClientPtr& aOldClient) noexcept {
	if (AirUtil::isAdcHub(aHubURL)) {
		return std::make_shared<AdcHub>(aHubURL, aOldClient);
	}

	return std::make_shared<NmdcHub>(aHubURL, aOldClient);
}

ClientPtr ClientManager::createClient(const string& aUrl) noexcept {

	auto c = ClientManager::makeClient(boost::trim_copy(aUrl));
	bool added = true;

	{
		WLock l(cs);
		auto ret = clients.emplace(const_cast<string*>(&c->getHubUrl()), c);
		if (!ret.second) {
			added = false;
			ret.first->second->setActive();
		}

		clientsById.emplace(c->getToken(), c);
	}

	if (!added) {
		c->shutdown(c, false);
		return nullptr;
	}

	c->addListener(this);

	fire(ClientManagerListener::ClientCreated(), c);
	return c;
}

ClientPtr ClientManager::getClient(const string& aHubURL) noexcept {
	RLock l (cs);
	auto p = clients.find(const_cast<string*>(&aHubURL));
	return p != clients.end() ? p->second : nullptr;
}

ClientPtr ClientManager::getClient(ClientToken aClientId) noexcept {
	RLock l(cs);
	auto p = clientsById.find(aClientId);
	return p != clientsById.end() ? p->second : nullptr;
}

void ClientManager::putClients() noexcept {
	vector<ClientToken> tokens;

	{
		RLock l(cs);
		boost::copy(clientsById | map_keys, back_inserter(tokens));
	}

	for (const auto& token : tokens) {
		putClient(token);
	}
}

bool ClientManager::putClient(ClientToken aClientId) noexcept {
	auto c = getClient(aClientId);
	if (c) {
		putClient(c);
		return true;
	}

	return false;
}

bool ClientManager::putClient(const string& aHubURL) noexcept {
	auto c = getClient(aHubURL);
	if (c) {
		putClient(c);
		return true;
	}

	return false;
}

bool ClientManager::putClient(ClientPtr& aClient) noexcept {
	dcassert(aClient->hasListener(this));

	fire(ClientManagerListener::ClientDisconnected(), aClient->getHubUrl());
	fire(ClientManagerListener::ClientRemoved(), aClient);

	aClient->disconnect(true);
	aClient->shutdown(aClient, false);
	aClient->removeListener(this);

	{
		WLock l(cs);
		clients.erase(const_cast<string*>(&aClient->getHubUrl()));
		clientsById.erase(aClient->getToken());
	}

	return true;
}

ClientPtr ClientManager::redirect(const string& aHubUrl, const string& aNewUrl) noexcept {
	auto oldClient = getClient(aHubUrl);
	if (!oldClient) {
		return nullptr;
	}

	oldClient->disconnect(true);
	oldClient->shutdown(oldClient, true);
	oldClient->removeListener(this);

	auto newClient = ClientManager::makeClient(aNewUrl, oldClient);
	oldClient->clearCache();

	{
		WLock l(cs);
		clients.erase(const_cast<string*>(&aHubUrl));
		clients.emplace(const_cast<string*>(&newClient->getHubUrl()), newClient);
		clientsById[newClient->getToken()] = newClient;
	}

	newClient->addListener(this);

	fire(ClientManagerListener::ClientRedirected(), oldClient, newClient);
	return newClient;
}

StringList ClientManager::getHubUrls(const CID& aCID) const noexcept {
	StringList lst;

	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aCID));
	for (auto i = op.first; i != op.second; ++i) {
		lst.push_back(i->second->getClient()->getHubUrl());
	}
	return lst;
}

OrderedStringSet ClientManager::getHubSet(const CID& aCID) const noexcept {
	OrderedStringSet lst;

	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aCID));
	for (auto i = op.first; i != op.second; ++i) {
		lst.insert(i->second->getClient()->getHubUrl());
	}
	return lst;
}

StringList ClientManager::getHubNames(const CID& aCID) const noexcept {
	StringList lst;

	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aCID));
	for (auto i = op.first; i != op.second; ++i) {
		lst.push_back(i->second->getClient()->getHubName());
	}

	sort(lst.begin(), lst.end());
	return lst;
}

StringPairList ClientManager::getHubs(const CID& aCID) const noexcept {
	RLock l(cs);
	StringPairList urlNameList;
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aCID));
	for (auto i = op.first; i != op.second; ++i) {
		urlNameList.emplace_back(i->second->getClient()->getHubUrl(), i->second->getClient()->getHubName());
	}
	return urlNameList;
}

string ClientManager::getHubName(const string& aHubUrl) const noexcept{
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&aHubUrl));
	if (i != clients.end()) {
		return i->second->getHubName();
	}

	return Util::emptyString;
}

StringList ClientManager::getNicks(const HintedUser& aUser) const noexcept {
	return getNicks(aUser.user->getCID());
}

StringList ClientManager::getHubNames(const HintedUser& aUser) const noexcept {
	return getHubNames(aUser.user->getCID());
}

StringList ClientManager::getHubUrls(const HintedUser& aUser) const noexcept {
	return getHubUrls(aUser.user->getCID());
}

StringList ClientManager::getNicks(const CID& aCID, bool aAllowCID /*true*/) const noexcept {
	set<string> ret;

	{
		RLock l(cs);
		auto op = onlineUsers.equal_range(const_cast<CID*>(&aCID));
		for (auto i = op.first; i != op.second; ++i) {
			ret.insert(i->second->getIdentity().getNick());
		}

		if(ret.empty()) {
			// offline
			auto i = offlineUsers.find(const_cast<CID*>(&aCID));
			if (i != offlineUsers.end()) {
				ret.insert(i->second.getNick());
			} else if (aAllowCID) {
				ret.insert('{' + aCID.toBase32() + '}');
			}
		}
	}

	return StringList(ret.begin(), ret.end());
}

map<string, Identity> ClientManager::getIdentities(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));
	auto ret = map<string, Identity>();
	for (auto i = op.first; i != op.second; ++i) {
		ret.emplace(i->second->getHubUrl(), i->second->getIdentity());
	}

	return ret;
}

string ClientManager::getNick(const UserPtr& aUser, const string& aHubUrl, bool aAllowFallback /*true*/) const noexcept {
	{
		RLock l(cs);
		OnlinePairC p;
		auto ou = findOnlineUserHintUnsafe(aUser->getCID(), aHubUrl, p);
		if (ou) {
			return ou->getIdentity().getNick();
		}

		if (aAllowFallback) {
			if (p.first != p.second) {
				return p.first->second->getIdentity().getNick();
			} else {
				// offline
				auto i = offlineUsers.find(const_cast<CID*>(&aUser->getCID()));
				if (i != offlineUsers.end()) {
					return i->second.getNick();
				}
			}
		}
	}

	//dcassert(0);

	//Should try to avoid this case at all times by saving users nicks and loading them...
	return aUser->getCID().toBase32();

}

OnlineUserPtr ClientManager::getOnlineUsers(const HintedUser& aUser, OnlineUserList& ouList_) const noexcept {
	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aUser.user->getCID()));
	for(auto i = op.first; i != op.second; ++i) {
		ouList_.push_back(i->second);
	}

	sort(ouList_.begin(), ouList_.end(), OnlineUser::NickSort());

	auto p = find_if(ouList_, OnlineUser::UrlCompare(aUser.hint));
	if (p != ouList_.end()) {
		auto hinted = *p;
		ouList_.erase(p);
		return hinted;
	}

	return nullptr;
}

string ClientManager::getFormatedNicks(const HintedUser& aUser) const noexcept {
	auto ret = formatUserProperty<OnlineUser::Nick>(aUser, true);
	if (ret.empty()) {
		// offline
		RLock l(cs);
		auto i = offlineUsers.find(const_cast<CID*>(&aUser.user->getCID()));
		//dcassert(i != offlineUsers.end());
		if (i != offlineUsers.end()) {
			return i->second.getNick();
		}
	}
	return ret;
}

string ClientManager::getFormatedHubNames(const HintedUser& aUser) const noexcept {
	auto ret = formatUserProperty<OnlineUser::HubName>(aUser, false);
	return ret.empty() ? STRING(OFFLINE) : ret;
}

optional<OfflineUser> ClientManager::getOfflineUser(const CID& cid) {
	RLock l(cs);
	auto i = offlineUsers.find(const_cast<CID*>(&cid));
	if (i != offlineUsers.end()) {
		return i->second;
	}
	return nullopt;
}

string ClientManager::getField(const CID& aCID, const string& aHint, const char* aField) const noexcept {
	RLock l(cs);
	OnlinePairC p;
	auto u = findOnlineUserHintUnsafe(aCID, aHint, p);
	if (u) {
		auto value = u->getIdentity().get(aField);
		if (!value.empty()) {
			return value;
		}
	}

	for(auto i = p.first; i != p.second; ++i) {
		auto value = i->second->getIdentity().get(aField);
		if (!value.empty()) {
			return value;
		}
	}

	return Util::emptyString;
}

string ClientManager::getDLSpeed(const CID& aCID) const noexcept {
	RLock l(cs);
	auto i = onlineUsers.find(const_cast<CID*>(&aCID));
	if (i != onlineUsers.end()) {
		return Util::formatBytes(i->second->getIdentity().get("DS")) + "/s";
	}
	return STRING(OFFLINE);
}

uint8_t ClientManager::getSlots(const CID& aCID) const noexcept {
	RLock l(cs);
	auto i = onlineUsers.find(const_cast<CID*>(&aCID));
	if (i != onlineUsers.end()) {
		return static_cast<uint8_t>(Util::toInt(i->second->getIdentity().get("SL")));
	}
	return 0;
}

bool ClientManager::hasClient(const string& aUrl) const noexcept{
	RLock l(cs);

	auto i = clients.find(const_cast<string*>(&aUrl));
	return i != clients.end();
}

string ClientManager::findHub(const string& aIpPort, bool aNmdc) const noexcept {
	string ip;
	string port = "411";
	Util::parseIpPort(aIpPort, ip, port);

	string url;

	RLock l(cs);
	for (const auto& c: clients | map_values) {
		if (c->getIp() == ip && AirUtil::isAdcHub(c->getHubUrl()) == !aNmdc) {
			// If exact match is found, return it
			if (c->getPort() == port) {
				return c->getHubUrl();
			}

			// Port is not always correct, so use this as a best guess...
			url = c->getHubUrl();
		}
	}

	return url;
}

const string& ClientManager::findHubEncoding(const string& aUrl) const noexcept {
	RLock l(cs);

	auto i = clients.find(const_cast<string*>(&aUrl));
	if (i != clients.end()) {
		return i->second->get(HubSettings::NmdcEncoding);
	}
	return SETTING(NMDC_ENCODING);
}

HintedUser ClientManager::findLegacyUser(const string& aNick) const noexcept {
	if (aNick.empty())
		return HintedUser();

	RLock l(cs);
	for (const auto& i: clients | map_values) {
		if (!AirUtil::isAdcHub(i->getHubUrl())) {
			auto nmdcHub = static_cast<NmdcHub*>(i.get());

			/** @todo run the search directly on non-UTF-8 nicks when we store them. */
			auto ou = nmdcHub->findUser(nmdcHub->toUtf8(aNick));
			if (ou) {
				return HintedUser(ou->getUser(), ou->getHubUrl());
			}
		}
	}

	return HintedUser();
}

UserPtr ClientManager::getUser(const string& aNick, const string& aHubUrl) noexcept {
	auto cid = makeCid(aNick, aHubUrl);

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
	auto u = users.emplace(const_cast<CID*>(&p->getCID()), p);
	return u.first->second;
}

UserPtr ClientManager::getUser(const CID& aCID) noexcept {
	auto user = findUser(aCID);
	if (user) {
		// dcassert(user->getCID() == aCID);
		return user;
	}

	if (aCID == getMe()->getCID()) {
		return getMe();
	}

	UserPtr p(new User(aCID));

	WLock l(cs);
	auto u = users.emplace(const_cast<CID*>(&p->getCID()), p);
	return u.first->second;
}

UserPtr ClientManager::loadUser(const string& aCID, const string& aUrl, const string& aNick, uint32_t aLastSeen) noexcept {
	// Skip loading any old data without correct CID
	if (aCID.length() != 39) {
		return nullptr;
	}

	auto u = getUser(CID(aCID));
	addOfflineUser(u, aNick, aUrl, aLastSeen);
	return u;
}

UserPtr ClientManager::findUser(const CID& aCID) const noexcept {
	RLock l(cs);
	auto ui = users.find(const_cast<CID*>(&aCID));
	if(ui != users.end()) {
		return ui->second;
	}
	return nullptr;
}

// deprecated
bool ClientManager::isOp(const UserPtr& aUser, const string& aHubUrl) const noexcept {
	RLock l(cs);
	auto p = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));
	for (auto i = p.first; i != p.second; ++i) {
		if(i->second->getClient()->getHubUrl() == aHubUrl) {
			return i->second->getIdentity().isOp();
		}
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

void ClientManager::putOnline(const OnlineUserPtr& ou) noexcept {
	{
		WLock l(cs);
		onlineUsers.emplace(const_cast<CID*>(&ou->getUser()->getCID()), ou.get());
	}
	
	if (!ou->getUser()->isOnline()) {
		// User came online
		ou->getUser()->setFlag(User::ONLINE);

		{
			WLock l(cs);
			auto i = offlineUsers.find(const_cast<CID*>(&ou->getUser()->getCID()));
			if (i != offlineUsers.end())
				offlineUsers.erase(i);
		}

		fire(ClientManagerListener::UserConnected(), *ou, true);
	} else {
		fire(ClientManagerListener::UserConnected(), *ou, false);
	}
}

void ClientManager::putOffline(const OnlineUserPtr& ou, bool aDisconnectTransfers) noexcept {
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
				so we ensure that we should always find the user in atleast one of the lists.
				*/
				if (diff == 1) {
					offlineUsers.emplace(const_cast<CID*>(&ou->getUser()->getCID()), OfflineUser(ou->getIdentity().getNick(), ou->getHubUrl(), GET_TIME()));
				}

				onlineUsers.erase(i);
				break;
			}
		}
	}

	if(diff == 1) { //last user
		UserPtr& u = ou->getUser();
		u->unsetFlag(User::ONLINE);
		//updateUser(*ou);
		if (aDisconnectTransfers) {
			ConnectionManager::getInstance()->disconnect(u);
		}

		fire(ClientManagerListener::UserDisconnected(), u, true);
	} else if(diff > 1) {
		fire(ClientManagerListener::UserDisconnected(), *ou, false);
	}
}

void ClientManager::listProfiles(const UserPtr& aUser, ProfileTokenSet& profiles_) const noexcept {
	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));
	for (auto i = op.first; i != op.second; ++i) {
		profiles_.insert(i->second->getClient()->get(HubSettings::ShareProfile));
	}
}

optional<ProfileToken> ClientManager::findProfile(UserConnection& uc, const string& aUserSID) const noexcept {
	if (!aUserSID.empty()) {
		RLock l(cs);
		auto op = onlineUsers.equal_range(const_cast<CID*>(&uc.getUser()->getCID())) | map_values;
		for (const auto& ou: op) {
			if (compare(ou->getIdentity().getSIDString(), aUserSID) == 0) {
				uc.setHubUrl(ou->getClient()->getHubUrl());
				return ou->getClient()->get(HubSettings::ShareProfile);
			}
		}

		//don't accept invalid SIDs
		return optional<ProfileToken>();
	}

	//no SID specified, find with hint.
	OnlinePairC op;

	RLock l(cs);
	auto ou = findOnlineUserHintUnsafe(uc.getUser()->getCID(), uc.getHubUrl(), op);
	if (ou) {
		return ou->getClient()->get(HubSettings::ShareProfile);
	} else if (op.first != op.second) {
		//pick a random profile
		return op.first->second->getClient()->get(HubSettings::ShareProfile);
	}

	return nullopt;
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
	if (i != clients.end() && i->second->isConnected()) {
		return i->second->isActive();		
	}

	return false;
}

string ClientManager::findMySID(const UserPtr& aUser, string& hubUrl_, bool aAllowFallback) const noexcept {
	if (!hubUrl_.empty()) { // we cannot find the correct SID without a hubUrl
		OnlinePairC op;

		RLock l(cs);
		auto u = findOnlineUserHintUnsafe(aUser->getCID(), hubUrl_, op);
		if (u) {
			return u->getClient()->getMyIdentity().getSIDString();
		} else if (aAllowFallback) {
			hubUrl_ = op.first->second->getClient()->getHubUrl();
			return op.first->second->getClient()->getMyIdentity().getSIDString();
		}
	}

	return Util::emptyString;
}

OnlineUser* ClientManager::findOnlineUserHintUnsafe(const CID& aCID, const string& aHintUrl, OnlinePairC& p) const noexcept {
	p = onlineUsers.equal_range(const_cast<CID*>(&aCID));
	if (p.first == p.second) // no user found with the given CID.
		return nullptr;

	if (!aHintUrl.empty()) {
		for (auto i = p.first; i != p.second; ++i) {
			auto u = i->second;
			if(u->getClient()->getHubUrl() == aHintUrl) {
				return u;
			}
		}
	}

	return nullptr;
}

optional<ClientManager::ShareInfo> ClientManager::getShareInfo(const HintedUser& aUser) const noexcept {
	auto ou = findOnlineUser(aUser);
	if (ou) {
		return ShareInfo({ Util::toInt64(ou->getIdentity().getShareSize()), Util::toInt(ou->getIdentity().getSharedFiles()) });
	}

	return nullopt;
}

User::UserInfoList ClientManager::getUserInfoList(const UserPtr& aUser) const noexcept {
	User::UserInfoList ret;

	{
		RLock l(cs);
		auto p = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));

		for (auto i = p.first; i != p.second; ++i) {
			auto& ou = i->second;
			ret.emplace_back(ou->getHubUrl(), ou->getClient()->getHubName(), Util::toInt64(ou->getIdentity().getShareSize()));
		}
	}

	return ret;
}

HintedUser ClientManager::checkDownloadUrl(const HintedUser& aUser) const noexcept {
	auto userInfoList = ClientManager::getInstance()->getUserInfoList(aUser);
	if (!userInfoList.empty() && find(userInfoList, aUser.hint) == userInfoList.end()) {
		sort(userInfoList.begin(), userInfoList.end(), User::UserHubInfo::ShareSort());

		return { aUser.user, userInfoList.back().hubUrl };
	}

	return aUser;
}

HintedUser ClientManager::checkOnlineUrl(const HintedUser& aUser) const noexcept {
	auto u = findOnlineUser(aUser, true);
	if (u && u->getHubUrl() != aUser.hint) {
		return { aUser.user, u->getHubUrl() };
	}

	return aUser;
}

OnlineUserPtr ClientManager::findOnlineUser(const HintedUser& aUser, bool aAllowFallback) const noexcept {
	return findOnlineUser(aUser.user->getCID(), aUser.hint, aAllowFallback);
}

OnlineUserPtr ClientManager::findOnlineUser(const CID& cid, const string& hintUrl, bool aAllowFallback) const noexcept {
	RLock l(cs);

	OnlinePairC p;
	auto u = findOnlineUserHintUnsafe(cid, hintUrl, p);
	if (u) // found an exact match (CID + hint).
		return u;

	if (p.first == p.second) // no user found with the given CID.
		return nullptr;

	// return a random user
	return aAllowFallback ? p.first->second : nullptr;
}

bool ClientManager::connect(const UserPtr& aUser, const string& aToken, bool aAllowUrlChange, string& lastError_, string& hubHint_, bool& isProtocolError_, ConnectionType aConnType) const noexcept {
	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));

	auto connectUser = [&] (OnlineUser* ou) -> bool {
		isProtocolError_ = false;

		auto ret = ou->getClient()->connect(*ou, aToken, lastError_);
		if (ret == AdcCommand::SUCCESS) {
			return true;
		}

		//get the error string
		if (ret == AdcCommand::ERROR_TLS_REQUIRED) {
			isProtocolError_ = true;
			lastError_ = STRING(SOURCE_NO_ENCRYPTION);
		} else if (ret == AdcCommand::ERROR_PROTOCOL_UNSUPPORTED) {
			isProtocolError_ = true;
			lastError_ = STRING_F(REMOTE_PROTOCOL_UNSUPPORTED, lastError_);
		} else if (ret == AdcCommand::ERROR_BAD_STATE) {
			lastError_ = STRING(CONNECTING_IN_PROGRESS);
		} else if (ret == AdcCommand::ERROR_FEATURE_MISSING) {
			isProtocolError_ = true;
			lastError_ = STRING(NO_NATT_SUPPORT);
		} else if (ret == AdcCommand::ERROR_PROTOCOL_GENERIC) {
			isProtocolError_ = true;
			lastError_ = STRING(UNABLE_CONNECT_USER);
		}

		return false;
	};

	if (aConnType == CONNECTION_TYPE_PM) {
		if (!aUser->isSet(User::TLS)) {
			isProtocolError_ = true;
			lastError_ = STRING(SOURCE_NO_ENCRYPTION);
			return false;
		}

		// We don't care which hub we use to establish the connection all we need to know is the user supports the CCPM feature.
		if (!aUser->isSet(User::CCPM)) {
			isProtocolError_ = true;
			lastError_ = STRING(CCPM_NOT_SUPPORTED);
			return false;
		}
	}

	// Prefer the hinted hub
	auto p = find_if(op, [&hubHint_](const pair<CID*, OnlineUser*>& ouc) { return ouc.second->getHubUrl() == hubHint_; });
	if (p != op.second && connectUser(p->second)) {
		return true;
	}

	if (!aAllowUrlChange) {
		return false;
	}

	// Connect via any available hub
	for (auto i = op.first; i != op.second; ++i) {
		if (connectUser(i->second)) {
			hubHint_ = i->second->getHubUrl();
			return true;
		}
	}

	return false;
}

bool ClientManager::privateMessageHooked(const HintedUser& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho) noexcept {
	auto user = findOnlineUser(aUser);
	if (!user) {
		error_ = STRING(USER_OFFLINE);
		return false;
	}
	
	return user->getClient()->sendPrivateMessageHooked(user, aMessage, error_, aEcho);
}

void ClientManager::userCommand(const HintedUser& user, const UserCommand& uc, ParamMap& params_, bool aCompatibility) noexcept {
	auto hubUrl = (!uc.getHub().empty() && hasClient(uc.getHub())) ? uc.getHub() : user.hint;
	auto ou = findOnlineUser(user.user->getCID(), hubUrl);
	if (!ou) {
		return;
	}

	ou->getIdentity().getParams(params_, "user", aCompatibility);
	ou->getClient()->getHubIdentity().getParams(params_, "hub", false);
	ou->getClient()->getMyIdentity().getParams(params_, "my", aCompatibility);
	ou->getClient()->sendUserCmd(uc, params_);
}

bool ClientManager::sendUDP(AdcCommand& cmd, const CID& aCID, bool aNoCID /*false*/, bool aNoPassive /*false*/, const string& aKey /*Util::emptyString*/, const string& aHubUrl /*Util::emptyString*/) noexcept {
	auto u = findOnlineUser(aCID, aHubUrl);
	if (!u) {
		return false;
	}

	if (cmd.getType() == AdcCommand::TYPE_UDP && !u->getIdentity().isUdpActive()) {
		if (u->getUser()->isNMDC() || aNoPassive) {
			return false;
		}

		cmd.setType(AdcCommand::TYPE_DIRECT);
		cmd.setTo(u->getIdentity().getSID());
		u->getClient()->send(cmd);
	} else {
		try {
			COMMAND_DEBUG(cmd.toString(), DebugManager::TYPE_CLIENT_UDP, DebugManager::OUTGOING, u->getIdentity().getUdpIp() + ":" + u->getIdentity().getUdpPort());
			auto cmdStr = aNoCID ? cmd.toString() : cmd.toString(getMe()->getCID());
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

			udp->writeTo(u->getIdentity().getUdpIp(), u->getIdentity().getUdpPort(), cmdStr);
		} catch(const SocketException&) {
			dcdebug("Socket exception sending ADC UDP command\n");
		}
	}

	return true;
}

void ClientManager::infoUpdated() noexcept {
	RLock l(cs);
	for (auto c: clients | map_values) {
		if (c->isConnected()) {
			c->info();
		}
	}
}

void ClientManager::userUpdated(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID())) | map_values;
	for (const auto& ou : op) {
		ou->getClient()->callAsync([=] {
			ou->getClient()->updated(ou);
		});
	}
}

pair<size_t, size_t> ClientManager::countAschSupport(const OrderedStringSet& aHubUrls) const noexcept {
	size_t found = 0;
	size_t total = 0;

	RLock l(cs);
	for (const auto& u : onlineUsers | map_values) {
		if (!u->getUser()->isSet(User::BOT) && aHubUrls.find(u->getHubUrl()) != aHubUrls.end()) {
			total++;
			if (u->getUser()->isSet(User::ASCH))
				found++;
		}
	}

	return { found, total };
}

void ClientManager::on(ClientListener::OutgoingSearch, const Client* aClient, const SearchPtr& aSearch) noexcept {
	fire(ClientManagerListener::OutgoingSearch(), aClient->getHubUrl(), aSearch);
}

void ClientManager::on(ClientListener::PrivateMessage, const Client*, const ChatMessagePtr& aMessage) noexcept {
	fire(ClientManagerListener::PrivateMessage(), aMessage);
}

void ClientManager::on(ClientListener::NmdcSearch, Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize,
									int aFileType, const string& aString, bool aIsPassive) noexcept
{
	fire(ClientManagerListener::IncomingSearch(), aString);

	bool hideShare = aClient->get(HubSettings::ShareProfile) == SP_HIDDEN;

	SearchResultList l;
	ShareManager::getInstance()->nmdcSearch(l, aString, aSearchType, aSize, aFileType, aIsPassive ? 5 : 10, hideShare);
	if(l.size() > 0) {
		if (aIsPassive) {
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
					udp->writeTo(ip, port, sr->toSR(*aClient));

			} catch(...) {
				dcdebug("Search caught error\n");
			}
		}
	} else if(!aIsPassive && (aFileType == Search::TYPE_TTH) && (aString.compare(0, 4, "TTH:") == 0)) {
		if (SETTING(EXTRA_PARTIAL_SLOTS) == 0) //disable partial uploads by setting 0
			return;

		PartsInfo partialInfo;
		string bundle;
		bool add = false, reply = false;
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
			udp->writeTo(Socket::resolve(ip), port, cmd.toString(getMe()->getCID()));
		} catch(...) {
			dcdebug("Partial search caught error\n");		
		}
	}
}

optional<uint64_t> ClientManager::search(string& aHubUrl, const SearchPtr& aSearch, string& error_) noexcept {
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&aHubUrl));
	if(i != clients.end()) {
		if (!i->second->isConnected()) {
			error_ = "Hub is not connected";
			return nullopt;
		}

		return i->second->queueSearch(aSearch);
	}

	error_ = "Hub was not found";
	return nullopt;
}

bool ClientManager::cancelSearch(const void* aOwner) noexcept {
	bool ret = false;

	{
		RLock l(cs);
		for (const auto& c : clients | map_values) {
			if (c->cancelSearch(aOwner)) {
				ret = true;
			}
		}
	}

	return ret;
}

optional<uint64_t> ClientManager::getMaxSearchQueueTime(const void* aOwner) const noexcept {
	optional<uint64_t> maxTime;

	{
		RLock l(cs);
		for (const auto& c : clients | map_values) {
			auto t = c->getQueueTime(aOwner);
			if (t) {
				maxTime = maxTime ? max(*t, *maxTime) : *t;
			}
		}
	}

	return maxTime;
}

bool ClientManager::hasSearchQueueOverflow() const noexcept {
	return find_if(clients | map_values, [](const ClientPtr& aClient) {
		return aClient->hasSearchOverflow();
	}).base() != clients.end();
}

int ClientManager::getMaxSearchQueueSize() const noexcept {
	int maxSize = 0;

	{
		RLock l(cs);
		for (const auto& c : clients | map_values) {
			auto s = c->getSearchQueueSize();
			if (s) {
				maxSize = maxSize ? max(s, maxSize) : s;
			}
		}
	}

	return maxSize;

}

bool ClientManager::directSearch(const HintedUser& aUser, const SearchPtr& aSearch, string& error_) noexcept {
	if (aUser.user->isNMDC()) {
		error_ = "Direct search is not supported with NMDC users";
		return false;
	}

	auto ou = findOnlineUser(aUser);
	if (!ou) {
		error_ = STRING(USER_OFFLINE);
		return false;
	}

	return ou->getClient()->directSearch(*ou, aSearch, error_);
}

OnlineUserList ClientManager::searchNicks(const string& aPattern, size_t aMaxResults, bool aIgnorePrefix, const StringList& aHubUrls) const noexcept {
	auto search = RelevanceSearch<OnlineUserPtr>(aPattern, [aIgnorePrefix](const OnlineUserPtr& aUser) {
		return aIgnorePrefix ? stripNick(aUser->getIdentity().getNick()) : aUser->getIdentity().getNick();
	});

	{
		RLock l(cs);
		for (const auto& c: clients | map_values) {
			if (find(aHubUrls.begin(), aHubUrls.end(), c->getHubUrl()) == aHubUrls.end()) {
				continue;
			}

			OnlineUserList hubUsers;
			c->getUserList(hubUsers, false);
			for (const auto& ou : hubUsers) {
				if (ou->getUser() == me) {
					continue;
				}

				search.match(ou);
			}
		}
	}

	return search.getResults(aMaxResults);
}

void ClientManager::getOnlineClients(StringList& onlineClients_) const noexcept {
	RLock l (cs);
	for (const auto& c: clients | map_values) {
		if (c->isConnected())
			onlineClients_.push_back(c->getHubUrl());
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

optional<ClientManager::ClientStats> ClientManager::getClientStats() const noexcept {
	ClientStats stats;

	map<string, int> clientNames;
	{
		RLock l(cs);
		map<CID, OnlineUser*> uniqueUserMap;
		for (const auto& ou : onlineUsers | map_values) {
			uniqueUserMap.emplace(ou->getUser()->getCID(), ou);
		}

		stats.totalUsers = onlineUsers.size();
		stats.uniqueUsers = uniqueUserMap.size();
		if (stats.uniqueUsers == 0) {
			return nullopt;
		}

		// User counts
		for (const auto& ou : uniqueUserMap | map_values) {
			stats.totalShare += Util::toInt64(ou->getIdentity().getShareSize());
			if (ou->isHidden()) {
				stats.hiddenUsers++;
				continue;
			}

			if (ou->getIdentity().isBot()) {
				stats.bots++;
				if (!ou->getUser()->isNMDC()) {
					continue;
				}
			}

			if (ou->getIdentity().isOp()) {
				stats.operators++;
			}

			if (ou->getIdentity().hasActiveTcpConnectivity()) {
				stats.activeUsers++;
			}

			if (ou->getUser()->isNMDC()) {
				auto speed = Util::toDouble(ou->getIdentity().getNmdcConnection());
				if (speed > 0) {
					stats.nmdcConnection += static_cast<int64_t>((speed * 1000.0 * 1000.0) / 8.0);
					//stats.nmdcHasConnection++;
				}
				stats.nmdcUsers++;
			} else {
				auto up = ou->getIdentity().getAdcConnectionSpeed(false);
				if (up > 0) {
					stats.uploadSpeed += up;
					//stats.adcHasUpload++;
				}

				auto down = ou->getIdentity().getAdcConnectionSpeed(true);
				if (down > 0) {
					stats.downloadSpeed += down;
					//stats.adcHasDownload++;
				}
				stats.adcUsers++;
			}
		}

		// Client counts
		for (const auto& ou : uniqueUserMap | map_values) {
			auto app = ou->getIdentity().getApplication();
			auto pos = app.find(" ");

			if (pos != string::npos) {
				clientNames[app.substr(0, pos)]++;
			} else {
				clientNames["Unknown"]++;
			}
		}
	}

	auto countCompare = [](const pair<string, int>& i, const pair<string, int>& j) -> bool {
		return (i.second > j.second);
	};

	for (const auto& cp : clientNames) {
		stats.clients.push_back(cp);
	}

	sort(stats.clients.begin(), stats.clients.end(), countCompare);

	stats.finalize();

	return stats;
}

void ClientManager::ClientStats::finalize() noexcept {
	nmdcSpeedPerUser = Util::countAverageInt64(nmdcConnection, nmdcUsers);

	downPerAdcUser = Util::countAverageInt64(downloadSpeed, adcUsers);
	upPerAdcUser = Util::countAverageInt64(uploadSpeed, adcUsers);
}

string ClientManager::printClientStats() const noexcept {
	auto optionalStats = getClientStats();
	if (!optionalStats) {
		return "No hubs";
	}

	auto stats = *optionalStats;

	string lb = "\r\n";
	string ret = boost::str(boost::format(
		"\r\n\r\n-=[ Hub statistics ]=-\r\n\r\n\
All users: %d\r\n\
Unique users: %d (%d%%)\r\n\
Active/operators/bots/hidden: %d (%d%%) / %d (%d%%) / %d (%d%%) / %d (%d%%)\r\n\
Protocol users (ADC/NMDC): %d / %d\r\n\
Total share: %s (%s per user)\r\n\
Average ADC connection speed: %s down, %s up\r\n\
Average NMDC connection speed: %s")

% stats.totalUsers
% stats.uniqueUsers % Util::countPercentage(stats.uniqueUsers, stats.totalUsers)
% stats.activeUsers % Util::countPercentage(stats.activeUsers, stats.uniqueUsers)
% stats.operators % Util::countPercentage(stats.operators, stats.uniqueUsers)
% stats.bots % Util::countPercentage(stats.bots, stats.uniqueUsers)
% stats.hiddenUsers % Util::countPercentage(stats.hiddenUsers, stats.uniqueUsers)
% stats.adcUsers % stats.nmdcUsers
% Util::formatBytes(stats.totalShare) % Util::formatBytes(Util::countAverageInt64(stats.totalShare, stats.uniqueUsers))
% Util::formatConnectionSpeed(stats.downPerAdcUser) % Util::formatConnectionSpeed(stats.upPerAdcUser)
% Util::formatConnectionSpeed(stats.nmdcSpeedPerUser)

);

	ret += lb;
	ret += lb;
	ret += "Clients (from unique users)";
	ret += lb;

	for (const auto& c: stats.clients) {
		ret += c.first + ":\t\t" + Util::toString(c.second) + " (" + Util::toString(Util::countPercentage(c.second, stats.uniqueUsers)) + "%)" + lb;
	}

	return ret;
}

UserPtr& ClientManager::getMe() noexcept {
	if(!me) {
		auto newMe = new User(getMyCID());

		WLock l(cs);
		auto u = users.emplace(const_cast<CID*>(&newMe->getCID()), newMe);
		me = u.first->second;
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

void ClientManager::addOfflineUser(const UserPtr& user, const string& aNick, const string& aUrl, uint32_t lastSeen/*0*/) noexcept{
	if (!user || aNick.empty() || aUrl.empty())
		return;

	WLock l(cs);
	auto p = offlineUsers.emplace(const_cast<CID*>(&user->getCID()), OfflineUser(aNick, aUrl, lastSeen));
	if (!p.second && lastSeen > 0) {
		p.first->second.setLastSeen(lastSeen);
	}
}

string ClientManager::getMyNick(const string& hubUrl) const noexcept {
	RLock l(cs);
	auto i = clients.find(const_cast<string*>(&hubUrl));
	if(i != clients.end()) {
		return i->second->getMyIdentity().getNick();
	}
	return Util::emptyString;
}

void ClientManager::on(ClientListener::Connected, const Client* aClient) noexcept {
	auto c = getClient(aClient->getHubUrl());
	if (c) {
		fire(ClientManagerListener::ClientConnected(), c);
	}
}

void ClientManager::on(ClientListener::UserUpdated, const Client*, const OnlineUserPtr& user) noexcept {
	fire(ClientManagerListener::UserUpdated(), *user);
}

void ClientManager::on(ClientListener::UsersUpdated, const Client*, const OnlineUserList& l) noexcept {
	for (const auto& ou: l) {
		fire(ClientManagerListener::UserUpdated(), *ou); 
	}
}

void ClientManager::on(ClientListener::HubUpdated, const Client* aClient) noexcept {
	auto c = getClient(aClient->getHubUrl());
	if (c) {
		fire(ClientManagerListener::ClientUpdated(), c);
	}
}

void ClientManager::on(ClientListener::Disconnected, const string& aHubUrl, const string& /*aLine*/) noexcept {
	fire(ClientManagerListener::ClientDisconnected(), aHubUrl);
}

void ClientManager::on(ClientListener::HubUserCommand, const Client* client, int aType, int ctx, const string& name, const string& command) noexcept {
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
	auto client = find_if(clients | map_values, [uniqueId](const ClientPtr& c) { return c->getToken() == uniqueId; });
	if(client.base() == clients.end()) { return false; }
	hubUrl_ = (*client)->getHubUrl();

	token_.erase(0, slash + 1);


	// get the connection and total slots
	OnlinePairC p;
	auto ou = findOnlineUserHintUnsafe(aCID, hubUrl_, p);
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

bool ClientManager::connectNMDCSearchResult(const string& aUserIP, const string& hubIpPort, HintedUser& user_, string& nick_, string& connection_, string& file_, string& hubName_) noexcept {
	user_.hint = findHub(hubIpPort, true);
	if(user_.hint.empty()) {
		// Could happen if hub has multiple URLs / IPs
		user_ = findLegacyUser(nick_);
		if (!user_) {
			return false;
		}
	}

	auto encoding = findHubEncoding(user_.hint);
	nick_ = Text::toUtf8(nick_, encoding);
	file_ = Text::toUtf8(file_, encoding);
	hubName_ = Text::toUtf8(hubName_, encoding);

	if (!user_.user) {
		user_.user = findUser(nick_, user_.hint);
		if (!user_.user) {
			return false;
		}
	}

	setIPUser(user_, aUserIP);

	auto ou = findOnlineUser(user_);
	if (ou) {
		connection_ = ou->getIdentity().getConnectionString();
	}

	return true;
}


bool ClientManager::processChatMessage(const ChatMessagePtr& aMessage, const Identity& aMyIdentity, const ActionHook<MessageHighlightList, const ChatMessagePtr>& aHook) {
	aMessage->parseMention(aMyIdentity);

	{
		MessageHighlightList highlights;

		try {
			auto results = aHook.runHooksDataThrow(aMessage);
			highlights = ActionHook<MessageHighlightList>::normalizeListItems(results);
		} catch (const HookRejectException&) {
			return false;
		}

		aMessage->parseHighlights(aMyIdentity, highlights);
	}

	return true;
}

} // namespace dcpp
