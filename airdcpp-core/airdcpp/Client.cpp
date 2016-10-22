/* 
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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
#include "Client.h"

#include "AirUtil.h"
#include "BufferedSocket.h"
#include "ClientManager.h"
#include "ConnectivityManager.h"
#include "DebugManager.h"
#include "FavoriteManager.h"
#include "LogManager.h"
#include "MessageManager.h"
#include "ResourceManager.h"
#include "ShareManager.h"
#include "ThrottleManager.h"
#include "TimerManager.h"

namespace dcpp {

atomic<long> Client::allCounts[COUNT_UNCOUNTED];
atomic<long> Client::sharingCounts[COUNT_UNCOUNTED];
ClientToken idCounter = 0;

Client::Client(const string& aHubUrl, char aSeparator, const ClientPtr& aOldClient) :
	hubUrl(aHubUrl), separator(aSeparator), 
	myIdentity(ClientManager::getInstance()->getMe(), 0),
	clientId(aOldClient ? aOldClient->getClientId() : ++idCounter),
	lastActivity(GET_TICK()),
	cache(aOldClient ? aOldClient->getCache() : SettingsManager::HUB_MESSAGE_CACHE)
{
	TimerManager::getInstance()->addListener(this);
	ShareManager::getInstance()->addListener(this);

	string file, proto, query, fragment;
	Util::decodeUrl(hubUrl, proto, address, port, file, query, fragment);

	keyprint = Util::decodeQuery(query)["kp"];
}

Client::~Client() {
	dcdebug("Client %s was deleted\n", hubUrl.c_str());
}

void Client::reconnect() noexcept {
	disconnect(true);
	setAutoReconnect(true);
	setReconnDelay(0);
}

void Client::setActive() noexcept {
	fire(ClientListener::SetActive(), this);
}

void Client::shutdown(ClientPtr& aClient, bool aRedirect) {
	FavoriteManager::getInstance()->removeUserCommand(getHubUrl());
	TimerManager::getInstance()->removeListener(this);
	ShareManager::getInstance()->removeListener(this);

	if (!aRedirect) {
		fire(ClientListener::Disconnecting(), this);
	}

	if(sock) {
		BufferedSocket::putSocket(sock, [=] { // Ensure that the pointer won't be deleted too early
			state = STATE_DISCONNECTED;
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

void Client::on(ShareManagerListener::DefaultProfileChanged, ProfileToken aOldDefault, ProfileToken /*aNewDefault*/) noexcept {
	if (get(HubSettings::ShareProfile) == aOldDefault) {
		reloadSettings(false);
	}
}

void Client::on(ShareManagerListener::ProfileRemoved, ProfileToken aProfile) noexcept {
	if (get(HubSettings::ShareProfile) == aProfile) {
		reloadSettings(false);
	}
}

void Client::reloadSettings(bool aUpdateNick) noexcept {
	/// @todo update the nick in ADC hubs?
	string prevNick;
	if(!aUpdateNick)
		prevNick = get(Nick);

	auto fav = FavoriteManager::getInstance()->getFavoriteHubEntry(getHubUrl());

	*static_cast<HubSettings*>(this) = SettingsManager::getInstance()->getHubSettings();

	if(fav) {
		FavoriteManager::getInstance()->mergeHubSettings(fav, *this);
		if (!fav->getPassword().empty()) {
			setPassword(fav->getPassword());
		}

		favToken = fav->getToken();
	} else {
		setPassword(Util::emptyString);
	}

	searchQueue.setMinInterval(get(HubSettings::SearchInterval) * 1000); //convert from seconds
	if (aUpdateNick) {
		checkNick(get(Nick));
	} else {
		get(Nick) = prevNick;
	}
}

