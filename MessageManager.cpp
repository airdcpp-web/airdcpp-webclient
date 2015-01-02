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

#include "ClientManager.h"
#include "MessageManager.h"
#include "IgnoreManager.h"
#include "LogManager.h"

#include "ChatMessage.h"


namespace dcpp 
{

MessageManager::MessageManager() noexcept{
	ConnectionManager::getInstance()->addListener(this);
}

MessageManager::~MessageManager() noexcept {
	ConnectionManager::getInstance()->removeListener(this);

	{
		WLock l(ccpmMutex);
		ccpms.clear();
	}
	ConnectionManager::getInstance()->disconnect();
}

bool MessageManager::hasCCPMConn(const UserPtr& user) {
	RLock l(ccpmMutex);
	return ccpms.find(user) != ccpms.end();
}

bool MessageManager::sendPrivateMessage(const HintedUser& aUser, const tstring& msg, string& error_, bool thirdPerson) {
	auto msg8 = Text::fromT(msg);

	{
		RLock l(ccpmMutex);
		auto i = ccpms.find(aUser);
		if (i != ccpms.end()) {
			auto uc = i->second;
			if (uc) {
				uc->pm(msg8, thirdPerson);
				return true;
			}
		}
	}
	return ClientManager::getInstance()->privateMessage(aUser, msg8, error_, thirdPerson);

}

bool MessageManager::StartCCPM(HintedUser& aUser, string& _err, bool& allowAuto){

	if (!aUser.user->isOnline()) {
		return false;
	}

	auto token = ConnectionManager::getInstance()->tokens.getToken(CONNECTION_TYPE_PM);
	return ClientManager::getInstance()->connect(aUser.user, token, true, _err, aUser.hint, allowAuto, CONNECTION_TYPE_PM);

}

bool MessageManager::isIgnoredOrFiltered(const ChatMessage& msg, Client* client, bool PM){
	if (IgnoreManager::getInstance()->isIgnoredOrFiltered(msg, client, PM)) {
		DisconnectCCPM(msg.from->getUser());
		return true;
	}
	return false;
}

void MessageManager::DisconnectCCPM(const UserPtr& aUser) {
	WLock l(ccpmMutex);
	auto i = ccpms.find(aUser);
	if (i != ccpms.end()) {
		auto uc = i->second;
		uc->removeListener(this);
		uc->disconnect(true);
		ccpms.erase(i);
	}
}


void MessageManager::on(ConnectionManagerListener::Connected, const ConnectionQueueItem* cqi, UserConnection* uc) noexcept{
		if (cqi->getConnType() == CONNECTION_TYPE_PM) {

			{
				// until a message is received, no need to open a PM window.
				WLock l(ccpmMutex);
				ccpms[cqi->getUser()] = uc;
				uc->addListener(this);
			}

			fire(MessageManagerListener::StatusMessage(), cqi->getUser(),STRING(CCPM_ESTABLISHED), LogManager::LOG_INFO);
		}
	}

void MessageManager::on(ConnectionManagerListener::Removed, const ConnectionQueueItem* cqi) noexcept{
	if (cqi->getConnType() == CONNECTION_TYPE_PM) {
		{
			WLock l(ccpmMutex);
			ccpms.erase(cqi->getUser());
		}
		fire(MessageManagerListener::StatusMessage(), cqi->getUser(), STRING(CCPM_DISCONNECTED), LogManager::LOG_INFO);
	}
}

void MessageManager::on(UserConnectionListener::PrivateMessage, UserConnection* uc, const ChatMessage& message) noexcept{
	fire(MessageManagerListener::PrivateMessage(), message);
}

}