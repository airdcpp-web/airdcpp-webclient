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

#include <web-server/stdinc.h>

#include <api/common/Serializer.h>
#include <api/common/Format.h>

#include <api/HubInfo.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/Bundle.h>
#include <airdcpp/Client.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/Message.h>
#include <airdcpp/DirectoryListing.h>
#include <airdcpp/GeoManager.h>
#include <airdcpp/OnlineUser.h>
#include <airdcpp/QueueItem.h>
#include <airdcpp/QueueManager.h>
#include <airdcpp/SearchManager.h>

namespace webserver {
	StringSet Serializer::getUserFlags(const UserPtr& aUser) noexcept {
		StringSet ret;
		if (aUser->isSet(User::BOT)) {
			ret.insert("bot");
		}

		if (aUser->isSet(User::FAVORITE)) {
			ret.insert("favorite");
		}

		if (aUser->isSet(User::IGNORED)) {
			ret.insert("ignored");
		}

		if (aUser == ClientManager::getInstance()->getMe()) {
			ret.insert("me");
		}

		if (aUser->isSet(User::NMDC)) {
			ret.insert("nmdc");
		}

		if (!aUser->isOnline()) {
			ret.insert("offline");
		}

		return ret;
	}

	StringSet Serializer::getOnlineUserFlags(const OnlineUserPtr& aUser) noexcept {
		auto flags = getUserFlags(aUser->getUser());
		appendOnlineUserFlags(aUser, flags);
		return flags;
	}

	void Serializer::appendOnlineUserFlags(const OnlineUserPtr& aUser, StringSet& flags_) noexcept {
		if (aUser->getIdentity().isAway()) {
			flags_.insert("away");
		}

		if (aUser->getIdentity().isOp()) {
			flags_.insert("op");
		}

		if (aUser->getIdentity().isBot() || aUser->getIdentity().isHub()) {
			flags_.insert("bot");
		}

		if (aUser->isHidden()) {
			flags_.insert("hidden");
		}
	}

	json Serializer::serializeUser(const UserPtr& aUser) noexcept {
		return{
			{ "cid", aUser->getCID().toBase32() },
			{ "nicks", Util::listToString(ClientManager::getInstance()->getHubNames(aUser->getCID())) },
			{ "flags", getUserFlags(aUser) }
		};
	}

	json Serializer::serializeHintedUser(const HintedUser& aUser) noexcept {
		auto flags = getUserFlags(aUser);
		if (aUser.user->isOnline()) {
			auto user = ClientManager::getInstance()->findOnlineUser(aUser);
			if (user) {
				appendOnlineUserFlags(user, flags);
			}
		}

		return {
			{ "cid", aUser.user->getCID().toBase32() },
			{ "nicks", ClientManager::getInstance()->getFormatedNicks(aUser) },
			{ "hub_url", aUser.hint },
			{ "hub_names", ClientManager::getInstance()->getFormatedHubNames(aUser) },
			{ "flags", flags }
		};
	}

	json Serializer::serializeMessage(const Message& aMessage) noexcept {
		if (aMessage.type == Message::TYPE_CHAT) {
			return{
				{ "chat_message", serializeChatMessage(aMessage.chatMessage) }
			};
		}

		return{
			{ "log_message", serializeLogMessage(aMessage.logMessage) }
		};
	}

	json Serializer::serializeChatMessage(const ChatMessagePtr& aMessage) noexcept {
		json ret = {
			{ "id", aMessage->getId()},
			{ "text", aMessage->getText() },
			{ "from", serializeOnlineUser(aMessage->getFrom()) },
			{ "time", aMessage->getTime() },
			{ "is_read", aMessage->getRead() }
		};

		if (aMessage->getTo()) {
			ret["to"] = serializeOnlineUser(aMessage->getTo());
		}

		if (aMessage->getReplyTo()) {
			ret["reply_to"] = serializeOnlineUser(aMessage->getReplyTo());
		}

		return ret;
	}

	string Serializer::getSeverity(LogMessage::Severity aSeverity) noexcept {
		switch (aSeverity) {
			case LogMessage::SEV_INFO: return "info";
			case LogMessage::SEV_WARNING: return "warning";
			case LogMessage::SEV_ERROR: return "error";
		}

		return Util::emptyString;
	}

	json Serializer::serializeLogMessage(const LogMessagePtr& aMessageData) noexcept {
		return{
			{ "id", aMessageData->getId() },
			{ "text", aMessageData->getText() },
			{ "time", aMessageData->getTime() },
			{ "severity", getSeverity(aMessageData->getSeverity()) },
			{ "is_read", aMessageData->getRead() }
		};
	}

	void Serializer::serializeCacheInfo(json& json_, const MessageCache& aCache, UnreadSerializerF unreadF) noexcept {
		json_["unread_messages"] = unreadF(aCache);
		json_["total_messages"] = aCache.size();
	}

	json Serializer::serializeUnreadLog(const MessageCache& aCache) noexcept {
		return{
			{ "info", aCache.countUnreadLogMessages(LogMessage::SEV_INFO) },
			{ "warning", aCache.countUnreadLogMessages(LogMessage::SEV_WARNING) },
			{ "error", aCache.countUnreadLogMessages(LogMessage::SEV_ERROR) },
		};
	}

	json Serializer::serializeUnreadChat(const MessageCache& aCache) noexcept {
		MessageCache::ChatMessageFilterF isBot = [](const ChatMessagePtr& aMessage) {
			if (aMessage->getFrom()->getIdentity().isBot() || aMessage->getFrom()->getIdentity().isHub()) {
				return true;
			}

			return aMessage->getReplyTo() && aMessage->getReplyTo()->getIdentity().isBot(); 
		};

		return {
			{ "user", aCache.countUnreadChatMessages(std::not1(isBot)) },
			{ "bot", aCache.countUnreadChatMessages(isBot) },
			{ "status", aCache.countUnreadLogMessages(LogMessage::SEV_LAST) },
		};
	}

	json Serializer::serializeOnlineUser(const OnlineUserPtr& aUser) noexcept {
		return serializeItemProperties(aUser, toPropertyIdSet(HubInfo::onlineUserPropertyHandler.properties), HubInfo::onlineUserPropertyHandler);
	}

	std::string typeNameToString(const string& aName) {
		switch (aName[0]) {
			case '1': return "audio";
			case '2': return "compressed";
			case '3': return "document";
			case '4': return "executable";
			case '5': return "picture";
			case '6': return "video";
			default: return "other";
		}
	}

	json Serializer::serializeFileType(const string& aPath) noexcept {
		auto ext = Format::formatFileType(aPath);
		auto typeName = SearchManager::getInstance()->getNameByExtension(ext, true);

		return{
			{ "id", "file" },
			{ "content_type", typeNameToString(typeName) },
			{ "str", ext }
		};
	}

	json Serializer::serializeFolderType(size_t aFiles, size_t aDirectories) noexcept {
		json retJson = {
			{ "id", "directory" },
			{ "str", Format::formatFolderContent(aFiles, aDirectories) }
		};

		if (aFiles >= 0 && aDirectories >= 0) {
			retJson["files"] = aFiles;
			retJson["directories"] = aDirectories;
		}

		return retJson;
	}

	json Serializer::serializeIp(const string& aIP) noexcept {
		return serializeIp(aIP, GeoManager::getInstance()->getCountry(aIP));
	}

	json Serializer::serializeIp(const string& aIP, const string& aCountryCode) noexcept {
		return{
			{ "str", Format::formatIp(aIP, aCountryCode) },
			{ "country_id", aCountryCode },
			{ "ip", aIP }
		};
	}
}