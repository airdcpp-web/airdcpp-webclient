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

#include "ActivityManager.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "MessageManager.h"
#include "LogManager.h"

#include "AirUtil.h"
#include "Message.h"
#include "PrivateChat.h"
#include "Util.h"
#include "Wildcards.h"

#define CONFIG_DIR Util::PATH_USER_CONFIG
#define CONFIG_NAME "IgnoredUsers.xml"

namespace dcpp 
{

	MessageManager::MessageManager() noexcept : dirty(false) {
	SettingsManager::getInstance()->addListener(this);
	ConnectionManager::getInstance()->addListener(this);
}

MessageManager::~MessageManager() noexcept {
	SettingsManager::getInstance()->removeListener(this);
	ConnectionManager::getInstance()->removeListener(this);

	{
		WLock l(cs);
		chats.clear();
	}
	ConnectionManager::getInstance()->disconnect();
}

PrivateChatPtr MessageManager::addChat(const HintedUser& user, bool aReceivedMessage) noexcept {
	if (getChat(user.user)) {
		return nullptr;
	}

	PrivateChatPtr chat;

	{
		WLock l(cs);
		chat = std::make_shared<PrivateChat>(user, getPMConn(user.user));
		chats.emplace(user.user, chat);
	}

	fire(MessageManagerListener::ChatCreated(), chat, aReceivedMessage);
	return chat;
}

PrivateChatPtr MessageManager::getChat(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	auto i = chats.find(aUser);
	return i != chats.end() ? i->second : nullptr;
}

MessageManager::ChatMap MessageManager::getChats() const noexcept {
	RLock l(cs);
	return chats;
}

bool MessageManager::removeChat(const UserPtr& aUser) {
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

	fire(MessageManagerListener::ChatRemoved(), chat);
	return true;
}

void MessageManager::closeAll(bool Offline) {
	UserList toRemove;

	{
		RLock l(cs);
		for (auto i : chats) {
			if (Offline && i.first->isOnline())
				continue;

			toRemove.push_back(i.first);
		}
	}

	for (const auto& u : toRemove) {
		removeChat(u);
	}
}

//LOCK!!
UserConnection* MessageManager::getPMConn(const UserPtr& user) {
	auto i = ccpms.find(user);
	if (i != ccpms.end()) {
		auto uc = i->second;
		uc->removeListener(this);
		ccpms.erase(i);
		return uc;
	}
	return nullptr;
}


void MessageManager::DisconnectCCPM(const UserPtr& aUser) {
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

void MessageManager::onPrivateMessage(const ChatMessagePtr& aMessage) {
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

	auto c = aMessage->getFrom()->getClient();
	if (wndCnt > 200 || (!myPM && isIgnoredOrFiltered(aMessage, c.get(), true))) {
		DisconnectCCPM(user);
		return;
	}

	const auto& identity = aMessage->getReplyTo()->getIdentity();
	if ((identity.isBot() && !SETTING(POPUP_BOT_PMS)) || (identity.isHub() && !SETTING(POPUP_HUB_PMS))) {
		c->addLine(STRING(PRIVATE_MESSAGE_FROM) + " " + identity.getNick() + ": " + aMessage->format());
		return;
	}

	auto chat = addChat(HintedUser(user, aMessage->getReplyTo()->getClient()->getHubUrl()), true);
	chat->handleMessage(aMessage);

	if (ActivityManager::getInstance()->isAway() && !myPM && (!SETTING(NO_AWAYMSG_TO_BOTS) || !user->isSet(User::BOT))) {
		ParamMap params;
		aMessage->getFrom()->getIdentity().getParams(params, "user", false);

		string error;
		chat->sendMessage(ActivityManager::getInstance()->getAwayMessage(c->get(HubSettings::AwayMsg), params), error, false);
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
			getPMConn(cqi->getUser());
		}
	}
}

void MessageManager::on(UserConnectionListener::PrivateMessage, UserConnection*, const ChatMessagePtr& message) noexcept{
	onPrivateMessage(message);
}

void MessageManager::on(AdcCommand::PMI, UserConnection* uc, const AdcCommand& cmd) noexcept{
	if (cmd.hasFlag("QU", 0)) {
		RLock l(cs);
		auto i = ccpms.find(uc->getUser());
		if (i != ccpms.end())
			uc->disconnect(true);
	}
}

// SettingsManagerListener
void MessageManager::on(SettingsManagerListener::Load, SimpleXML& aXml) noexcept{
	load(aXml);
}

void MessageManager::on(SettingsManagerListener::Save, SimpleXML& aXml) noexcept{
	save(aXml);
}

MessageManager::UserSet MessageManager::getIgnoredUsers() const noexcept {
	RLock l(Ignorecs);
	return ignoredUsers;
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

bool MessageManager::isIgnoredOrFiltered(const ChatMessagePtr& msg, Client* aClient, bool PM){
	const auto& identity = msg->getFrom()->getIdentity();

	auto logIgnored = [&](bool filter) -> void {
		if (SETTING(LOG_IGNORED)) {
			string tmp;
			if (PM) {
				tmp = filter ? STRING(PM_MESSAGE_FILTERED) : STRING(PM_MESSAGE_IGNORED);
			}
			else {
				string hub = "[" + ((aClient && !aClient->getHubName().empty()) ?
					(aClient->getHubName().size() > 50 ? (aClient->getHubName().substr(0, 50) + "...") : aClient->getHubName()) : aClient->getHubUrl()) + "] ";
				tmp = (filter ? STRING(MC_MESSAGE_FILTERED) : STRING(MC_MESSAGE_IGNORED)) + hub;
			}
			tmp += "<" + identity.getNick() + "> " + msg->getText();
			LogManager::getInstance()->message(tmp, LogMessage::SEV_INFO);
		}
	};

	if (msg->getFrom()->getUser()->isIgnored() && ((aClient && aClient->isOp()) || !identity.isOp() || identity.isBot())) {
		logIgnored(false);
		return true;
	}

	if (isChatFiltered(identity.getNick(), msg->getText(), PM ? ChatFilterItem::PM : ChatFilterItem::MC)) {
		logIgnored(true);
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
	aXml.resetCurrentChild();
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
		LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, CONFIG_NAME % e.getError()), LogMessage::SEV_ERROR);
	}
}

}