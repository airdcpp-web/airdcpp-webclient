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
#include <airdcpp/hub/ClientManager.h>

#include <airdcpp/connectivity/ConnectivityManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/util/CryptoUtil.h>
#include <airdcpp/protocol/ProtocolCommandManager.h>
#include <airdcpp/util/LinkUtil.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/search/RelevanceSearch.h>
#include <airdcpp/core/localization/ResourceManager.h>

#include <airdcpp/hub/AdcHub.h>
#include <airdcpp/hub/NmdcHub.h>

#include <boost/algorithm/string/trim.hpp>

namespace dcpp {

using ranges::find_if;

ClientManager::ClientManager() : udp(make_unique<Socket>(Socket::TYPE_UDP)), lastOfflineUserCleanup(GET_TICK()) {
	TimerManager::getInstance()->addListener(this);
}

ClientManager::~ClientManager() {
	TimerManager::getInstance()->removeListener(this);
}

ClientPtr ClientManager::makeClient(const string& aHubURL, const ClientPtr& aOldClient) noexcept {
	if (LinkUtil::isAdcHub(aHubURL)) {
		return std::make_shared<AdcHub>(aHubURL, aOldClient);
	}

	return std::make_shared<NmdcHub>(aHubURL, aOldClient);
}

ClientPtr ClientManager::createClient(const string& aUrl) noexcept {

	auto c = ClientManager::makeClient(boost::trim_copy(aUrl));
	bool added = true;

	{
		WLock l(cs);
		auto [existingClientPair, added_] = clients.try_emplace(const_cast<string*>(&c->getHubUrl()), c);
		if (!added_) {
			added = added_;
			existingClientPair->second->setActive();
		}

		clientsById.try_emplace(c->getToken(), c);
	}

	if (!added) {
		c->shutdown(c, false);
		return nullptr;
	}

	c->addListener(this);

	fire(ClientManagerListener::ClientCreated(), c);
	return c;
}

void ClientManager::getOnlineClients(StringList& onlineClients_) const noexcept {
	RLock l(cs);
	for (const auto& c : clients | views::values) {
		if (c->isConnected())
			onlineClients_.push_back(c->getHubUrl());
	}
}

ClientPtr ClientManager::findClient(const string& aHubURL) const noexcept {
	RLock l (cs);
	auto i = clients.find(const_cast<string*>(&aHubURL));
	return i != clients.end() ? i->second : nullptr;
}

ClientPtr ClientManager::findClient(ClientToken aClientId) const noexcept {
	RLock l(cs);
	auto p = clientsById.find(aClientId);
	return p != clientsById.end() ? p->second : nullptr;
}

string ClientManager::findClientByIpPort(const string& aIpPort, bool aNmdc) const noexcept {
	string ip;
	string port = "411";
	Util::parseIpPort(aIpPort, ip, port);

	string url;

	RLock l(cs);
	for (const auto& c: clients | views::values) {
		if (c->getIp() == ip && LinkUtil::isAdcHub(c->getHubUrl()) == !aNmdc) {
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


void ClientManager::putClients() noexcept {
	vector<ClientPtr> clientList;

	{
		RLock l(cs);
		ranges::copy(clientsById | views::values, back_inserter(clientList));
	}

	for (auto& c: clientList) {
		putClient(c);
	}
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
	auto oldClient = findClient(aHubUrl);
	if (!oldClient) {
		return nullptr;
	}

	oldClient->disconnect(true);
	oldClient->shutdown(oldClient, true);
	oldClient->removeListener(this);

	auto newClient = ClientManager::makeClient(aNewUrl, oldClient);

	{
		WLock l(cs);
		clients.erase(const_cast<string*>(&aHubUrl));
		clients.try_emplace(const_cast<string*>(&newClient->getHubUrl()), newClient);
		clientsById[newClient->getToken()] = newClient;
	}

	newClient->addListener(this);

	fire(ClientManagerListener::ClientRedirected(), oldClient, newClient);
	return newClient;
}

string ClientManager::getHubName(const string& aHubUrl) const noexcept {
	if (auto c = findClient(aHubUrl); c) {
		return c->getHubName();
	}

	return Util::emptyString;
}

void ClientManager::myInfoUpdated() noexcept {
	RLock l(cs);
	for (auto c : clients | views::values) {
		if (c->isConnected()) {
			c->info();
		}
	}
}


// USERS
UserPtr ClientManager::getUser(const CID& aCID) noexcept {
	if (auto user = findUser(aCID); user) {
		// dcassert(user->getCID() == aCID);
		return user;
	}

	if (aCID == getMyCID()) {
		return getMe();
	}

	UserPtr p(new User(aCID));

	WLock l(cs);
	auto [userPair, _] = users.emplace(const_cast<CID*>(&p->getCID()), p);
	return userPair->second;
}

UserPtr ClientManager::loadUser(const string& aCID, const string& aUrl, const string& aNick, time_t aLastSeen) noexcept {
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
	if(auto ui = users.find(const_cast<CID*>(&aCID)); ui != users.end()) {
		return ui->second;
	}
	return nullptr;
}

StringList ClientManager::getHubUrls(const CID& aCID) const noexcept {
	StringList lst;

	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aCID));
	for (const auto& ou : op | pair_to_range | views::values) {
		lst.push_back(ou->getClient()->getHubUrl());
	}
	return lst;
}

OrderedStringSet ClientManager::getHubSet(const CID& aCID) const noexcept {
	auto hubs = getHubUrls(aCID);
	return OrderedStringSet(hubs.begin(), hubs.end());
}

StringList ClientManager::getHubNames(const CID& aCID) const noexcept {
	StringList lst;

	{
		RLock l(cs);
		auto op = onlineUsers.equal_range(const_cast<CID*>(&aCID));
		for (const auto& ou : op | pair_to_range | views::values) {
			lst.push_back(ou->getClient()->getHubName());
		}
	}

	sort(lst.begin(), lst.end());
	return lst;
}

void ClientManager::putOnline(const OnlineUserPtr& ou) noexcept {
	{
		WLock l(cs);
		onlineUsers.emplace(const_cast<CID*>(&ou->getUser()->getCID()), ou);
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
		auto [begin, end] = onlineUsers.equal_range(const_cast<CID*>(&ou->getUser()->getCID()));
		dcassert(begin != end);
		for(auto i = begin; i != end; ++i) {
			auto ou2 = i->second;
			if(ou == ou2) {
				diff = distance(begin, end);
				
				/*
				User went offline, cache his information in offlineUsers map.
				This needs to be done inside the same WLock that removes the onlineUser, 
				so we ensure that we should always find the user in atleast one of the lists.
				*/
				if (diff == 1) {
					offlineUsers.try_emplace(const_cast<CID*>(&ou->getUser()->getCID()), ou->getIdentity().getNick(), ou->getHubUrl(), GET_TIME());
				}

				onlineUsers.erase(i);
				break;
			}
		}
	}

	if (diff == 1) { //last user
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

optional<OfflineUser> ClientManager::getOfflineUser(const CID& cid) {
	RLock l(cs);
	if (auto i = offlineUsers.find(const_cast<CID*>(&cid)); i != offlineUsers.end()) {
		return i->second;
	}
	return nullopt;
}

void ClientManager::addOfflineUser(const UserPtr& user, const string& aNick, const string& aUrl, time_t lastSeen/*0*/) noexcept {
	if (!user || aNick.empty() || aUrl.empty())
		return;

	WLock l(cs);
	auto [offlineUserPair, added] = offlineUsers.try_emplace(const_cast<CID*>(&user->getCID()), aNick, aUrl, lastSeen);
	if (!added && lastSeen > 0) {
		offlineUserPair->second.setLastSeen(lastSeen);
	}
}


StringList ClientManager::getNicks(const UserPtr& aUser) const noexcept {
	return getNicks(aUser->getCID());
}

StringList ClientManager::getHubNames(const UserPtr& aUser) const noexcept {
	return getHubNames(aUser->getCID());
}

StringList ClientManager::getHubUrls(const UserPtr& aUser) const noexcept {
	return getHubUrls(aUser->getCID());
}

StringList ClientManager::getNicks(const CID& aCID, bool aAllowCID /*true*/) const noexcept {
	OrderedStringSet ret;

	{
		RLock l(cs);
		auto op = onlineUsers.equal_range(const_cast<CID*>(&aCID));
		for (const auto& ou: op | pair_to_range | views::values) {
			ret.insert(ou->getIdentity().getNick());
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

string ClientManager::getFormattedNicks(const HintedUser& aUser) const noexcept {
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

string ClientManager::getNick(const UserPtr& aUser, const string& aHubUrl, bool aAllowFallback /*true*/) const noexcept {
	{
		RLock l(cs);
		OnlinePairC p;
		if (auto ou = findOnlineUserHintUnsafe(aUser->getCID(), aHubUrl, p)) {
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


string ClientManager::getFormattedHubNames(const HintedUser& aUser) const noexcept {
	auto ret = formatUserProperty<OnlineUser::HubName>(aUser, false);
	return ret.empty() ? STRING(OFFLINE) : ret;
}

string ClientManager::getField(const CID& aCID, const string& aHint, const char* aField) const noexcept {
	RLock l(cs);
	OnlinePairC p;
	if (auto u = findOnlineUserHintUnsafe(aCID, aHint, p)) {
		auto value = u->getIdentity().get(aField);
		if (!value.empty()) {
			return value;
		}
	}

	for (const auto& ou : p | pair_to_range | views::values) {
		auto value = ou->getIdentity().get(aField);
		if (!value.empty()) {
			return value;
		}
	}

	return Util::emptyString;
}


OnlineUserList ClientManager::getOnlineUsers(const UserPtr& aUser) const noexcept {
	OnlineUserList ouList;

	RLock l(cs);
	auto p = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));
	ranges::copy(p | pair_to_range | views::values, back_inserter(ouList));
	return ouList;
}

OnlineUserPtr ClientManager::getOnlineUsers(const HintedUser& aUser, OnlineUserList& ouList_) const noexcept {
	ouList_ = getOnlineUsers(aUser);

	sort(ouList_.begin(), ouList_.end(), OnlineUser::NickSort());

	if (auto p = find_if(ouList_, OnlineUser::UrlCompare(aUser.hint)); p != ouList_.end()) {
		auto hinted = *p;
		ouList_.erase(p);
		return hinted;
	}

	return nullptr;
}

OnlineUserPtr ClientManager::findOnlineUserHintUnsafe(const CID& aCID, const string_view& aHintUrl, OnlinePairC& p) const noexcept {
	p = onlineUsers.equal_range(const_cast<CID*>(&aCID));
	if (p.first == p.second) // no user found with the given CID.
		return nullptr;

	if (!aHintUrl.empty()) {
		for (const auto& ou : p | pair_to_range | views::values) {
			if (ou->getClient()->getHubUrl() == aHintUrl) {
				return ou;
			}
		}
	}

	return nullptr;
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

void ClientManager::userUpdated(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));
	for (const auto& ou : op | pair_to_range | views::values) {
		ou->getClient()->callAsync([ou] {
			ou->getClient()->updated(ou);
		});
	}
}

optional<ClientManager::ShareInfo> ClientManager::getShareInfo(const HintedUser& aUser) const noexcept {
	auto ou = findOnlineUser(aUser);
	if (ou) {
		return ShareInfo({ Util::toInt64(ou->getIdentity().getShareSize()), Util::toInt(ou->getIdentity().getSharedFiles()) });
	}

	return nullopt;
}

void ClientManager::forEachOnlineUser(const OnlineUserCallback& aCallback, bool aIgnoreBots) const noexcept {
	RLock l(cs);
	for (const auto& u : onlineUsers | views::values) {
		if (aIgnoreBots && u->getUser()->isSet(User::BOT)) {
			continue;
		}

		aCallback(u);
	}
}

User::UserInfoList ClientManager::getUserInfoList(const UserPtr& aUser) const noexcept {
	User::UserInfoList ret;

	{
		RLock l(cs);
		auto op = onlineUsers.equal_range(const_cast<CID*>(&aUser->getCID()));
		for (const auto& ou : op | pair_to_range | views::values) {
			ret.emplace_back(ou->getHubUrl(), ou->getClient()->getHubName(), Util::toInt64(ou->getIdentity().getShareSize()));
		}
	}

	return ret;
}

OnlineUserList ClientManager::searchNicks(const string& aPattern, size_t aMaxResults, bool aIgnorePrefix, const StringList& aHubUrls) const noexcept {
	auto search = RelevanceSearch<OnlineUserPtr>(aPattern, [aIgnorePrefix](const OnlineUserPtr& aUser) {
		return aIgnorePrefix ? stripNick(aUser->getIdentity().getNick()) : aUser->getIdentity().getNick();
	});

	{
		RLock l(cs);
		for (const auto& c: clients | views::values) {
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



// CONNECT
UserConnectResult ClientManager::connect(const HintedUser& aUser, const string& aToken, bool aAllowUrlChange, ConnectionType aConnType) const noexcept {
	dcassert(aAllowUrlChange || !aUser.hint.empty());
	UserConnectResult result;

	auto connectUser = [&] (const OnlineUserPtr& ou) {
		result.resetError();

		string connectError;
		auto ret = ou->getClient()->connect(*ou, aToken, connectError);
		if (ret == AdcCommand::SUCCESS) {
			return true;
		}

		//get the error string
		if (ret == AdcCommand::ERROR_TLS_REQUIRED) {
			result.onProtocolError(STRING(SOURCE_NO_ENCRYPTION));
		} else if (ret == AdcCommand::ERROR_PROTOCOL_UNSUPPORTED) {
			result.onProtocolError(STRING_F(REMOTE_PROTOCOL_UNSUPPORTED, connectError));
		} else if (ret == AdcCommand::ERROR_BAD_STATE) {
			result.onMinorError(STRING(CONNECTING_IN_PROGRESS));
		} else if (ret == AdcCommand::ERROR_FEATURE_MISSING) {
			result.onProtocolError(STRING(NO_NATT_SUPPORT));
		} else if (ret == AdcCommand::ERROR_PROTOCOL_GENERIC) {
			result.onProtocolError(STRING(UNABLE_CONNECT_USER));
		} else {
			result.onMinorError(STRING_F(ERROR_CODE_X, STRING(UNKNOWN_ERROR) % ret));
		}

		return false;
	};

	if (aConnType == CONNECTION_TYPE_PM) {
		if (!aUser.user->isSet(User::TLS)) {
			result.onProtocolError(STRING(SOURCE_NO_ENCRYPTION));
			return result;
		}

		// We don't care which hub we use to establish the connection all we need to know is the user supports the CCPM feature.
		if (!aUser.user->isSet(User::CCPM)) {
			result.onProtocolError(STRING(CCPM_NOT_SUPPORTED));
			return result;
		}
	}

	OnlineUserList otherHubUsers;

	{
		// Prefer the hinted hub
		auto ou = getOnlineUsers(aUser, otherHubUsers);
		if (!ou) {
			result.onMinorError(STRING(USER_OFFLINE));
		} else if (connectUser(ou)) {
			result.onSuccess(aUser.hint);
			return result;
		}
	}

	// Offline in the hinted hub
	if (!aAllowUrlChange) {
		return result;
	}

	// Connect via any available hub
	for (const auto& ou: otherHubUsers) {
		if (connectUser(ou)) {
			result.onSuccess(ou->getHubUrl());
			return result;
		}
	}

	return result;
}

bool ClientManager::sendUDPHooked(AdcCommand& cmd, const HintedUser& to, const OutgoingUDPCommandOptions& aOptions, string& error_) noexcept {
	auto u = findOnlineUser(to);
	if (!u) {
		error_ = "User missing";
		return false;
	}

	if (u->getUser()->isNMDC()) {
		error_ = "NMDC user";
		return false;
	}

	if (cmd.getType() == AdcCommand::TYPE_UDP && !u->getIdentity().isUdpActive()) {
		if (aOptions.noPassive) {
			error_ = "The user is passive";
			return false;
		}

		cmd.setType(AdcCommand::TYPE_DIRECT);
		cmd.setTo(u->getIdentity().getSID());

		return u->getClient()->sendHooked(cmd, aOptions.owner, error_);
	} else {
		auto ipPort = u->getIdentity().getUdpIp() + ":" + u->getIdentity().getUdpPort();

		// Hooks
		{
			AdcCommand::ParamMap params;
			try {
				auto results = outgoingUdpCommandHook.runHooksDataThrow(this, cmd, u, ipPort);
				params = ActionHook<AdcCommand::ParamMap>::normalizeMap(results);
			} catch (const HookRejectException& e) {
				error_ = ActionHookRejection::formatError(e.getRejection());
				return false;
			}

			cmd.addParams(params);
		}

		// Listeners
		ProtocolCommandManager::getInstance()->fire(ProtocolCommandManagerListener::OutgoingUDPCommand(), cmd, ipPort, u);
		COMMAND_DEBUG(cmd.toString(), ProtocolCommandManager::TYPE_CLIENT_UDP, ProtocolCommandManager::OUTGOING, ipPort);

		// Send
		try {
			auto cmdStr = aOptions.noCID ? cmd.toString() : cmd.toString(getMyCID());
			if (!aOptions.encryptionKey.empty() && Encoder::isBase32(aOptions.encryptionKey.c_str())) {
				uint8_t keyChar[16];
				Encoder::fromBase32(aOptions.encryptionKey.c_str(), keyChar, 16);

				CryptoUtil::encryptSUDP(keyChar, cmdStr);
			}

			udp->writeTo(u->getIdentity().getUdpIp(), u->getIdentity().getUdpPort(), cmdStr);
		} catch(const SocketException&) {
			dcdebug("Socket exception sending ADC UDP command\n");
			error_ = "Socket error";
			return false;
		}
	}

	return true;
}


// MESSAGES
bool ClientManager::privateMessageHooked(const HintedUser& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho) const noexcept {
	auto user = findOnlineUser(aUser);
	if (!user) {
		error_ = STRING(USER_OFFLINE);
		return false;
	}
	
	return user->getClient()->sendPrivateMessageHooked(user, aMessage, error_, aEcho);
}

bool ClientManager::processChatMessage(const ChatMessagePtr& aMessage, const Identity& aMyIdentity, const ActionHook<MessageHighlightList, const ChatMessagePtr>& aHook) {
	aMessage->parseMention(aMyIdentity);

	{
		MessageHighlightList highlights;

		try {
			auto results = aHook.runHooksDataThrow(ClientManager::getInstance(), aMessage);
			highlights = ActionHook<MessageHighlightList>::normalizeListItems(results);
		} catch (const HookRejectException&) {
			return false;
		}

		aMessage->parseHighlights(aMyIdentity, highlights);
	}

	return true;
}


// SEARCHING
optional<uint64_t> ClientManager::hubSearch(const string& aHubUrl, const SearchPtr& aSearch, string& error_) noexcept {
	if (auto c = findClient(aHubUrl); c) {
		if (!c->isConnected()) {
			error_ = "Hub is not connected";
			return nullopt;
		}

		return c->queueSearch(aSearch);
	}

	error_ = "Hub was not found";
	return nullopt;
}

bool ClientManager::cancelSearch(CallerPtr aOwner) noexcept {
	bool ret = false;

	{
		RLock l(cs);
		for (const auto& c : clients | views::values) {
			if (c->cancelSearch(aOwner)) {
				ret = true;
			}
		}
	}

	return ret;
}

optional<uint64_t> ClientManager::getMaxSearchQueueTime(CallerPtr aOwner) const noexcept {
	optional<uint64_t> maxTime;

	{
		RLock l(cs);
		for (const auto& c : clients | views::values) {
			auto t = c->getQueueTime(aOwner);
			if (t) {
				maxTime = maxTime ? max(*t, *maxTime) : *t;
			}
		}
	}

	return maxTime;
}

bool ClientManager::hasSearchQueueOverflow() const noexcept {
	return find_if(clients | views::values, [](const ClientPtr& aClient) {
		return aClient->hasSearchOverflow();
	}).base() != clients.end();
}

int ClientManager::getMaxSearchQueueSize() const noexcept {
	int maxSize = 0;

	{
		RLock l(cs);
		for (const auto& c : clients | views::values) {
			auto s = c->getSearchQueueSize();
			if (s) {
				maxSize = maxSize ? max(s, maxSize) : s;
			}
		}
	}

	return maxSize;

}

bool ClientManager::directSearchHooked(const HintedUser& aUser, const SearchPtr& aSearch, string& error_) const noexcept {
	if (aUser.user->isNMDC()) {
		error_ = "Direct search is not supported with NMDC users";
		return false;
	}

	auto ou = findOnlineUser(aUser);
	if (!ou) {
		error_ = STRING(USER_OFFLINE);
		return false;
	}

	return ou->getClient()->directSearchHooked(*ou, aSearch, error_);
}

bool ClientManager::connectADCSearchHubUnsafe(string& token_, string& hubUrl_) const noexcept {
	// token format: [per-hub unique id] "/" [per-search actual token] (see AdcHub::search)
	auto slash = token_.find('/');
	if (slash == string::npos) { return false; }

	auto uniqueId = Util::toUInt32(token_.substr(0, slash));
	auto client = findClient(uniqueId);
	if (!client) {
		return false;
	}

	hubUrl_ = client->getHubUrl();
	token_.erase(0, slash + 1);
	return true;
}

bool ClientManager::connectADCSearchResult(const CID& aCID, string& token_, string& hubUrl_, string& connection_, uint8_t& slots_) const noexcept {
	RLock l(cs);
	if (!connectADCSearchHubUnsafe(token_, hubUrl_)) {
		return false;
	}

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

string ClientManager::getADCSearchHubUrl(const CID& aCID, const string& aHubIpPort) const noexcept {
	auto hubUrl = findClientByIpPort(aHubIpPort, false);
	if (hubUrl.empty()) {
		// Pick any hub where the user is online
		auto hubUrls = getHubUrls(aCID);
		if (!hubUrls.empty()) {
			return hubUrls.front();
		}
	}

	return hubUrl;
}


// STATS
void ClientManager::addStatsUser(const OnlineUserPtr& aUser, ClientStats& stats_) const noexcept {
	const auto& identity = aUser->getIdentity();

	stats_.totalShare += Util::toInt64(identity.getShareSize());
	if (aUser->isHidden()) {
		stats_.hiddenUsers++;
		return;
	}

	if (identity.isBot()) {
		stats_.bots++;
		if (!aUser->getUser()->isNMDC()) {
			return;
		}
	}

	if (identity.isOp()) {
		stats_.operators++;
	}

	if (identity.hasActiveTcpConnectivity()) {
		stats_.activeUsers++;
	}

	if (aUser->getUser()->isNMDC()) {
		auto speed = Util::toDouble(identity.getNmdcConnection());
		if (speed > 0) {
			stats_.nmdcConnection += static_cast<int64_t>((speed * 1000.0 * 1000.0) / 8.0);
			//stats.nmdcHasConnection++;
		}
		stats_.nmdcUsers++;
	} else {
		auto up = identity.getAdcConnectionSpeed(false);
		if (up > 0) {
			stats_.uploadSpeed += up;
			//stats.adcHasUpload++;
		}

		auto down = identity.getAdcConnectionSpeed(true);
		if (down > 0) {
			stats_.downloadSpeed += down;
			//stats.adcHasDownload++;
		}
		stats_.adcUsers++;
	}
}

optional<ClientManager::ClientStats> ClientManager::getClientStats() const noexcept {
	ClientStats stats;

	map<string, int> clientNames;
	{
		RLock l(cs);
		map<CID, OnlineUserPtr> uniqueUserMap;
		for (const auto& ou : onlineUsers | views::values) {
			uniqueUserMap.try_emplace(ou->getUser()->getCID(), ou);
		}

		stats.totalUsers = static_cast<int>(onlineUsers.size());
		stats.uniqueUsers = static_cast<int>(uniqueUserMap.size());
		if (stats.uniqueUsers == 0) {
			return nullopt;
		}

		// User counts
		for (const auto& ou : uniqueUserMap | views::values) {
			addStatsUser(ou, stats);
		}

		// Client counts
		for (const auto& ou : uniqueUserMap | views::values) {
			auto app = ou->getIdentity().getApplication();
			auto pos = app.find(" ");

			if (pos != string::npos) {
				clientNames[app.substr(0, pos)]++;
			} else {
				clientNames[STRING(UNKNOWN)]++;
			}
		}
	}

	auto countCompare = [](const pair<string, int>& i, const pair<string, int>& j) -> bool {
		return (i.second > j.second);
	};

	for (const auto& cp: clientNames) {
		stats.clients.emplace_back(cp);
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


// ME
UserPtr& ClientManager::getMe() noexcept {
	if(!me) {
		TigerHash tiger;
		tiger.update(getMyPID().data(), CID::SIZE);

		auto newMe = new User(CID(tiger.finalize()));

		WLock l(cs);
		auto [user, _] = users.emplace(const_cast<CID*>(&newMe->getCID()), newMe);
		me = user->second;
	}
	return me;
}

const CID& ClientManager::getMyPID() noexcept {
	if (!pid) {
		pid = CID(SETTING(PRIVATE_ID));
	}

	return pid;
}

const CID& ClientManager::getMyCID() noexcept {
	return getMe()->getCID();
}


// NMDC
void ClientManager::setNmdcIPUser(const UserPtr& user, const string& IP, const string& aUdpPort /*emptyString*/) noexcept {
	if (IP.empty())
		return;

	RLock l(cs);
	auto op = onlineUsers.equal_range(const_cast<CID*>(&user->getCID()));
	for (const auto& ou : op | pair_to_range | views::values) {
		ou->getIdentity().setIp4(IP);
		if (!aUdpPort.empty()) {
			ou->getIdentity().setUdp4Port(aUdpPort);
		}
	}
}

HintedUser ClientManager::getNmdcSearchHintedUserEncoded(const string& aNick, const string& aHubIpPort, const string& aUserIP, string& encoding_) noexcept {
	HintedUser ret;
	ret.hint = findClientByIpPort(aHubIpPort, true);
	if (ret.hint.empty()) {
		// Could happen if hub has multiple URLs / IPs
		ret = findNmdcUser(aNick);
		if (!ret || ret.hint.empty()) {
			return ret;
		}
	}

	encoding_ = findNmdcEncoding(ret.hint);
	if (!ret.user) {
		auto utf8Nick = Text::toUtf8(aNick, encoding_);

		ret.user = findNmdcUser(utf8Nick, ret.hint);
		if (!ret.user) {
			return ret;
		}
	}

	setNmdcIPUser(ret, aUserIP);
	return ret;
}

HintedUser ClientManager::getNmdcSearchHintedUserUtf8(const string& aUtf8Nick, const string& aHubIpPort, const string& aUserIP) noexcept {
	auto hubUrl = findClientByIpPort(aHubIpPort, true);
	if (!hubUrl.empty()) {
		auto u = findNmdcUser(aUtf8Nick, hubUrl);
		if (u) {
			setNmdcIPUser(u, aUserIP);
			return HintedUser(u, hubUrl);
		}
	}

	// Could happen if hub has multiple URLs / IPs
	auto ret = findNmdcUser(aUtf8Nick);
	if (ret) {
		setNmdcIPUser(ret, aUserIP);
	}

	return ret;
}

bool ClientManager::connectNMDCSearchResult(const string& aUserIP, const string& aHubIpPort, const string& aNick, HintedUser& user_, string& connection_, string& hubEncoding_) noexcept {
	user_ = getNmdcSearchHintedUserEncoded(aNick, aHubIpPort, aUserIP, hubEncoding_);
	if (!user_) {
		return false;
	}

	if (auto ou = findOnlineUser(user_); ou) {
		connection_ = ou->getIdentity().getConnectionString();
	}

	return true;
}

const string& ClientManager::findNmdcEncoding(const string& aUrl) const noexcept {
	if (auto c = findClient(aUrl); c) {
		return c->get(HubSettings::NmdcEncoding);
	}
	return SETTING(NMDC_ENCODING);
}

HintedUser ClientManager::findNmdcUser(const string& aNick) const noexcept {
	if (aNick.empty())
		return HintedUser();

	RLock l(cs);
	for (const auto& i: clients | views::values) {
		if (!LinkUtil::isAdcHub(i->getHubUrl())) {
			auto nmdcHub = static_cast<NmdcHub*>(i.get());
			auto ou = nmdcHub->findUser(nmdcHub->toUtf8(aNick));
			if (ou) {
				return HintedUser(ou->getUser(), ou->getHubUrl());
			}
		}
	}

	return HintedUser();
}

UserPtr ClientManager::getNmdcUser(const string& aNick, const string& aHubUrl) noexcept {
	auto cid = makeNmdcCID(aNick, aHubUrl);

	{
		RLock l(cs);
		auto ui = users.find(&cid);
		if(ui != users.end()) {
			dcassert(ui->second->getCID() == cid);
			ui->second->setFlag(User::NMDC);
			return ui->second;
		}
	}

	if(cid == getMyCID()) {
		return getMe();
	}

	UserPtr p(new User(cid));
	p->setFlag(User::NMDC);

	WLock l(cs);
	auto [userPair,_] = users.emplace(const_cast<CID*>(&p->getCID()), p);
	return userPair->second;
}

CID ClientManager::makeNmdcCID(const string& aNick, const string& aHubUrl) const noexcept {
	string n = Text::toLower(aNick);
	TigerHash th;
	th.update(n.c_str(), n.length());
	th.update(Text::toLower(aHubUrl).c_str(), aHubUrl.length());
	// Construct hybrid CID from the bits of the tiger hash - should be
	// fairly random, and hopefully low-collision
	return CID(th.finalize());
}

bool ClientManager::sendNmdcUDP(const string& aData, const string& aIP, const string& aPort) noexcept {
	try {
		auto ip = Socket::resolve(aIP);
		COMMAND_DEBUG(aData, ProtocolCommandManager::TYPE_CLIENT_UDP, ProtocolCommandManager::OUTGOING, ip + ":" + aPort);
		udp->writeTo(ip, aPort, aData);
	}
	catch (const SocketException&) {
		dcdebug("Socket exception sending NMDC UDP command\n");
		return false;
	}

	return true;
}

//store offline users information for approx 10minutes, no need to be accurate.
#define USERMAP_CLEANUP_INTERVAL_MINUTES 10

// LISTENERS
void ClientManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	
	if (aTick > (lastOfflineUserCleanup + USERMAP_CLEANUP_INTERVAL_MINUTES * 60 * 1000)) {
		cleanUserMap();
		lastOfflineUserCleanup = aTick;
	}

	RLock l (cs);
	for (auto c: clients | views::values)
		c->info();
}

void ClientManager::cleanUserMap() noexcept {
	WLock l(cs);

	// Collect some garbage...
	auto i = users.begin();
	while (i != users.end()) {
		dcassert(i->second->getCID() == *i->first);
		if (i->second.use_count() == 1) {
			if (auto n = offlineUsers.find(const_cast<CID*>(&i->second->getCID())); n != offlineUsers.end())
				offlineUsers.erase(n);
			users.erase(i++);
		} else {
			++i;
		}
	}
}

void ClientManager::on(ClientListener::Connected, const Client* aClient) noexcept {
	auto c = findClient(aClient->getHubUrl());
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
	auto c = findClient(aClient->getHubUrl());
	if (c) {
		fire(ClientManagerListener::ClientUpdated(), c);
	}
}

void ClientManager::on(ClientListener::Disconnected, const string& aHubUrl, const string& /*aLine*/) noexcept {
	fire(ClientManagerListener::ClientDisconnected(), aHubUrl);
}

void ClientManager::on(ClientListener::HubUserCommand, const Client* aClient, int aType, int ctx, const string& name, const string& command) noexcept {
	fire(ClientManagerListener::ClientUserCommand(), aClient, aType, ctx, name, command);
}

void ClientManager::on(ClientListener::OutgoingSearch, const Client* aClient, const SearchPtr& aSearch) noexcept {
	fire(ClientManagerListener::OutgoingSearch(), aClient->getHubUrl(), aSearch);
}

void ClientManager::on(ClientListener::PrivateMessage, const Client*, const ChatMessagePtr& aMessage) noexcept {
	fire(ClientManagerListener::PrivateMessage(), aMessage);
}

} // namespace dcpp
