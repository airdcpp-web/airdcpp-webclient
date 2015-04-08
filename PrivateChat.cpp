/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#include "ChatMessage.h"
#include "ConnectionManager.h"
#include "LogManager.h"

namespace dcpp 
{

PrivateChat::PrivateChat(const HintedUser& aUser, UserConnection* aUc) :
	uc(aUc), replyTo(aUser), ccpmAttempts(0), allowAutoCCPM(true), lastCCPMAttempt(0), state(DISCONNECTED),
	online(aUser.user->isOnline()), hubName(ClientManager::getInstance()->getHubName(aUser.hint)) {
		
	string _err = Util::emptyString;
	supportsCCPM = ClientManager::getInstance()->getSupportsCCPM(aUser.user, _err);
	lastCCPMError = _err;

	if (aUc) {
		state = CONNECTED;
		aUc->addListener(this);
	} else {
		delayEvents.addEvent(CCPM_AUTO, [this] { checkAlwaysCCPM(); }, 1000);
	}

	ClientManager::getInstance()->addListener(this);
}

PrivateChat::~PrivateChat() {
	ClientManager::getInstance()->removeListener(this);
	if (uc)
		uc->removeListener(this);
}


void PrivateChat::CCPMConnected(UserConnection* aUc) {
	state = CONNECTED;
	setUc(aUc);
	aUc->addListener(this);
	fire(PrivateChatListener::PMStatus(), CCPM_ESTABLISHED);
}

void PrivateChat::CCPMDisconnected() {
	if (ccReady()) {
		state = DISCONNECTED;
		uc->removeListener(this);
		setUc(nullptr);
		fire(PrivateChatListener::PMStatus(), CCPM_DISCONNECTED);
		delayEvents.addEvent(CCPM_AUTO, [this] { checkAlwaysCCPM(); }, 1000);
	}
}

bool PrivateChat::sendPrivateMessage(const HintedUser& aUser, const string& msg, string& error_, bool thirdPerson) {
	if (ccReady()) {
		uc->pm(msg, thirdPerson);
		return true;
	}

	return ClientManager::getInstance()->privateMessage(aUser, msg, error_, thirdPerson);
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
			state = DISCONNECTED;
			uc->removeListener(this);
			setUc(nullptr);
		}
	}
}

void PrivateChat::onExit() {
	//PM window closed, signal it if the user supports CPMI
	if (ccReady() && uc) {
		if (uc->isSet(UserConnection::FLAG_CPMI))
			sendPMInfo(QUIT);
		else
			closeCC(true, false);
	}
}

void PrivateChat::handleMessage(const ChatMessage& aMessage) {
	if (aMessage.replyTo->getHubUrl() != replyTo.hint) {
		fire(PrivateChatListener::StatusMessage(), STRING_F(MESSAGES_SENT_THROUGH_REMOTE, 
			ClientManager::getInstance()->getHubName(aMessage.replyTo->getHubUrl())), LogManager::LOG_INFO);

		setHubUrl(aMessage.replyTo->getHubUrl());
		fire(PrivateChatListener::UserUpdated());
	}

	fire(PrivateChatListener::PrivateMessage(), aMessage);
}

void PrivateChat::activate(const string& msg, Client* c) {
	fire(PrivateChatListener::Activate(), msg, c);
}

void PrivateChat::close() {
	fire(PrivateChatListener::Close());
}

void PrivateChat::startCC() {
	bool protocolError;
	if (!replyTo.user->isOnline() || state < DISCONNECTED) {
		return;
	}
	state = CONNECTING;
	lastCCPMError = Util::emptyString;

	auto token = ConnectionManager::getInstance()->tokens.getToken(CONNECTION_TYPE_PM);
	bool connecting = ClientManager::getInstance()->connect(replyTo.user, token, true, lastCCPMError, replyTo.hint, protocolError, CONNECTION_TYPE_PM);
	allowAutoCCPM = !protocolError;

	if (!connecting) {
		state = DISCONNECTED;
		if (!lastCCPMError.empty())
			fire(PrivateChatListener::PMStatus(), CCPM_ERROR);
	} else {
		fire(PrivateChatListener::PMStatus(), CCPM_ESTABLISHING);
		delayEvents.addEvent(CCPM_TIMEOUT, [this] { checkCCPMTimeout(); }, 30000); // 30 seconds, completely arbitrary amount of time.
	}
	
}

