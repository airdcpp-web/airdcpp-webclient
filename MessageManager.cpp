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
#include "LogManager.h"

#include "ChatMessage.h"
#include "Util.h"
#include "Wildcards.h"

#define CONFIG_DIR Util::PATH_USER_CONFIG
#define CONFIG_NAME "IgnoredUsers.xml"

namespace dcpp 
{

	MessageManager::MessageManager() noexcept : dirty(false) {
	SettingsManager::getInstance()->addListener(this);
	ConnectionManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);
}

MessageManager::~MessageManager() noexcept {
	SettingsManager::getInstance()->removeListener(this);
	ConnectionManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);

	{
		WLock l(cs);
		chats.clear();
	}
	ConnectionManager::getInstance()->disconnect();
}

PrivateChat* MessageManager::addChat(const HintedUser& user) {
	WLock l(cs);
	auto p = new PrivateChat(user);
	chats.emplace(user.user, p).first->second;
	p->setUc(getPMConn(user.user, p));
	return p;
	
}

PrivateChat* MessageManager::getChat(const UserPtr& aUser) {
	RLock l(cs);
	auto i = chats.find(aUser);
	return i != chats.end() ? i->second : nullptr;
}

void MessageManager::removeChat(const UserPtr& aUser) {
	WLock l(cs);
	auto i = chats.find(aUser);
	i->second->Disconnect();
	delete i->second; //TODO: use smart pointers
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
		auto uc = getPMConn(user, i->second);
		if (uc)
			i->second->setUc(uc);
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

// SettingsManagerListener
void MessageManager::on(SettingsManagerListener::Load, SimpleXML& aXml) noexcept{
	load(aXml);
}

void MessageManager::on(SettingsManagerListener::Save, SimpleXML& aXml) noexcept{
	save(aXml);
}

void MessageManager::storeIgnore(const UserPtr& aUser) {
	{
		WLock l(Ignorecs);
		ignoredUsers.emplace(aUser);
	}
	aUser->setFlag(User::IGNORED);
	dirty = true;
	fire(MessageManagerListener::IgnoreAdded(), aUser);
}

void MessageManager::removeIgnore(const UserPtr& aUser) {
	{
		WLock l(Ignorecs);
		auto i = ignoredUsers.find(aUser);
		if (i != ignoredUsers.end())
			ignoredUsers.erase(i);
	}
	aUser->unsetFlag(User::IGNORED);
	dirty = true;
	fire(MessageManagerListener::IgnoreRemoved(), aUser);
}

bool MessageManager::isIgnored(const UserPtr& aUser) {
	RLock l(Ignorecs);
	auto i = ignoredUsers.find(aUser);
	return (i != ignoredUsers.end());
}

bool MessageManager::isIgnoredOrFiltered(const ChatMessage& msg, Client* client, bool PM){
	const auto& identity = msg.from->getIdentity();

	auto logIgnored = [&](bool filter) -> void {
		if (SETTING(LOG_IGNORED)) {
			string tmp;
			if (PM) {
				tmp = filter ? STRING(PM_MESSAGE_FILTERED) : STRING(PM_MESSAGE_IGNORED);
			}
			else {
				string hub = "[" + ((client && !client->getHubName().empty()) ?
					(client->getHubName().size() > 50 ? (client->getHubName().substr(0, 50) + "...") : client->getHubName()) : client->getHubUrl()) + "] ";
				tmp = (filter ? STRING(MC_MESSAGE_FILTERED) : STRING(MC_MESSAGE_IGNORED)) + hub;
			}
			tmp += "<" + identity.getNick() + "> " + msg.text;
			LogManager::getInstance()->message(tmp, LogManager::LOG_INFO);
		}
	};

	if (PM && client) {
		// don't be that restrictive with the fav hub option
		if (client->getFavNoPM() && (client->isOp() || !msg.replyTo->getIdentity().isOp()) && !msg.replyTo->getIdentity().isBot() && !msg.replyTo->getUser()->isFavorite()) {
			string tmp;
			client->privateMessage(msg.replyTo, "Private messages sent via this hub are ignored", tmp);
			DisconnectCCPM(msg.from->getUser());
			return true;
		}
	}

	if (msg.from->getUser()->isIgnored() && ((client && client->isOp()) || !identity.isOp() || identity.isBot())) {
		logIgnored(false);
		if (PM)
			DisconnectCCPM(msg.from->getUser());
		return true;
	}

	if (isChatFiltered(identity.getNick(), msg.text, PM ? ChatFilterItem::PM : ChatFilterItem::MC)) {
		logIgnored(true);
		if (PM)
			DisconnectCCPM(msg.from->getUser());
		return true;
	}

	return false;
}

bool MessageManager::isChatFiltered(const string& aNick, const string& aText, ChatFilterItem::Context aContext) {
	RLock l(Ignorecs);
	for (auto& i : ChatFilterItems) {
		if (i.match(aNick, aText, aContext))
			return true;
	}
	return false;
}
void MessageManager::load(SimpleXML& aXml) {

	if (aXml.findChild("ChatFilterItems")) {
		aXml.stepIn();
		while (aXml.findChild("ChatFilterItem")) {
			WLock l(Ignorecs);
			ChatFilterItems.push_back(ChatFilterItem(aXml.getChildAttrib("Nick"), aXml.getChildAttrib("Text"),
				(StringMatch::Method)aXml.getIntChildAttrib("NickMethod"), (StringMatch::Method)aXml.getIntChildAttrib("TextMethod"),
				aXml.getBoolChildAttrib("MC"), aXml.getBoolChildAttrib("PM"), aXml.getBoolChildAttrib("Enabled")));
		}
		aXml.stepOut();
	}
	loadUsers();
}

void MessageManager::save(SimpleXML& aXml) {
	aXml.addTag("ChatFilterItems");
	aXml.stepIn();
	{
		RLock l(Ignorecs);
		for (const auto& i : ChatFilterItems) {
			aXml.addTag("ChatFilterItem");
			aXml.addChildAttrib("Nick", i.getNickPattern());
			aXml.addChildAttrib("NickMethod", i.getNickMethod());
			aXml.addChildAttrib("Text", i.getTextPattern());
			aXml.addChildAttrib("TextMethod", i.getTextMethod());
			aXml.addChildAttrib("MC", i.matchMainchat);
			aXml.addChildAttrib("PM", i.matchPM);
			aXml.addChildAttrib("Enabled", i.getEnabled());
		}
	}
	aXml.stepOut();

	if (dirty)
		saveUsers();
}

void MessageManager::saveUsers() {
	SimpleXML xml;

	xml.addTag("Ignored");
	xml.stepIn();

	xml.addTag("Users");
	xml.stepIn();

	//TODO: cache this information?
	{
		RLock l(Ignorecs);
		for (const auto& u : ignoredUsers) {
			xml.addTag("User");
			xml.addChildAttrib("CID", u->getCID().toBase32());
			auto ou = ClientManager::getInstance()->findOnlineUser(u->getCID(), "");
			if (ou) {
				xml.addChildAttrib("Nick", ou->getIdentity().getNick());
				xml.addChildAttrib("Hub", ou->getHubUrl());
				xml.addChildAttrib("LastSeen", GET_TIME());
			}
			else {
				auto ofu = ClientManager::getInstance()->getOfflineUser(u->getCID());
				xml.addChildAttrib("Nick", ofu ? ofu->getNick() : "");
				xml.addChildAttrib("Hub", ofu ? ofu->getUrl() : "");
				xml.addChildAttrib("LastSeen", ofu ? ofu->getLastSeen() : GET_TIME());
			}
		}
	}
	xml.stepOut();
	xml.stepOut();

	SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_NAME);
}

void MessageManager::loadUsers() {
	try {
		SimpleXML xml;
		SettingsManager::loadSettingFile(xml, CONFIG_DIR, CONFIG_NAME);
		auto cm = ClientManager::getInstance();
		if (xml.findChild("Ignored")) {
			xml.stepIn();
			xml.resetCurrentChild();
			if (xml.findChild("Users")) {
				xml.stepIn();
				while (xml.findChild("User")) {
					UserPtr user = cm->getUser(CID(xml.getChildAttrib("CID")));
					{
						WLock(cm->getCS());
						cm->addOfflineUser(user, xml.getChildAttrib("Nick"), xml.getChildAttrib("Hub"), (uint32_t)xml.getIntChildAttrib("LastSeen"));
					}
					WLock l(Ignorecs);
					ignoredUsers.emplace(user);
					user->setFlag(User::IGNORED);
				}
				xml.stepOut();
			}
			xml.stepOut();
		}
	}
	catch (const Exception& e) {
		LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, CONFIG_NAME % e.getError()), LogManager::LOG_ERROR);
	}
}

}