bool Client::changeBoolHubSetting(HubSettings::HubBoolSetting aSetting) noexcept {
	auto newValue = !get(aSetting);
	get(aSetting) = newValue;

	//save for a favorite hub if needed
	if (favToken > 0) {
		FavoriteManager::getInstance()->setHubSetting(hubUrl, aSetting, newValue);
	}
	return newValue;
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

void Client::connect(bool withKeyprint) noexcept {
	if (sock) {
		BufferedSocket::putSocket(sock);
		sock = 0;
	}

	redirectUrl = Util::emptyString;
	setAutoReconnect(true);
	setReconnDelay(120 + Util::rand(0, 60));
	reloadSettings(true);
	setRegistered(false);
	setMyIdentity(Identity(ClientManager::getInstance()->getMe(), 0));
	setHubIdentity(Identity());

	setConnectState(STATE_CONNECTING);

	try {
		sock = BufferedSocket::getSocket(separator, v4only());
		sock->addListener(this);
		sock->connect(
			Socket::AddressInfo(address, Socket::AddressInfo::TYPE_URL), 
			port, 
			AirUtil::isSecure(hubUrl), 
			SETTING(ALLOW_UNTRUSTED_HUBS), 
			true, 
			withKeyprint ? keyprint : Util::emptyString
		);
	} catch (const Exception& e) {
		setConnectState(STATE_DISCONNECTED);
		fire(ClientListener::Failed(), hubUrl, e.getError());
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
	COMMAND_DEBUG(aMessage, DebugManager::TYPE_HUB, DebugManager::OUTGOING, getIpPort());
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

void Client::statusMessage(const string& aMessage, LogMessage::Severity aSeverity, int aFlag) noexcept {
	auto message = std::make_shared<LogMessage>(aMessage, aSeverity);

	if (aFlag != ClientListener::FLAG_IS_SPAM) {
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

	fire(ClientListener::StatusMessage(), this, message, aFlag);
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
	if (ClientManager::getInstance()->hasClient(aRedirectUrl)) {
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
				statusMessage("*** " + STRING(JOINS) + ": " + aUser->getIdentity().getNick(), LogMessage::SEV_INFO, ClientListener::FLAG_IS_SYSTEM);
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
				statusMessage("*** " + STRING(PARTS) + ": " + aUser->getIdentity().getNick(), LogMessage::SEV_INFO, ClientListener::FLAG_IS_SYSTEM);
			}
		}
	}

	fire(ClientListener::UserRemoved(), this, aUser);
}

void Client::allowUntrustedConnect() noexcept {
	if (state != STATE_DISCONNECTED || !SETTING(ALLOW_UNTRUSTED_HUBS) || !AirUtil::isSecure(hubUrl))
		return;
	//Connect without keyprint just this once...
	connect(false);
}

void Client::onChatMessage(const ChatMessagePtr& aMessage) noexcept {
	if (MessageManager::getInstance()->isIgnoredOrFiltered(aMessage, this, false))
		return;

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

bool Client::saveFavorite() {
	FavoriteHubEntryPtr e = new FavoriteHubEntry();
	e->setServer(getHubUrl());
	e->setName(getHubName());
	e->setDescription(getHubDescription());
	e->setAutoConnect(true);
	if (!defpassword.empty()) {
		e->setPassword(defpassword);
	}

	return FavoriteManager::getInstance()->addFavoriteHub(e);
}

void Client::doRedirect() noexcept {
	if (redirectUrl.empty()) {
		return;
	}

	if (ClientManager::getInstance()->hasClient(redirectUrl)) {
		statusMessage(STRING(REDIRECT_ALREADY_CONNECTED), LogMessage::SEV_INFO);
		return;
	}

	auto newClient = ClientManager::getInstance()->redirect(getHubUrl(), redirectUrl);
	fire(ClientListener::Redirected(), getHubUrl(), newClient);
}

void Client::on(BufferedSocketListener::Failed, const string& aLine) noexcept {
	clearUsers();
	
	if(stateNormal())
		FavoriteManager::getInstance()->removeUserCommand(hubUrl);

	setConnectState(STATE_DISCONNECTED);
	statusMessage(aLine, LogMessage::SEV_WARNING); //Error?

	if (isKeyprintMismatch()) {
		fire(ClientListener::KeyprintMismatch(), this);
	}

	sock->removeListener(this);
	fire(ClientListener::Failed(), getHubUrl(), aLine);
}

bool Client::isKeyprintMismatch() const noexcept {
	return sock && !sock->isKeyprintMatch();
}

void Client::callAsync(AsyncF f) noexcept {
	if (sock) {
		sock->callAsync(move(f));
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
				addLine(STRING(HUB_NOT_PROTECTED));
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

string Client::getAllCountsStr() noexcept {
	char buf[128];
	return string(buf, snprintf(buf, sizeof(buf), "%ld/%ld/%ld",
		allCounts[COUNT_NORMAL].load(), allCounts[COUNT_REGISTERED].load(), allCounts[COUNT_OP].load()));
}

long Client::getDisplayCount(CountType aCountType) const noexcept {
	//return SETTING(SEPARATE_NOSHARE_HUBS) && isSharingHub() ? sharingCounts[aCountType] : allCounts[aCountType];
	return allCounts[aCountType];
}

void Client::addLine(const string& msg) noexcept {
	fire(ClientListener::AddLine(), this, msg);
}
 
void Client::on(BufferedSocketListener::Line, const string& aLine) noexcept {
	updateActivity();
	COMMAND_DEBUG(aLine, DebugManager::TYPE_HUB, DebugManager::INCOMING, getIpPort());
}

void Client::on(TimerManagerListener::Second, uint64_t aTick) noexcept{
	if (state == STATE_DISCONNECTED && getAutoReconnect() && (aTick > (getLastActivity() + getReconnDelay() * 1000))) {
		// Try to reconnect...
		connect();
	}

	if (searchQueue.hasWaitingTime(aTick)) return;

	if (isConnected()){
		auto s = move(searchQueue.pop());
		if (s){
			search(move(s));
		}
	}
}

}