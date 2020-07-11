/*
* Copyright (C) 2011-2019 AirDC++ Project
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
#include "PrivateChatManager.h"

#include "ActivityManager.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "IgnoreManager.h"

#include "AirUtil.h"
#include "Message.h"
#include "PrivateChat.h"
#include "Util.h"

#define CONFIG_DIR Util::PATH_USER_CONFIG
#define CONFIG_NAME "IgnoredUsers.xml"

namespace dcpp 
{

PrivateChatManager::PrivateChatManager() noexcept {
	ClientManager::getInstance()->addListener(this);
	ConnectionManager::getInstance()->addListener(this);
}

PrivateChatManager::~PrivateChatManager() noexcept {
	ConnectionManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);

	{
		WLock l(cs);
		chats.clear();
	}

	ConnectionManager::getInstance()->disconnect();
}

pair<PrivateChatPtr, bool> PrivateChatManager::addChat(const HintedUser& aUser, bool aReceivedMessage) noexcept {
	PrivateChatPtr chat;

	auto user = ClientManager::getInstance()->checkOnlineUrl(aUser);

	{
		WLock l(cs);
		auto res = chats.emplace(user.user, std::make_shared<PrivateChat>(user, getPMConn(user.user)));
		chat = res.first->second;
		if (!res.second) {
			return { chat, false };
		}
	}

	fire(PrivateChatManagerListener::ChatCreated(), chat, aReceivedMessage);
	return { chat, true };
}

PrivateChatPtr PrivateChatManager::getChat(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	auto i = chats.find(aUser);
	return i != chats.end() ? i->second : nullptr;
}

PrivateChatManager::ChatMap PrivateChatManager::getChats() const noexcept {
	RLock l(cs);
	return chats;
}

bool PrivateChatManager::removeChat(const UserPtr& aUser) {
	PrivateChatPtr chat;

	{
		WLock l(cs);
		auto i = chats.find(aUser);
		if (i == chats.end()) {
			return false;
		}

		chat = i->second;

		chat->close();
		auto uc = chat->getUc();
		if (uc) {
			//Closed the window, keep listening to the connection until QUIT is received with CPMI;
			ccpms[aUser] = uc;
			uc->addListener(this);
		}

		chats.erase(i);
	}

	fire(PrivateChatManagerListener::ChatRemoved(), chat);
	return true;
}

void PrivateChatManager::closeAll(bool aOfflineOnly) {
	UserList toRemove;

	{
		RLock l(cs);
		for (const auto& i : chats) {
			if (aOfflineOnly && i.first->isOnline())
				continue;

			toRemove.push_back(i.first);
		}
	}

	for (const auto& u : toRemove) {
		removeChat(u);
	}
}

//LOCK!!
UserConnection* PrivateChatManager::getPMConn(const UserPtr& user) {
	auto i = ccpms.find(user);
	if (i != ccpms.end()) {
		auto uc = i->second;
		uc->removeListener(this);
		ccpms.erase(i);
		return uc;
	}
	return nullptr;
}


void PrivateChatManager::DisconnectCCPM(const UserPtr& aUser) {
	{
		RLock l(cs);
		auto i = chats.find(aUser);
		if (i != chats.end()) {
			i->second->closeCC(true, true);
			return;
		}
	}

	WLock l(cs);
	auto uc = getPMConn(aUser);
	if (uc)
		uc->disconnect(true);

}

void PrivateChatManager::onPrivateMessage(const ChatMessagePtr& aMessage) {
	bool myPM = aMessage->getReplyTo()->getUser() == ClientManager::getInstance()->getMe();
	const UserPtr& user = myPM ? aMessage->getTo()->getUser() : aMessage->getReplyTo()->getUser();
	size_t wndCnt;
	{
		WLock l(cs);
		wndCnt = chats.size();
		auto i = chats.find(user);
		if (i != chats.end()) {
			i->second->handleMessage(aMessage); //We should have a listener in the frame
			return;
		}
	}

	if (wndCnt > 200) {
		DisconnectCCPM(user);
		return;
	}


	const auto client = aMessage->getFrom()->getClient();
	const auto& identity = aMessage->getReplyTo()->getIdentity();
	if ((identity.isBot() && !SETTING(POPUP_BOT_PMS)) || (identity.isHub() && !SETTING(POPUP_HUB_PMS))) {
		client->addLine(STRING(PRIVATE_MESSAGE_FROM) + " " + identity.getNick() + ": " + aMessage->format());
		return;
	}

	auto chat = addChat(HintedUser(user, client->getHubUrl()), true).first;
	chat->handleMessage(aMessage);

	if (ActivityManager::getInstance()->isAway() && !myPM && (!SETTING(NO_AWAYMSG_TO_BOTS) || !user->isSet(User::BOT))) {
		ParamMap params;
		aMessage->getFrom()->getIdentity().getParams(params, "user", false);

		string error;
		const auto message = ActivityManager::getInstance()->getAwayMessage(client->get(HubSettings::AwayMsg), params);
		chat->sendMessageHooked(OutgoingChatMessage(message, nullptr, false), error);
	}
}

void PrivateChatManager::on(ConnectionManagerListener::Connected, const ConnectionQueueItem* cqi, UserConnection* uc) noexcept{
	if (cqi->getConnType() == CONNECTION_TYPE_PM) {
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

void PrivateChatManager::on(ConnectionManagerListener::Removed, const ConnectionQueueItem* cqi) noexcept{
	if (cqi->getConnType() == CONNECTION_TYPE_PM) {
		{
			WLock l(cs);
			auto i = chats.find(cqi->getUser());
			if (i != chats.end()) {
				i->second->CCPMDisconnected();
			}
			getPMConn(cqi->getUser());
		}
	}
}

void PrivateChatManager::on(ClientManagerListener::PrivateMessage, const ChatMessagePtr& aMessage) noexcept {
	onPrivateMessage(aMessage);
}

void PrivateChatManager::on(UserConnectionListener::PrivateMessage, UserConnection*, const ChatMessagePtr& aMessage) noexcept{
	onPrivateMessage(aMessage);
}

void PrivateChatManager::on(AdcCommand::PMI, UserConnection* uc, const AdcCommand& cmd) noexcept{
	if (cmd.hasFlag("QU", 0)) {
		RLock l(cs);
		auto i = ccpms.find(uc->getUser());
		if (i != ccpms.end())
			uc->disconnect(true);
	}
}

}