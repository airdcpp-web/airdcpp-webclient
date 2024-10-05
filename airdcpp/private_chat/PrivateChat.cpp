/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#include <airdcpp/private_chat/PrivateChat.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/core/crypto/CryptoManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/message/Message.h>


namespace dcpp {

PrivateChat::PrivateChat(const HintedUser& aUser, UserConnection* aUc) :
	cache(SettingsManager::PM_MESSAGE_CACHE), replyTo(aUser), uc(aUc), 
	online(aUser.user->isOnline()), 
	hubName(ClientManager::getInstance()->getHubName(aUser.hint)) 
{
	initConnectState();

	ClientManager::getInstance()->addListener(this);

	readLastLog();
	checkIgnored();
}

void PrivateChat::initConnectState() {
	if (uc) {
		ccpmState = CCPMState::CONNECTED;
		uc->addListener(this);
	} else {
		delayEvents.addEvent(CCPM_AUTO, [this] { checkAlwaysCCPM(); }, 1000);
		checkCCPMHubBlocked();
	}
}

void PrivateChat::readLastLog() {
	auto lastLogLines = LogManager::readFromEnd(getLogPath(), SETTING(MAX_PM_HISTORY_LINES), Util::convertSize(16, Util::KB));
	if (!lastLogLines.empty()) {
		auto logMessage = std::make_shared<LogMessage>(
			lastLogLines,
			LogMessage::SEV_INFO,
			LogMessage::Type::HISTORY,
			Util::emptyString,
			LogMessage::InitFlags::INIT_DISABLE_TIMESTAMP | LogMessage::InitFlags::INIT_READ
		);
		cache.addMessage(logMessage);
	}
}

PrivateChat::~PrivateChat() {
	ClientManager::getInstance()->removeListener(this);
	if (uc)
		uc->removeListener(this);
}

void PrivateChat::checkIgnored() noexcept {
	if (replyTo.user->isIgnored()) {
		statusMessage(STRING(PM_IGNORE_INFO), LogMessage::SEV_INFO, LogMessage::Type::SYSTEM);
	}
}

bool PrivateChat::allowCCPM() const noexcept {
	if (!CryptoManager::getInstance()->TLSOk())
		return false;

	if (!getUser()->isSet(User::CCPM) || !getUser()->isSet(User::TLS))
		return false;

	return true;
}

void PrivateChat::checkCCPMHubBlocked() noexcept {
	if (replyTo.user->isSet(User::NMDC)) {
		return;
	}

	// Auto connecting?
	if (ccReady() || (allowCCPM() && SETTING(ALWAYS_CCPM))) {
		return;
	}

	auto ou = ClientManager::getInstance()->findOnlineUser(replyTo, false);
	if (!ou) {
		return;
	}

	// We are not connecting, check the current hub only
	if (ou->supportsCCPM()) {
		return;
	}

	//Encryption disabled...
	if (!getUser()->isSet(User::TLS) || !CryptoManager::getInstance()->TLSOk())
		return;

	// Only report if the client is known to support CCPM
	auto app = ou->getIdentity().getApplication();
	if (app.find("AirDC++") == string::npos) {
		return;
	}

	auto msg = boost::str(boost::format(
"%s\r\n\r\n\
%s")

% STRING_F(CCPM_BLOCKED_WARNING, hubName)
% (getUser()->isSet(User::CCPM) ? STRING(OTHER_CCPM_SUPPORTED) : STRING(OTHER_MEANS_COMMUNICATION))
);

	statusMessage(msg, LogMessage::SEV_WARNING, LogMessage::Type::SYSTEM);
}

const string& PrivateChat::ccpmStateToString(CCPMState aState) noexcept {
	switch (aState) {
	case CCPMState::CONNECTING: return STRING(CONNECTING);
	case CCPMState::CONNECTED: return STRING(CONNECTED);
	case CCPMState::DISCONNECTED: return STRING(DISCONNECTED);
	}

	return Util::emptyString;
}


void PrivateChat::CCPMConnected(UserConnection* aUc) {
	ccpmState = CCPMState::CONNECTED;
	setUc(aUc);
	aUc->addListener(this);
	statusMessage(STRING(CCPM_ESTABLISHED), LogMessage::SEV_INFO, LogMessage::Type::SERVER);
	fire(PrivateChatListener::CCPMStatusUpdated(), this);
}

void PrivateChat::CCPMDisconnected() {
	if (ccReady()) {
		ccpmState = CCPMState::DISCONNECTED;
		uc->removeListener(this);
		setUc(nullptr);
		statusMessage(STRING(CCPM_DISCONNECTED), LogMessage::SEV_INFO, LogMessage::Type::SERVER);
		fire(PrivateChatListener::CCPMStatusUpdated(), this);
		delayEvents.addEvent(CCPM_AUTO, [this] { checkAlwaysCCPM(); }, 1000);
	}
}

bool PrivateChat::sendMessageHooked(const OutgoingChatMessage& aMessage, string& error_) {
	if (Util::isChatCommand(aMessage.text)) {
		fire(PrivateChatListener::ChatCommand(), this, aMessage);
		// TODO: don't continue and run hooks after this with API v2
	}

	if (ccReady()) {
		return uc->sendPrivateMessageHooked(aMessage, error_);
	}

	return ClientManager::getInstance()->privateMessageHooked(replyTo, aMessage, error_);
}

void PrivateChat::closeCC(bool now, bool aNoAutoConnect) {
	if (ccReady()) {
		if (aNoAutoConnect) {
			sendPMInfo(NO_AUTOCONNECT);
			allowAutoCCPM = false;
		}

		//Don't disconnect graceless so the last command can be transferred successfully.
		uc->disconnect(now && !aNoAutoConnect);
		if (now) {
			ccpmState = CCPMState::DISCONNECTED;
			uc->removeListener(this);
			setUc(nullptr);
		}
	}
}

void PrivateChat::handleMessage(const ChatMessagePtr& aMessage) noexcept {
	if (aMessage->getReplyTo()->getHubUrl() != replyTo.hint) {
		setHubUrl(aMessage->getReplyTo()->getHubUrl());

		if (!ccReady()) {
			statusMessage(STRING_F(MESSAGES_SENT_THROUGH_REMOTE, hubName), LogMessage::SEV_INFO, LogMessage::Type::SERVER);
		}
	}

	if (SETTING(LOG_PRIVATE_CHAT)) {
		logMessage(aMessage->format());
	}

	cache.addMessage(aMessage);
	fire(PrivateChatListener::PrivateMessage(), this, aMessage);
}

void PrivateChat::setRead() noexcept {
	auto unreadInfo = cache.setRead();
	
	if (unreadInfo.chatMessages > 0) {
		sendPMInfo(PrivateChat::MSG_SEEN);
	}

	if (unreadInfo.hasMessages()) {
		fire(PrivateChatListener::MessagesRead(), this);
	}
}

int PrivateChat::clearCache() noexcept {
	auto ret = cache.clear();
	if (ret > 0) {
		fire(PrivateChatListener::MessagesCleared(), this);
	}

	return ret;
}

void PrivateChat::statusMessage(const string& aMessage, LogMessage::Severity aSeverity, LogMessage::Type aType, const string& aLabel, const string& aOwner) noexcept {
	auto message = std::make_shared<LogMessage>(aMessage, aSeverity, aType, aLabel);

	if (aOwner.empty() && aType != LogMessage::Type::SPAM && aType != LogMessage::Type::PRIVATE) {
		cache.addMessage(message);
	}
	fire(PrivateChatListener::StatusMessage(), this, message, aOwner);
}

void PrivateChat::close() {
	fire(PrivateChatListener::Close(), this);

	//PM window closed, signal it if the user supports CPMI
	if (ccReady() && uc) {
		if (uc->getSupports().includes(UserConnection::FEATURE_ADC_CPMI))
			sendPMInfo(QUIT);
		else
			closeCC(true, false);
	}

	LogManager::getInstance()->removePmCache(getUser());
}

void PrivateChat::startCC() {
	if (!replyTo.user->isOnline() || ccpmState != CCPMState::DISCONNECTED) {
		return;
	}

	ccpmState = CCPMState::CONNECTING;

	auto token = ConnectionManager::getInstance()->tokens.createToken(CONNECTION_TYPE_PM);
	auto connectResult = ClientManager::getInstance()->connect(replyTo, token, true, CONNECTION_TYPE_PM);
	if (replyTo.hint != connectResult.getHubHint()) {
		setHubUrl(connectResult.getHubHint());
	}

	allowAutoCCPM = !connectResult.getIsProtocolError();

	if (!connectResult.getIsSuccess()) {
		ccpmState = CCPMState::DISCONNECTED;
		if (!connectResult.getError().empty()) {
			statusMessage(connectResult.getError(), LogMessage::SEV_ERROR, LogMessage::Type::SERVER);
		}
	} else {
		statusMessage(STRING(CCPM_ESTABLISHING), LogMessage::SEV_INFO, LogMessage::Type::SERVER);
		fire(PrivateChatListener::CCPMStatusUpdated(), this);
		delayEvents.addEvent(CCPM_TIMEOUT, [this] { checkCCPMTimeout(); }, 30000); // 30 seconds, completely arbitrary amount of time.
	}
	
}

void PrivateChat::checkAlwaysCCPM() {
	if (!SETTING(ALWAYS_CCPM) || !allowCCPM())
		return;

	if (allowAutoCCPM && ccpmState == CCPMState::DISCONNECTED) {
		startCC();
		allowAutoCCPM = allowAutoCCPM && ccpmAttempts++ < 3;
	} else if (ccReady()){
		allowAutoCCPM = true;
	}
}

void PrivateChat::checkCCPMTimeout() {
	if (ccpmState == CCPMState::CONNECTING) {
		statusMessage(STRING(CCPM_TIMEOUT), LogMessage::SEV_WARNING, LogMessage::Type::SERVER);
		ccpmState = CCPMState::DISCONNECTED;
		fire(PrivateChatListener::CCPMStatusUpdated(), this);
	} 
}

string PrivateChat::getLastCCPMError() const noexcept {

	if (!allowCCPM()) {
		if (!replyTo.user->isOnline()) {
			return STRING(USER_OFFLINE);
		} else if (replyTo.user->isNMDC()) {
			return STRING(CCPM_NOT_SUPPORTED_NMDC);
		} else if (!replyTo.user->isSet(User::TLS)) {
			return STRING(SOURCE_NO_ENCRYPTION);
		} else if (!CryptoManager::getInstance()->TLSOk()) {
				return STRING(ENCRYPTION_DISABLED);
		} else {
			return STRING(CCPM_NOT_SUPPORTED);
		}
	}
	return Util::emptyString;
}

void PrivateChat::onUserUpdated(const OnlineUser& aUser) noexcept {
	if (aUser.getUser() != replyTo.user)
		return;

	delayEvents.addEvent(USER_UPDATE, [this] {
		if (!online) {
			auto hubNames = ClientManager::getInstance()->getFormattedHubNames(replyTo);
			auto nicks = ClientManager::getInstance()->getFormattedNicks(replyTo);
			statusMessage(
				STRING(USER_WENT_ONLINE) + " [" + nicks + " - " + hubNames + "]",
				LogMessage::SEV_INFO,
				LogMessage::Type::SERVER
			);

			// online from a different hub?
			checkUserHub(false);
			online = true;
		}

		fire(PrivateChatListener::UserUpdated(), this);
	}, 1000);

	delayEvents.addEvent(CCPM_AUTO, [this] { checkAlwaysCCPM(); }, 3000);
}

void PrivateChat::on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool /*wasOffline*/) noexcept {
	onUserUpdated(aUser);
}

