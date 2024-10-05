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

#include <airdcpp/user/ignore/IgnoreManager.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/favorites/FavoriteUserManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/private_chat/PrivateChatManager.h>
#include <airdcpp/settings/SettingsManager.h>

#include <airdcpp/message/Message.h>
#include <airdcpp/util/Util.h>

#define CONFIG_DIR AppUtil::PATH_USER_CONFIG
#define CONFIG_NAME "IgnoredUsers.xml"
#define IGNORE_HOOK_ID "chat_ignore"

namespace dcpp {

IgnoreManager::IgnoreManager() noexcept {
	SettingsManager::getInstance()->addListener(this);

	ClientManager::getInstance()->incomingPrivateMessageHook.addSubscriber(ActionHookSubscriber(IGNORE_HOOK_ID, STRING(SETTINGS_IGNORE), nullptr), HOOK_HANDLER(IgnoreManager::onPrivateMessage));
	ClientManager::getInstance()->incomingHubMessageHook.addSubscriber(ActionHookSubscriber(IGNORE_HOOK_ID, STRING(SETTINGS_IGNORE), nullptr), HOOK_HANDLER(IgnoreManager::onHubMessage));
}

IgnoreManager::~IgnoreManager() noexcept {
	SettingsManager::getInstance()->removeListener(this);
}

ActionHookResult<MessageHighlightList> IgnoreManager::onPrivateMessage(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept {
	return isIgnoredOrFiltered(aMessage, aResultGetter, true);
}

ActionHookResult<MessageHighlightList> IgnoreManager::onHubMessage(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept {
	return isIgnoredOrFiltered(aMessage, aResultGetter, false);
}

// SettingsManagerListener
void IgnoreManager::on(SettingsManagerListener::Load, SimpleXML& aXml) noexcept {
	aXml.resetCurrentChild();
	if (aXml.findChild("ChatFilterItems")) {
		aXml.stepIn();
		while (aXml.findChild("ChatFilterItem")) {
			WLock l(cs);
			ChatFilterItems.push_back(ChatFilterItem(aXml.getChildAttrib("Nick"), aXml.getChildAttrib("Text"),
				(StringMatch::Method)aXml.getIntChildAttrib("NickMethod"), (StringMatch::Method)aXml.getIntChildAttrib("TextMethod"),
				aXml.getBoolChildAttrib("MC"), aXml.getBoolChildAttrib("PM"), aXml.getBoolChildAttrib("Enabled")));
		}
		aXml.stepOut();
	}
}

void IgnoreManager::on(SettingsManagerListener::Save, SimpleXML& aXml) noexcept {
	aXml.addTag("ChatFilterItems");
	aXml.stepIn();
	{
		RLock l(cs);
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
		save();
}

IgnoreManager::IgnoreMap IgnoreManager::getIgnoredUsers() const noexcept {
	RLock l(cs);
	return ignoredUsers;
}

bool IgnoreManager::storeIgnore(const UserPtr& aUser) noexcept {
	if (aUser->isIgnored()) {
		return false;
	}

	{
		WLock l(cs);
		ignoredUsers.emplace(aUser, 0);
	}

	aUser->setFlag(User::IGNORED);
	dirty = true;

	fire(IgnoreManagerListener::IgnoreAdded(), aUser);

	{
		auto chat = PrivateChatManager::getInstance()->getChat(aUser);
		if (chat) {
			chat->checkIgnored();
		}
	}

	ClientManager::getInstance()->userUpdated(aUser);
	return true;
}

bool IgnoreManager::removeIgnore(const UserPtr& aUser) noexcept {
	{
		WLock l(cs);
		auto i = ignoredUsers.find(aUser);
		if (i == ignoredUsers.end()) {
			return false;
		}

		ignoredUsers.erase(i);
	}

	aUser->unsetFlag(User::IGNORED);
	dirty = true;

	fire(IgnoreManagerListener::IgnoreRemoved(), aUser);
	ClientManager::getInstance()->userUpdated(aUser);
	return true;
}

bool IgnoreManager::checkIgnored(const OnlineUserPtr& aUser, bool aPM) noexcept {
	if (!aUser) {
		return false;
	}

	if (aPM && PrivateChatManager::getInstance()->getChat(aUser->getUser())) {
		return false;
	}

	RLock l(cs);
	auto i = ignoredUsers.find(aUser->getUser());
	if (i == ignoredUsers.end()) {
		return false;
	}

	i->second++;
	return true;
}

ActionHookResult<MessageHighlightList> IgnoreManager::isIgnoredOrFiltered(const ChatMessagePtr& msg, const ActionHookResultGetter<MessageHighlightList>& aResultGetter, bool aPM) noexcept {
	const auto& fromIdentity = msg->getFrom()->getIdentity();

	//Don't filter own messages
	if (msg->getFrom()->getUser() == ClientManager::getInstance()->getMe())
		return { nullptr, nullptr };

	auto logIgnored = [&](bool filter) {
		if (SETTING(LOG_IGNORED)) {
			string tmp;
			if (aPM) {
				tmp = filter ? STRING(PM_MESSAGE_FILTERED) : STRING(PM_MESSAGE_IGNORED);
			} else {
				tmp = (filter ? STRING(MC_MESSAGE_FILTERED) : STRING(MC_MESSAGE_IGNORED));
			}
			tmp += "<" + fromIdentity.getNick() + "> " + msg->getText();
			LogManager::getInstance()->message(tmp, LogMessage::SEV_INFO, STRING(SETTINGS_CHATFILTER));
		}
	};

	// replyTo can be different if the message is received via a chat room (it should be possible to ignore those as well)
	if (checkIgnored(msg->getFrom(), aPM) || checkIgnored(msg->getReplyTo(), aPM)) {
		return aResultGetter.getRejection("user_ignored", "User ignored");
	}

	if (isChatFiltered(fromIdentity.getNick(), msg->getText(), aPM ? ChatFilterItem::PM : ChatFilterItem::MC)) {
		logIgnored(true);
		return aResultGetter.getRejection("message_filtered", "Message filtered");
	}

	return { nullptr, nullptr };
}

bool IgnoreManager::isChatFiltered(const string& aNick, const string& aText, ChatFilterItem::Context aContext) const noexcept {
	RLock l(cs);
	for (const auto& i : ChatFilterItems) {
		if (i.match(aNick, aText, aContext))
			return true;
	}
	return false;
}

void IgnoreManager::save() {
	SimpleXML xml;

	xml.addTag("Ignored");
	xml.stepIn();

	xml.addTag("Users");
	xml.stepIn();

	{
		RLock l(cs);
		for (const auto& [user, ignoreCount] : ignoredUsers) {
			xml.addTag("User");
			xml.addChildAttrib("CID", user->getCID().toBase32());
			xml.addChildAttrib("IgnoredMessages", ignoreCount);

			FavoriteUserManager::getInstance()->addSavedUser(user);
		}
	}

	xml.stepOut();
	xml.stepOut();

	SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_NAME);
}

void IgnoreManager::load() {
	SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_NAME, [this](SimpleXML& xml) {
		if (xml.findChild("Ignored")) {
			xml.stepIn();
			xml.resetCurrentChild();
			if (xml.findChild("Users")) {
				xml.stepIn();
				while (xml.findChild("User")) {
					auto user = ClientManager::getInstance()->loadUser(xml.getChildAttrib("CID"), xml.getChildAttrib("Nick"), xml.getChildAttrib("Hub"));
					if (!user)
						continue;

					WLock l(cs);
					ignoredUsers.emplace(user, xml.getIntChildAttrib("IgnoredMessages"));
					user->setFlag(User::IGNORED);
				}
				xml.stepOut();
			}
			xml.stepOut();
		}
	});
}

}