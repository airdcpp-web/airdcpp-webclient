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

#include "MessageManager.h"
#include "IgnoreManager.h"
#include "LogManager.h"

#include "ChatMessage.h"


namespace dcpp 
{

MessageManager::MessageManager() noexcept{
	ConnectionManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);
}

MessageManager::~MessageManager() noexcept {
	ConnectionManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);

	{
		WLock l(cs);
		chats.clear();
	}
	ConnectionManager::getInstance()->disconnect();
}

PrivateChat* MessageManager::getChat(const HintedUser& user) {
	WLock l(cs);
	auto i = chats.find(user.user);
	if (i != chats.end()) {
		return i->second;
	} else {
		auto p = new PrivateChat(user);
		chats.emplace(user.user, p).first->second;
		p->setUc(getPMConn(user.user, p));
		return p;
	}
}

bool MessageManager::hasWindow(const UserPtr& aUser) {
	RLock l(cs);
	return chats.find(aUser) != chats.end();
}

void MessageManager::closeWindow(const UserPtr& aUser) {
	WLock l(cs);
	auto i = chats.find(aUser);
	i->second->Disconnect();
	delete i->second;
	chats.erase(i);
}

void MessageManager::closeAll(bool Offline) {

	for (auto i : chats) {
		if (Offline && i.first->isOnline())
			continue;
		i.second->Close();
	}
}

//LOCK!!
UserConnection* MessageManager::getPMConn(const UserPtr& user, UserConnectionListener* listener) {
	auto i = ccpms.find(user);
	if (i != ccpms.end()) {
		auto uc = i->second;
		ccpms.erase(i);
		uc->addListener(listener);
		uc->removeListener(this);
		return uc;
	}
	return nullptr;
}

bool MessageManager::isIgnoredOrFiltered(const ChatMessage& msg, Client* client, bool PM){
	if (IgnoreManager::getInstance()->isIgnoredOrFiltered(msg, client, PM)) {
		DisconnectCCPM(msg.from->getUser());
		return true;
	}
	return false;
}

void MessageManager::DisconnectCCPM(const UserPtr& aUser) {
	
	RLock l(cs);
	auto i = chats.find(aUser);
	if (i != chats.end()) {
		i->second->Disconnect();
	}
}

void MessageManager::onPrivateMessage(const ChatMessage& aMessage) {
	bool myPM = aMessage.replyTo->getUser() == ClientManager::getInstance()->getMe();
	const UserPtr& user = myPM ? aMessage.to->getUser() : aMessage.replyTo->getUser();
	RLock l(cs);
	auto i = chats.find(user);
	if (i != chats.end()) {
		i->second->setUc(getPMConn(user, i->second));
		i->second->Message(aMessage); //We should have a listener in the frame
	} else {
		Client* c = &aMessage.from->getClient();
		if (chats.size() > 200 || !myPM && isIgnoredOrFiltered(aMessage, c, true)) 
			return;

		const auto& identity = aMessage.replyTo->getIdentity();
		if ((identity.isBot() && !SETTING(POPUP_BOT_PMS)) || (identity.isHub() && !SETTING(POPUP_HUB_PMS))) {
			c->Message(STRING(PRIVATE_MESSAGE_FROM) + " " + identity.getNick() + ": " + aMessage.format());
			return;
		}
		//This will result in creating a new window
		fire(MessageManagerListener::PrivateMessage(), aMessage);
	}
}

void MessageManager::on(ConnectionManagerListener::Connected, const ConnectionQueueItem* cqi, UserConnection* uc) noexcept{
	if (cqi->getConnType() == CONNECTION_TYPE_PM) {

		{
			WLock l(cs);
			auto i = chats.find(cqi->getUser());
			if (i != chats.end()) {
				i->second->CCPMConnected(uc);
			} else {
				// until a message is received, no need to open a PM window.
				ccpms[cqi->getUser()] = uc;
				uc->addListener(this);
			}
		}
	}
}

void MessageManager::on(ConnectionManagerListener::Removed, const ConnectionQueueItem* cqi) noexcept{
	if (cqi->getConnType() == CONNECTION_TYPE_PM) {
		{
			WLock l(cs);
			auto i = chats.find(cqi->getUser());
			if (i != chats.end()) {
				i->second->CCPMDisconnected();
			}
			ccpms.erase(cqi->getUser());
		}
	}
}

void MessageManager::on(UserConnectionListener::PrivateMessage, UserConnection*, const ChatMessage& message) noexcept{
	onPrivateMessage(message);

}

void MessageManager::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept{
	RLock l(cs);
	auto i = chats.find(aUser);
	if (i != chats.end()) {
		i->second->UserDisconnected(wentOffline);
	}
}

void MessageManager::on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept{
	RLock l(cs);
	auto i = chats.find(aUser.getUser());
	if (i != chats.end()) {
		i->second->UserUpdated(aUser);
	}
}

}