void PrivateChat::on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept {
	onUserUpdated(aUser);
}

void PrivateChat::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept{
	if (aUser != replyTo.user)
		return;

	if (wentOffline) {
		delayEvents.removeEvent(USER_UPDATE);
		if (ccpmState == CCPMState::CONNECTING) {
			delayEvents.removeEvent(CCPM_TIMEOUT);
			ccpmState = CCPMState::DISCONNECTED;
		}

		closeCC(true, false);
		allowAutoCCPM = true;
		online = false;
		fire(PrivateChatListener::UserUpdated(), this);
		statusMessage(STRING(USER_WENT_OFFLINE), LogMessage::SEV_INFO, LogMessage::Type::SERVER);
	} else {
		delayEvents.addEvent(USER_UPDATE, [this] {
			checkUserHub(true);
			fire(PrivateChatListener::UserUpdated(), this);
		}, 1000);
	}
}

void PrivateChat::checkUserHub(bool aWentOffline) noexcept {
	auto ou = ClientManager::getInstance()->findOnlineUser(replyTo, true);
	if (!ou)
		return;

	if (ou->getHubUrl() != replyTo.hint) {
		auto hubNameNew = ou->getClient()->getHubName();
		if (!ccReady()) {
			auto statusText = aWentOffline ? STRING_F(USER_OFFLINE_PM_CHANGE, hubName % hubNameNew) :
				STRING_F(MESSAGES_SENT_THROUGH, hubNameNew);

			statusMessage(statusText, LogMessage::SEV_INFO, LogMessage::Type::SERVER);
		}

		setHubUrl(ou->getHubUrl());
		hubName = hubNameNew;
	}
}

