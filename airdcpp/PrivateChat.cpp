/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include "PrivateChat.h"

#include "ClientManager.h"
#include "ConnectionManager.h"
#include "LogManager.h"
#include "Message.h"


namespace dcpp {

PrivateChat::PrivateChat(const HintedUser& aUser, UserConnection* aUc) :
	uc(aUc), replyTo(aUser), ccpmAttempts(0), allowAutoCCPM(true), lastCCPMAttempt(0), ccpmState(DISCONNECTED),
	online(aUser.user->isOnline()), hubName(ClientManager::getInstance()->getHubName(aUser.hint)), cache(SettingsManager::PM_MESSAGE_CACHE) {
		
	if (aUc) {
		ccpmState = CONNECTED;
		aUc->addListener(this);
	} else {
		delayEvents.addEvent(CCPM_AUTO, [this] { checkAlwaysCCPM(); }, 1000);
		checkCCPMHubBlocked();
	}

	ClientManager::getInstance()->addListener(this);

	lastLogLines = LogManager::readFromEnd(getLogPath(), SETTING(SHOW_LAST_LINES_LOG), Util::convertSize(16, Util::KB));
}

PrivateChat::~PrivateChat() {
	ClientManager::getInstance()->removeListener(this);
	if (uc)
		uc->removeListener(this);
}

void PrivateChat::checkCCPMHubBlocked() noexcept {
	// Auto connecting?
	if (ccReady() || (getUser()->isSet(User::CCPM) && SETTING(ALWAYS_CCPM))) {
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

	// Only report if the client is known to support CCPM
	auto app = ou->getIdentity().getApplication();
	if (app.find("AirDC++ 3.") == string::npos && app.find("AirDC++w") == string::npos) {
		return;
	}

	auto msg = boost::str(boost::format(
"%s\r\n\r\n\
%s")

% STRING_F(CCPM_BLOCKED_WARNING, hubName)
% (getUser()->isSet(User::CCPM) ? STRING(OTHER_CCPM_SUPPORTED) : STRING(OTHER_MEANS_COMMUNICATION))
);

	statusMessage(msg, LogMessage::SEV_WARNING);
}

const string& PrivateChat::ccpmStateToString(uint8_t aState) noexcept {
	switch (aState) {
	case CONNECTING: return STRING(CONNECTING);
	case CONNECTED: return STRING(CONNECTED);
	case DISCONNECTED: return STRING(DISCONNECTED);
	}

	return Util::emptyString;
}


void PrivateChat::CCPMConnected(UserConnection* aUc) {
	ccpmState = CONNECTED;
	setUc(aUc);
	aUc->addListener(this);
	statusMessage(STRING(CCPM_ESTABLISHED), LogMessage::SEV_INFO);
	fire(PrivateChatListener::CCPMStatusUpdated(), this);
}

void PrivateChat::CCPMDisconnected() {
	if (ccReady()) {
		ccpmState = DISCONNECTED;
		uc->removeListener(this);
		setUc(nullptr);
		statusMessage(STRING(CCPM_DISCONNECTED), LogMessage::SEV_INFO);
		fire(PrivateChatListener::CCPMStatusUpdated(), this);
		delayEvents.addEvent(CCPM_AUTO, [this] { checkAlwaysCCPM(); }, 1000);
	}
}

bool PrivateChat::sendMessage(const string& msg, string& error_, bool thirdPerson) {
	if (ccReady()) {
		uc->pm(msg, thirdPerson);
		return true;
	}

	return ClientManager::getInstance()->privateMessage(replyTo, msg, error_, thirdPerson);
}

void PrivateChat::closeCC(bool now, bool noAutoConnect) {
	if (ccReady()) {
		if (noAutoConnect) {
			sendPMInfo(NO_AUTOCONNECT);
			allowAutoCCPM = false;
		}
		//Don't disconnect graceless so the last command can be transferred successfully.
		uc->disconnect(now && !noAutoConnect);
		if (now) {
			ccpmState = DISCONNECTED;
			uc->removeListener(this);
			setUc(nullptr);
		}
	}
}

void PrivateChat::handleMessage(const ChatMessagePtr& aMessage) noexcept {
	if (aMessage->getReplyTo()->getHubUrl() != replyTo.hint) {
		if (!ccReady()) {
			statusMessage(STRING_F(MESSAGES_SENT_THROUGH_REMOTE,
				ClientManager::getInstance()->getHubName(aMessage->getReplyTo()->getHubUrl())), LogMessage::SEV_INFO);
		}

		setHubUrl(aMessage->getReplyTo()->getHubUrl());
		fire(PrivateChatListener::UserUpdated(), this);
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

void PrivateChat::statusMessage(const string& aMessage, LogMessage::Severity aSeverity) noexcept {
	auto message = std::make_shared<LogMessage>(aMessage, aSeverity);

	fire(PrivateChatListener::StatusMessage(), this, message);
	cache.addMessage(message);
}

void PrivateChat::close() {
	fire(PrivateChatListener::Close(), this);

	//PM window closed, signal it if the user supports CPMI
	if (ccReady() && uc) {
		if (uc->isSet(UserConnection::FLAG_CPMI))
			sendPMInfo(QUIT);
		else
			closeCC(true, false);
	}

	LogManager::getInstance()->removePmCache(getUser());
}

void PrivateChat::startCC() {
	bool protocolError;
	if (!replyTo.user->isOnline() || ccpmState < DISCONNECTED) {
		return;
	}

	ccpmState = CONNECTING;
	string lastError;

	auto token = ConnectionManager::getInstance()->tokens.getToken(CONNECTION_TYPE_PM);

	auto newUrl = replyTo.hint;
	bool connecting = ClientManager::getInstance()->connect(replyTo.user, token, true, lastError, newUrl, protocolError, CONNECTION_TYPE_PM);
	if (replyTo.hint != newUrl) {
		setHubUrl(newUrl);
	}

	allowAutoCCPM = !protocolError;

	if (!connecting) {
		ccpmState = DISCONNECTED;
		if (!lastError.empty()) {
			statusMessage(lastError, LogMessage::SEV_ERROR);
		}
	} else {
		statusMessage(STRING(CCPM_ESTABLISHING), LogMessage::SEV_INFO);
		fire(PrivateChatListener::CCPMStatusUpdated(), this);
		delayEvents.addEvent(CCPM_TIMEOUT, [this] { checkCCPMTimeout(); }, 30000); // 30 seconds, completely arbitrary amount of time.
	}
	
}

void PrivateChat::checkAlwaysCCPM() {
	if (!SETTING(ALWAYS_CCPM) || !getUser()->isSet(User::CCPM))
		return;

	if (allowAutoCCPM && ccpmState == DISCONNECTED) {
		startCC();
		allowAutoCCPM = allowAutoCCPM && ccpmAttempts++ < 3;
	} else if (ccReady()){
		allowAutoCCPM = true;
	}
}

void PrivateChat::checkCCPMTimeout() {
	if (ccpmState == CONNECTING) {
		statusMessage(STRING(CCPM_TIMEOUT), LogMessage::SEV_INFO);
		fire(PrivateChatListener::CCPMStatusUpdated(), this);
		ccpmState = DISCONNECTED;
	} 
}

string PrivateChat::getLastCCPMError() {
	if (!replyTo.user->isSet(User::CCPM)) {
		if (!replyTo.user->isOnline()) {
			return STRING(USER_OFFLINE);
		} else if (replyTo.user->isNMDC()) {
			return STRING(CCPM_NOT_SUPPORTED_NMDC);
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
			auto hubNames = ClientManager::getInstance()->getFormatedHubNames(replyTo);
			auto nicks = ClientManager::getInstance()->getFormatedNicks(replyTo);
			statusMessage(STRING(USER_WENT_ONLINE) + " [" + nicks + " - " + hubNames + "]",
				LogMessage::SEV_INFO);

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
		if (ccpmState == CONNECTING) {
			delayEvents.removeEvent(CCPM_TIMEOUT);
			ccpmState = DISCONNECTED;
		}

		closeCC(true, false);
		allowAutoCCPM = true;
		online = false;
		fire(PrivateChatListener::UserUpdated(), this);
		statusMessage(STRING(USER_WENT_OFFLINE), LogMessage::SEV_INFO);
	} else {
		delayEvents.addEvent(USER_UPDATE, [this] {
			checkUserHub(true);
			fire(PrivateChatListener::UserUpdated(), this);
		}, 1000);
	}
}

void PrivateChat::checkUserHub(bool aWentOffline) noexcept {
	auto hubs = ClientManager::getInstance()->getHubs(replyTo.user->getCID());
	if (hubs.empty())
		return;

	if (find_if(hubs.begin(), hubs.end(), CompareFirst<string, string>(replyTo.hint)) == hubs.end()) {
		if (!ccReady()) {
			auto statusText = aWentOffline ? STRING_F(USER_OFFLINE_PM_CHANGE, hubName % hubs[0].second) :
				STRING_F(MESSAGES_SENT_THROUGH, hubs[0].second);

			statusMessage(statusText, LogMessage::SEV_INFO);
		}

		setHubUrl(hubs[0].first);
		hubName = hubs[0].second;
	}
}

ClientPtr PrivateChat::getClient() const noexcept {
	return ClientManager::getInstance()->getClient(replyTo.hint);
}

void PrivateChat::setHubUrl(const string& aHubUrl) noexcept { 
	replyTo.hint = aHubUrl;
	hubName = ClientManager::getInstance()->getHubName(replyTo.hint);

	fire(PrivateChatListener::UserUpdated(), this);
}

void PrivateChat::sendPMInfo(uint8_t aType) {
	if (ccReady() && uc && uc->isSet(UserConnection::FLAG_CPMI)) {
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
	}
	else if (cmd.getParam("TP", 0, tmp)) {
		type = (tmp == "1") ? TYPING_ON : TYPING_OFF;
	}
	else if (cmd.getParam("AC", 0, tmp)) {
		allowAutoCCPM = tmp == "1" ? true : false;
		type = NO_AUTOCONNECT;
	}
	else if (cmd.hasFlag("QU", 0)) {
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
	params["myCID"] = [] { return ClientManager::getInstance()->getMe()->getCID().toBase32(); };
}

string PrivateChat::getLogPath() const noexcept {
	ParamMap params;
	fillLogParams(params);
	return LogManager::getInstance()->getPath(getUser(), params);
}

}