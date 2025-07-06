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
#include <airdcpp/hub/Client.h>

#include <airdcpp/connection/socket/BufferedSocket.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/connectivity/ConnectivityManager.h>
#include <airdcpp/protocol/ProtocolCommandManager.h>
#include <airdcpp/favorites/FavoriteManager.h>
#include <airdcpp/util/LinkUtil.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/share/profiles/ShareProfileManager.h>
#include <airdcpp/connection/ThrottleManager.h>
#include <airdcpp/core/timer/TimerManager.h>
#include <airdcpp/util/ValueGenerator.h>

namespace dcpp {

atomic<long> Client::allCounts[COUNT_UNCOUNTED];
atomic<long> Client::sharingCounts[COUNT_UNCOUNTED];
atomic<ClientToken> idCounter { 0 };

#define FLOOD_PERIOD 60

Client::Client(const string& aHubUrl, char aSeparator, const ClientPtr& aOldClient) :
	myIdentity(ClientManager::getInstance()->getMe(), 0), lastActivity(GET_TICK()),
	cache(aOldClient ? MessageCache(aOldClient->getCache()) : MessageCache(SettingsManager::HUB_MESSAGE_CACHE)),
	ctmFloodCounter(FLOOD_PERIOD),
	searchFloodCounter(FLOOD_PERIOD),
	clientId(aOldClient ? aOldClient->getToken() : ++idCounter),
	hubUrl(aHubUrl),
	separator(aSeparator)
{
	TimerManager::getInstance()->addListener(this);
	ShareManager::getInstance()->getProfileMgr().addListener(this);

	string file, proto, query, fragment;
	LinkUtil::decodeUrl(hubUrl, proto, address, port, file, query, fragment);

	keyprint = LinkUtil::decodeQuery(query)["kp"];
}

Client::~Client() {
	dcdebug("Client %s was deleted\n", hubUrl.c_str());
}

void Client::reconnect() noexcept {
	disconnect(true);
	setAutoReconnect(true);
	setReconnDelay(0);
	supports.clear();
}

void Client::setActive() noexcept {
	fire(ClientListener::SetActive(), this);
}

void Client::shutdown(ClientPtr& aClient, bool aRedirect) {
	TimerManager::getInstance()->removeListener(this);
	ShareManager::getInstance()->getProfileMgr().removeListener(this);

	if (!aRedirect) {
		fire(ClientListener::Close(), this);
	}

	if(sock) {
		destroySocket([=, this] { // Ensure that the pointer won't be deleted too early
			sock = nullptr;

			// Users store a reference that prevents the client from being deleted
			// so the lists must be cleared manually 
			if (!aRedirect) {
				cache.clear();
			}

			aClient->clearUsers();
			updateCounts(true);
		});
	}
}

string Client::getDescription() const noexcept {
	string ret = get(HubSettings::Description);

	int upLimit = ThrottleManager::getInstance()->getUpLimit();
	if(upLimit > 0)
		ret = "[L:" + Util::toString(upLimit) + "KB] " + ret;
	return ret;
}

void Client::on(ShareProfileManagerListener::DefaultProfileChanged, ProfileToken aOldDefault, ProfileToken /*aNewDefault*/) noexcept {
	if (get(HubSettings::ShareProfile) == aOldDefault) {
		reloadSettings(false);
	}
}

void Client::on(ShareProfileManagerListener::ProfileRemoved, ProfileToken aProfile) noexcept {
	if (get(HubSettings::ShareProfile) == aProfile) {
		reloadSettings(false);
	}
}

/// @todo update the nick in ADC hubs?
void Client::reloadSettings(bool aUpdateNick) noexcept {
	HubSettings oldHubSettings = *static_cast<HubSettings*>(this);

	// Merging
	*static_cast<HubSettings*>(this) = SettingsManager::getInstance()->getHubSettings();

	auto fav = FavoriteManager::getInstance()->getFavoriteHubEntry(getHubUrl());
	if(fav) {
		FavoriteManager::getInstance()->mergeHubSettings(fav, *this);
		favToken = fav->getToken();
	}

	// Restore the old nick if nick change is not allowed
	if (aUpdateNick) {
		checkNick(get(Nick));
	} else {
		get(Nick) = oldHubSettings.get(Nick);
	}

	// Avoid unnecessary listener updates
	if (oldHubSettings == *static_cast<HubSettings*>(this)) {
		return;
	}

	// Something has changed
	if (fav) {
		if (!fav->getPassword().empty()) {
			setPassword(fav->getPassword());
		}
	} else {
		setPassword(Util::emptyString);
	}

	searchQueue.setMinInterval(get(HubSettings::SearchInterval) * 1000); //convert from seconds

	fire(ClientListener::SettingsUpdated(), this);
}


bool Client::toggleHubBoolSetting(HubSettings::HubBoolSetting aSetting) noexcept {
	auto newValue = static_cast<bool>(!get(aSetting));
	setHubSetting(aSetting, newValue);
	return newValue;
}

void Client::setHubSetting(HubSettings::HubBoolSetting aSetting, bool aNewValue) noexcept {
	get(aSetting) = aNewValue;

	//save for a favorite hub if needed
	if (favToken > 0) {
		FavoriteManager::getInstance()->setHubSetting(hubUrl, aSetting, aNewValue);
	}

	fire(ClientListener::SettingsUpdated(), this);
}

void Client::updated(const OnlineUserPtr& aUser) noexcept {
	fire(ClientListener::UserUpdated(), this, aUser);
}

void Client::updated(OnlineUserList& users) noexcept {
	//std::for_each(users.begin(), users.end(), [](OnlineUser* user) { UserMatchManager::getInstance()->match(*user); });

	fire(ClientListener::UsersUpdated(), this, users);
}

const string& Client::getUserIp4() const noexcept {
	if (!get(UserIp).empty()) {
		return get(UserIp);
	}
	return CONNSETTING(EXTERNAL_IP);
}

const string& Client::getUserIp6() const noexcept {
	if (!get(UserIp6).empty()) {
		return get(UserIp6);
	}
	return CONNSETTING(EXTERNAL_IP6);
}

bool Client::isActive() const noexcept {
	return isActiveV4() || isActiveV6();
}

bool Client::isActiveV4() const noexcept {
	return get(HubSettings::Connection) != SettingsManager::INCOMING_PASSIVE && get(HubSettings::Connection) != SettingsManager::INCOMING_DISABLED;
}

bool Client::isActiveV6() const noexcept {
	return !v4only() && get(HubSettings::Connection6) != SettingsManager::INCOMING_PASSIVE && get(HubSettings::Connection6) != SettingsManager::INCOMING_DISABLED;
}

void Client::destroySocket(const AsyncF& aShutdownAction) noexcept {
	state = STATE_DISCONNECTED;

	BufferedSocket::putSocket(sock, aShutdownAction);
}

void Client::connect(bool withKeyprint) noexcept {
	if (sock) {
		destroySocket();
	}

	redirectUrl = Util::emptyString;
	setAutoReconnect(true);
	setReconnDelay(120 + ValueGenerator::rand(0, 60));
	reloadSettings(true);
	setRegistered(false);
	setMyIdentity(Identity(ClientManager::getInstance()->getMe(), 0));
	setHubIdentity(Identity());

	setConnectState(STATE_CONNECTING);

	try {
		sock = BufferedSocket::getSocket(separator, v4only());
		sock->addListener(this);
		sock->connect(
			AddressInfo(address, AddressInfo::TYPE_URL), 
			SocketConnectOptions(port, LinkUtil::isSecure(hubUrl)),
			SETTING(ALLOW_UNTRUSTED_HUBS), 
			true, 
			withKeyprint ? keyprint : Util::emptyString
		);
	} catch (const Exception& e) {
		setConnectState(STATE_DISCONNECTED);
		fire(ClientListener::Disconnected(), hubUrl, e.getError());
	}
	updateActivity();
}

void Client::info() {
	callAsync([this] { infoImpl(); });
}

void Client::send(const char* aMessage, size_t aLen) {
	if (!isConnected() || !sock) {
		dcassert(0);
		return;
	}
	updateActivity();
	sock->write(aMessage, aLen);
	COMMAND_DEBUG(aMessage, ProtocolCommandManager::TYPE_HUB, ProtocolCommandManager::OUTGOING, getIpPort());
}

void Client::on(BufferedSocketListener::Connected) noexcept {
	statusMessage(STRING(CONNECTED), LogMessage::SEV_INFO);

	updateActivity();
	ip = sock->getIp();
	localIp = sock->getLocalIp();
	
	fire(ClientListener::Connected(), this);
	setConnectState(STATE_PROTOCOL);
}

void Client::setConnectState(State aState) noexcept {
	if (state == aState) {
		return;
	}

	state = aState;
	fire(ClientListener::ConnectStateChanged(), this, aState);
}

void Client::statusMessage(const string& aMessage, LogMessage::Severity aSeverity, LogMessage::Type aType, const string& aLabel, const string& aOwner) noexcept {
	auto message = std::make_shared<LogMessage>(aMessage, aSeverity, aType, aLabel);

	if (aOwner.empty() && aType != LogMessage::Type::SPAM && aType != LogMessage::Type::PRIVATE) {
		cache.addMessage(message);

		if (SETTING(LOG_STATUS_MESSAGES)) {
			ParamMap params;
			getHubIdentity().getParams(params, "hub", false);
			params["hubURL"] = getHubUrl();
			getMyIdentity().getParams(params, "my", true);
			params["message"] = aMessage;
			LOG(LogManager::STATUS, params);
		}
	}

	fire(ClientListener::StatusMessage(), this, message, aOwner);
}

void Client::setRead() noexcept {
	auto unreadInfo = cache.setRead();
	if (unreadInfo.hasMessages()) {
		fire(ClientListener::MessagesRead(), this);
	}
}

int Client::clearCache() noexcept {
	auto ret = cache.clear();
	if (ret > 0) {
		fire(ClientListener::MessagesCleared(), this);
	}

	return ret;
}

void Client::onPassword() noexcept {
	setConnectState(STATE_VERIFY);
	if (!defpassword.empty()) {
		password(defpassword);
		statusMessage(STRING(STORED_PASSWORD_SENT), LogMessage::SEV_INFO);
	} else {
		fire(ClientListener::GetPassword(), this);
	}
}

void Client::onRedirect(const string& aRedirectUrl) noexcept {
	if (ClientManager::getInstance()->findClient(aRedirectUrl)) {
		statusMessage(STRING(REDIRECT_ALREADY_CONNECTED), LogMessage::SEV_INFO);
		return;
	}

	redirectUrl = aRedirectUrl;

	if (SETTING(AUTO_FOLLOW)) {
		doRedirect();
	} else {
		fire(ClientListener::Redirect(), this, redirectUrl);
	}
}

void Client::onUserConnected(const OnlineUserPtr& aUser) noexcept {
	if (!aUser->getIdentity().isHub()) {
		ClientManager::getInstance()->putOnline(aUser);

		if (aUser->getUser() != ClientManager::getInstance()->getMe()) {
			if (!aUser->isHidden() && get(HubSettings::ShowJoins) || (get(HubSettings::FavShowJoins) && aUser->getUser()->isFavorite())) {
				statusMessage(STRING(JOINS) + ": " + aUser->getIdentity().getNick(), LogMessage::SEV_VERBOSE, LogMessage::Type::SYSTEM);
			}
		}
	}

	fire(ClientListener::UserConnected(), this, aUser);
}

void Client::onUserDisconnected(const OnlineUserPtr& aUser, bool aDisconnectTransfers) noexcept {
	if (!aUser->getIdentity().isHub()) {
		ClientManager::getInstance()->putOffline(aUser, aDisconnectTransfers);

		if (aUser->getUser() != ClientManager::getInstance()->getMe()) {
			if (!aUser->isHidden() && get(HubSettings::ShowJoins) || (get(HubSettings::FavShowJoins) && aUser->getUser()->isFavorite())) {
				statusMessage(STRING(PARTS) + ": " + aUser->getIdentity().getNick(), LogMessage::SEV_VERBOSE, LogMessage::Type::SYSTEM);
			}
		}
	}

	fire(ClientListener::UserRemoved(), this, aUser);
}

void Client::allowUntrustedConnect() noexcept {
	if (state != STATE_DISCONNECTED || !SETTING(ALLOW_UNTRUSTED_HUBS) || !LinkUtil::isSecure(hubUrl))
		return;
	//Connect without keyprint just this once...
	connect(false);
}

bool Client::sendMessageHooked(const OutgoingChatMessage& aMessage, string& error_) noexcept {
	if (Util::isChatCommand(aMessage.text)) {
		fire(ClientListener::ChatCommand(), this, aMessage);
		// TODO: don't continue and run hooks after this with API v2
	} else if (!stateNormal()) {
		error_ = STRING(CONNECTING_IN_PROGRESS);
		return false;
	}

	auto error = ClientManager::getInstance()->outgoingHubMessageHook.runHooksError(this, aMessage, *this);
	if (error) {
		error_ = ActionHookRejection::formatError(error);
		return false;
	}


	if (Util::isChatCommand(aMessage.text)) {
		return false;
	}

	return hubMessageHooked(aMessage, error_);
}

bool Client::sendPrivateMessageHooked(const OnlineUserPtr& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho) noexcept {
	if (!stateNormal() && !Util::isChatCommand(aMessage.text)) {
		error_ = STRING(CONNECTING_IN_PROGRESS);
		return false;
	}

	auto error = ClientManager::getInstance()->outgoingPrivateMessageHook.runHooksError(this, aMessage, HintedUser(aUser->getUser(), aUser->getHubUrl()), aEcho);
	if (error) {
		error_ = ActionHookRejection::formatError(error);
		return false;
	}

	if (Util::isChatCommand(aMessage.text)) {
		return false;
	}

	return privateMessageHooked(aUser, aMessage, error_, aEcho);
}

void Client::onPrivateMessage(const ChatMessagePtr& aMessage) noexcept {
	if (!ClientManager::processChatMessage(aMessage, getMyIdentity(), ClientManager::getInstance()->incomingPrivateMessageHook)) {
		return;
	}

	fire(ClientListener::PrivateMessage(), this, aMessage);
}

void Client::onChatMessage(const ChatMessagePtr& aMessage) noexcept {
	if (!ClientManager::processChatMessage(aMessage, getMyIdentity(), ClientManager::getInstance()->incomingHubMessageHook)) {
		return;
	}

	if (get(HubSettings::LogMainChat)) {
		ParamMap params;
		params["message"] = aMessage->format();
		getHubIdentity().getParams(params, "hub", false);
		params["hubURL"] = getHubUrl();
		getMyIdentity().getParams(params, "my", true);
		LOG(LogManager::CHAT, params);
	}

	cache.addMessage(aMessage);
	fire(ClientListener::ChatMessage(), this, aMessage);
}

void Client::on(BufferedSocketListener::Connecting) noexcept {
	statusMessage(STRING(CONNECTING_TO) + " " + getHubUrl() + " ...", LogMessage::SEV_INFO);
	fire(ClientListener::Connecting(), this);
}

FavoriteHubEntryPtr Client::saveFavorite() {
	FavoriteHubEntryPtr e = new FavoriteHubEntry();
	e->setServer(getHubUrl());
	e->setName(getHubName());
	e->setDescription(getHubDescription());
	e->setAutoConnect(true);
	if (!defpassword.empty()) {
		e->setPassword(defpassword);
	}

	return FavoriteManager::getInstance()->addFavoriteHub(e) ? e : nullptr;
}

void Client::doRedirect() noexcept {
	if (redirectUrl.empty()) {
		return;
	}

	if (ClientManager::getInstance()->findClient(redirectUrl)) {
		statusMessage(STRING(REDIRECT_ALREADY_CONNECTED), LogMessage::SEV_INFO);
		return;
	}

	auto newClient = ClientManager::getInstance()->redirect(getHubUrl(), redirectUrl);
	fire(ClientListener::Redirected(), getHubUrl(), newClient);
}

void Client::on(BufferedSocketListener::Failed, const string& aLine) noexcept {
	updateCounts(true);
	clearUsers();

	setConnectState(STATE_DISCONNECTED);
	statusMessage(aLine, LogMessage::SEV_WARNING);

	if (isKeyprintMismatch()) {
		fire(ClientListener::KeyprintMismatch(), this);
	}

	fire(ClientListener::Disconnected(), getHubUrl(), aLine);
}

bool Client::isKeyprintMismatch() const noexcept {
	return sock && !sock->isKeyprintMatch();
}

void Client::callAsync(AsyncF f) noexcept {
	if (sock) {
		sock->callAsync(std::move(f));
	}
}

void Client::disconnect(bool graceLess) noexcept {
	if(sock) 
		sock->disconnect(graceLess);
}

bool Client::isConnected() const noexcept {
	State s = state;
	return s != STATE_CONNECTING && s != STATE_DISCONNECTED; 
}

bool Client::isSocketSecure() const noexcept {
	return isConnected() && sock->isSecure();
}

bool Client::isTrusted() const noexcept {
	return isConnected() && sock->isTrusted();
}

std::string Client::getEncryptionInfo() const noexcept {
	return isConnected() ? sock->getEncryptionInfo() : Util::emptyString;
}

ByteVector Client::getKeyprint() const noexcept {
	return isConnected() ? sock->getKeyprint() : ByteVector();
}

void Client::updateActivity() noexcept {
	lastActivity = GET_TICK(); 
}

bool Client::isSharingHub() const noexcept {
	return get(HubSettings::ShareProfile) != SP_HIDDEN;
}

bool Client::updateCounts(bool aRemove) noexcept {
	// We always remove the count and then add the correct one if requested...
	if(countType != COUNT_UNCOUNTED) {
		allCounts[countType]--;
		if (countIsSharing) {
			sharingCounts[countType]--;
		}

		countType = COUNT_UNCOUNTED;
	}

	if(!aRemove) {
		if(getMyIdentity().isOp()) {
			countType = COUNT_OP;
		} else if(getMyIdentity().isRegistered()) {
			countType = COUNT_REGISTERED;
		} else {
			//disconnect before the hubcount is updated.
			if(SETTING(DISALLOW_CONNECTION_TO_PASSED_HUBS)) {
				statusMessage(STRING(HUB_NOT_PROTECTED), LogMessage::SEV_ERROR);
				disconnect(true);
				setAutoReconnect(false);
				return false;
			}

			countType = COUNT_NORMAL;
		}

		countIsSharing = isSharingHub();

		allCounts[countType]++;
		if (countIsSharing) {
			sharingCounts[countType]++;
		}
	}
	return true;
}

uint64_t Client::queueSearch(const SearchPtr& aSearch) noexcept {
	dcdebug("Queue search %s\n", aSearch->query.c_str());
	return searchQueue.add(aSearch);
}

optional<uint64_t> Client::getQueueTime(CallerPtr aOwner) const noexcept {
	return searchQueue.getQueueTime(Search::CompareOwner(aOwner));
}

string Client::getAllCountsStr() noexcept {
	char buf[128];
	return string(buf, snprintf(buf, sizeof(buf), "%ld/%ld/%ld",
		allCounts[COUNT_NORMAL].load(), allCounts[COUNT_REGISTERED].load(), allCounts[COUNT_OP].load()));
}

long Client::getDisplayCount(CountType aCountType) const noexcept {
	//return SETTING(SEPARATE_NOSHARE_HUBS) && isSharingHub() ? sharingCounts[aCountType] : allCounts[aCountType];
	return allCounts[aCountType];
}
 
void Client::on(BufferedSocketListener::Line, const string& aLine) noexcept {
	updateActivity();
	COMMAND_DEBUG(aLine, ProtocolCommandManager::TYPE_HUB, ProtocolCommandManager::INCOMING, getIpPort());
}

void Client::on(TimerManagerListener::Second, uint64_t aTick) noexcept{
	if (state == STATE_DISCONNECTED && getAutoReconnect() && (aTick > (getLastActivity() + getReconnDelay() * 1000))) {
		// Try to reconnect...
		connect();
	}

	if (isConnected()){
		auto s = searchQueue.maybePop();
		if (s) {
			fire(ClientListener::OutgoingSearch(), this, s);
			search(s);
		}
	}
}

FloodCounter::FloodLimits Client::getCTMLimits(const OnlineUser* aAdcUser) noexcept {
	// Is it a valid DC client?
	// There may be many connection attempts with MCN users so we don't want to ban them
	if (aAdcUser && ConnectionManager::getInstance()->isMCNUser(aAdcUser->getUser())) {
		return {
			100,
			150,
		};
	}

	return {
		15,
		40,
	};
}

FloodCounter::FloodLimits Client::getSearchLimits() noexcept {
	return {
		20,
		60,
	};
}

bool Client::checkIncomingCTM(const string& aTarget, const OnlineUser* aAdcUser) noexcept {
	auto result = ctmFloodCounter.handleRequest(aTarget, getCTMLimits(aAdcUser));
	if (result.type == FloodCounter::FloodType::OK) {
		return true;
	}

	auto nick = aAdcUser ? aAdcUser->getIdentity().getNick() : STRING(UNKNOWN);
	auto message = STRING_F(CONNECT_REQUEST_SPAM_FROM, aTarget % nick);

	handleFlood(result, ctmFloodCounter.appendFloodRate(aTarget, message, result.type == FloodCounter::FloodType::FLOOD_SEVERE));
	return false;
}

bool Client::checkIncomingSearch(const string& aTarget, const OnlineUser* aAdcUser) noexcept {
	auto result = searchFloodCounter.handleRequest(aTarget, getSearchLimits());
	if (result.type == FloodCounter::FloodType::OK) {
		return true;
	}

	auto nick = aAdcUser ? aAdcUser->getIdentity().getNick() : STRING(UNKNOWN);
	auto message = STRING_F(SEARCH_SPAM_FROM, aTarget % nick);

	handleFlood(result, searchFloodCounter.appendFloodRate(aTarget, message, result.type == FloodCounter::FloodType::FLOOD_SEVERE));
	return false;
}


void Client::handleFlood(const FloodCounter::FloodResult& aResult, const string& aMessage) noexcept {
	if (aResult.type == FloodCounter::FloodType::FLOOD_MINOR) {
		if (aResult.hitLimit) {
			// Status message only
			statusMessage(aMessage, LogMessage::SEV_VERBOSE, LogMessage::Type::SPAM);
		}
	} else if (aResult.type == FloodCounter::FloodType::FLOOD_SEVERE) {
		// If the flood is really severe, there may be lots of incoming messages waiting to be processed
		// We only want to do this once
		if (sock && sock->isDisconnecting()) {
			return;
		}

		statusMessage(
			STRING_F(HUB_DDOS_DISCONNECT, aMessage),
			LogMessage::SEV_ERROR
		);
		setReconnDelay(10 * 60); // 10 minutes
		disconnect(true);
	}
}

}