ClientPtr PrivateChat::getClient() const noexcept {
	return ClientManager::getInstance()->findClient(replyTo.hint);
}

void PrivateChat::setHubUrl(const string& aHubUrl) noexcept { 
	replyTo.hint = aHubUrl;
	hubName = ClientManager::getInstance()->getHubName(replyTo.hint);

	fire(PrivateChatListener::UserUpdated(), this);
}

void PrivateChat::sendPMInfo(uint8_t aType) {
	if (ccReady() && uc && uc->getSupports().includes(UserConnection::FEATURE_ADC_CPMI)) {
		AdcCommand c(AdcCommand::CMD_PMI);
		switch (aType) {
		case MSG_SEEN:
			c.addParam("SN", "1");
			break;
		case TYPING_ON:
			c.addParam("TP", "1");
			break;
		case TYPING_OFF:
			c.addParam("TP", "0");
			break;
		case NO_AUTOCONNECT:
			c.addParam("AC", "0");
			break;
		case QUIT:
			c.addParam("QU", "1");
			break;
		default:
			c.addParam("\n");
		}

		uc->send(c);
	}
}

void PrivateChat::on(AdcCommand::PMI, UserConnection*, const AdcCommand& cmd) noexcept{

	auto type = PMINFO_LAST;
	string tmp;

	//We only send one flag at a time so we can do it like this.
	if (cmd.hasFlag("SN", 0)) {
		type = MSG_SEEN;
	} else if (cmd.getParam("TP", 0, tmp)) {
		type = (tmp == "1") ? TYPING_ON : TYPING_OFF;
	} else if (cmd.getParam("AC", 0, tmp)) {
		allowAutoCCPM = tmp == "1" ? true : false;
		type = NO_AUTOCONNECT;
	} else if (cmd.hasFlag("QU", 0)) {
		type = QUIT;
	}

	if (type != PMINFO_LAST)
		fire(PrivateChatListener::PMStatus(), this, type);
}

void PrivateChat::logMessage(const string& aMessage) const noexcept {
	if (SETTING(LOG_PRIVATE_CHAT)) {
		ParamMap params;
		params["message"] = aMessage;
		fillLogParams(params);
		LogManager::getInstance()->log(getUser(), params);
	}
}

void PrivateChat::fillLogParams(ParamMap& params) const noexcept {
	const CID& cid = getUser()->getCID();;
	params["hubNI"] = [&] { return Util::listToString(ClientManager::getInstance()->getHubNames(cid)); };
	params["hubURL"] = [&] { return getHubUrl(); };
	params["userCID"] = [&cid] { return cid.toBase32(); };
	params["userNI"] = [&] { return ClientManager::getInstance()->getNick(getUser(), getHubUrl()); };
	params["myCID"] = [] { return ClientManager::getInstance()->getMyCID().toBase32(); };
}

string PrivateChat::getLogPath() const noexcept {
	ParamMap params;
	fillLogParams(params);
	return LogManager::getInstance()->getPath(getUser(), params);
}

}