void PrivateChat::checkAlwaysCCPM() {
	if (!replyTo.user->isOnline() || !SETTING(ALWAYS_CCPM) || !getSupportsCCPM() || replyTo.user->isNMDC() || replyTo.user->isSet(User::BOT))
		return;

	if (allowAutoCCPM && state == DISCONNECTED) {
		startCC();
		allowAutoCCPM = allowAutoCCPM && ccpmAttempts++ < 3;
	}else if (ccReady()){
		allowAutoCCPM = true;
	}
}

void PrivateChat::checkCCPMTimeout() {
	if (state == CONNECTING) {
		fire(PrivateChatListener::PMStatus(), CCPM_CONNECTION_TIMEOUT);
		state = DISCONNECTED;
	} 
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

void PrivateChat::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept{
	if (aUser != replyTo.user)
		return;

	setSupportsCCPM(ClientManager::getInstance()->getSupportsCCPM(replyTo, lastCCPMError));
	if (wentOffline) {
		delayEvents.removeEvent(USER_UPDATE);
		closeCC(false, false);
		allowAutoCCPM = true;
		online = false;
		fire(PrivateChatListener::StatusMessage(), STRING(USER_WENT_OFFLINE), LogManager::LOG_INFO);
		fire(PrivateChatListener::UserUpdated());
	} else {
		delayEvents.addEvent(USER_UPDATE, [this] {
			checkUserHub(true);
			fire(PrivateChatListener::UserUpdated()); 
		}, 1000);
	}
}

void PrivateChat::checkUserHub(bool wentOffline) {
	auto hubs = ClientManager::getInstance()->getHubs(replyTo.user->getCID());

	dcassert(!hubs.empty());
	if (hubs.empty())
		return;

	if (find_if(hubs.begin(), hubs.end(), CompareFirst<string, string>(replyTo.hint)) == hubs.end()) {
		auto statusText = wentOffline ? STRING_F(USER_OFFLINE_PM_CHANGE, hubName % hubs[0].second) : 
			STRING_F(MESSAGES_SENT_THROUGH, hubs[0].second);

		fire(PrivateChatListener::StatusMessage(), statusText, LogManager::LOG_INFO);

		setHubUrl(hubs[0].first);
		hubName = hubs[0].second;
	}
}

void PrivateChat::setHubUrl(const string& hint) { 
	replyTo.hint = hint;
	hubName = ClientManager::getInstance()->getHubName(replyTo.hint);
}

void PrivateChat::on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept{
	if (aUser.getUser() != replyTo.user)
		return;

	setSupportsCCPM(getSupportsCCPM() || aUser.supportsCCPM(lastCCPMError));
	delayEvents.addEvent(USER_UPDATE, [this] {
		if (!online) {
			auto hubNames = ClientManager::getInstance()->getFormatedHubNames(replyTo);
			auto nicks = ClientManager::getInstance()->getFormatedNicks(replyTo);
			fire(PrivateChatListener::StatusMessage(), STRING(USER_WENT_ONLINE) + " [" + nicks + " - " + hubNames + "]", 
				LogManager::LOG_INFO);

			// online from a different hub?
			checkUserHub(false);
			online = true;
		}

		fire(PrivateChatListener::UserUpdated()); 
	}, 1000);

	delayEvents.addEvent(CCPM_AUTO, [this] { checkAlwaysCCPM(); }, 3000);
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
		fire(PrivateChatListener::PMStatus(), type);
}

void PrivateChat::logMessage(const string& aMessage) {
	if (SETTING(LOG_PRIVATE_CHAT)) {
		ParamMap params;
		params["message"] = aMessage;
		fillLogParams(params);
		LogManager::getInstance()->log(getUser(), params);
	}
}

void PrivateChat::fillLogParams(ParamMap& params) const {
	const CID& cid = getUser()->getCID();;
	params["hubNI"] = [&] { return Util::listToString(ClientManager::getInstance()->getHubNames(cid)); };
	params["hubURL"] = [&] { return getHubUrl(); };
	params["userCID"] = [&cid] { return cid.toBase32(); };
	params["userNI"] = [&] { return ClientManager::getInstance()->getNick(getUser(), getHubUrl()); };
	params["myCID"] = [] { return ClientManager::getInstance()->getMe()->getCID().toBase32(); };
}

string PrivateChat::getLogPath() const {
	ParamMap params;
	fillLogParams(params);
	return LogManager::getInstance()->getPath(getUser(), params);
}

}