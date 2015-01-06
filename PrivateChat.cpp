/*
* Copyright (C) 2011-2014 AirDC++ Project
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
#include "LogManager.h"
#include "ConnectionManager.h"

namespace dcpp {

void PrivateChat::CCPMConnected(UserConnection* uc) {
		setUc(uc);
		uc->addListener(this);
		fire(PrivateChatListener::CCPMStatusChanged(), STRING(CCPM_ESTABLISHED));
}

void PrivateChat::CCPMDisconnected() {
	if (uc) {
		uc->removeListener(this);
		setUc(nullptr);
		fire(PrivateChatListener::CCPMStatusChanged(), STRING(CCPM_DISCONNECTED));
		checkAlwaysCCPM();
	}
}

bool PrivateChat::sendPrivateMessage(const HintedUser& aUser, const string& msg, string& error_, bool thirdPerson) {
	if (uc) {
		uc->pm(msg, thirdPerson);
		return true;
	}

	return ClientManager::getInstance()->privateMessage(aUser, msg, error_, thirdPerson);
}

void PrivateChat::Disconnect() {
	if (uc) {
		uc->removeListener(this);
		uc->disconnect(true);
		setUc(nullptr);
	}
}

void PrivateChat::UserDisconnected(bool wentOffline) {
	setSupportsCCPM(ClientManager::getInstance()->getSupportsCCPM(replyTo, lastCCPMError));
	if (wentOffline) {
		Disconnect();
		allowAutoCCPM = true;
	}
	fire(PrivateChatListener::UserUpdated(), wentOffline);
}

void PrivateChat::UserUpdated(const OnlineUser& aUser) {
	setSupportsCCPM(getSupportsCCPM() || aUser.supportsCCPM(lastCCPMError));
	fire(PrivateChatListener::UserUpdated(), false);

	//checkAlwaysCCPM(); This needs to be called Async!! Maybe add timer for checking ccpm reconnect?
}

void PrivateChat::Message(const ChatMessage& aMessage) {
	fire(PrivateChatListener::PrivateMessage(), aMessage);
}

void PrivateChat::Activate(const HintedUser& replyTo, const string& msg, Client* c) {
	fire(PrivateChatListener::Activate(), replyTo, msg, c);
}

void PrivateChat::Close() {
	fire(PrivateChatListener::Close());
}

void PrivateChat::StartCCPM(HintedUser& aUser, string& _err, bool& allowAuto){

	if (!aUser.user->isOnline() || getUc()) {
		return;
	}

	auto token = ConnectionManager::getInstance()->tokens.getToken(CONNECTION_TYPE_PM);
	if (ClientManager::getInstance()->connect(aUser.user, token, true, _err, aUser.hint, allowAuto, CONNECTION_TYPE_PM))
		fire(PrivateChatListener::StatusMessage(), STRING(CCPM_ESTABLISHING), LogManager::LOG_INFO);
	else if (!_err.empty()) {
		fire(PrivateChatListener::StatusMessage(), _err, LogManager::LOG_ERROR);
	}

}

void PrivateChat::StartCC() {
	bool protocolError;
	StartCCPM(replyTo, lastCCPMError, protocolError);
	allowAutoCCPM = !protocolError;
}

void PrivateChat::checkAlwaysCCPM() {
	if (!replyTo.user->isOnline() || !SETTING(ALWAYS_CCPM) || !getSupportsCCPM() || replyTo.user->isNMDC() || replyTo.user->isSet(User::BOT))
		return;

	if (allowAutoCCPM && !getUc()) {
		StartCC();
		allowAutoCCPM = allowAutoCCPM && ccpmAttempts++ < 3;
	}else if (getUc()){
		allowAutoCCPM = true;
	}
}


}