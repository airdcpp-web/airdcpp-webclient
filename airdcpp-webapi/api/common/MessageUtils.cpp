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

#include "MessageUtils.h"

#include <airdcpp/AirUtil.h>
#include <airdcpp/Magnet.h>
#include <airdcpp/MessageCache.h>
#include <airdcpp/ShareManager.h>
#include <airdcpp/OnlineUser.h>

#include "Serializer.h"


namespace webserver {
	string MessageUtils::getHighlighType(MessageHighlight::HighlightType aType) noexcept {
		switch (aType) {
			case MessageHighlight::HighlightType::TYPE_ME: return "me";
			case MessageHighlight::HighlightType::TYPE_RELEASE: return "release";
			case MessageHighlight::HighlightType::TYPE_TEMP_SHARE: return "temp_share";
			case MessageHighlight::HighlightType::TYPE_URL: return "url";
			default: return Util::emptyString;
		}
	}

	// MESSAGES
	json MessageUtils::serializeMessage(const Message& aMessage) noexcept {
		if (aMessage.type == Message::TYPE_CHAT) {
			return{
				{ "chat_message", serializeChatMessage(aMessage.chatMessage) }
			};
		}

		return{
			{ "log_message", serializeLogMessage(aMessage.logMessage) }
		};
	}

	json MessageUtils::serializeChatMessage(const ChatMessagePtr& aMessage) noexcept {
		json ret = {
			{ "id", aMessage->getId()},
			{ "text", aMessage->getText() },
			{ "from", Serializer::serializeOnlineUser(aMessage->getFrom()) },
			{ "time", aMessage->getTime() },
			{ "is_read", aMessage->getRead() },
			{ "third_person", aMessage->getThirdPerson() },
			{ "highlights", Serializer::serializeList(aMessage->getHighlights(), serializeMessageHighlight) },
			{ "has_mention", hasMention(aMessage) },
		};

		if (aMessage->getTo()) {
			ret["to"] = Serializer::serializeOnlineUser(aMessage->getTo());
		}

		if (aMessage->getReplyTo()) {
			ret["reply_to"] = Serializer::serializeOnlineUser(aMessage->getReplyTo());
		}

		return ret;
	}

	string MessageUtils::getMessageSeverity(LogMessage::Severity aSeverity) noexcept {
		switch (aSeverity) {
		case LogMessage::SEV_NOTIFY: return "notify";
		case LogMessage::SEV_INFO: return "info";
		case LogMessage::SEV_WARNING: return "warning";
		case LogMessage::SEV_ERROR: return "error";
		default: return Util::emptyString;
		}
	}

	json MessageUtils::serializeLogMessage(const LogMessagePtr& aMessage) noexcept {
		return {
			{ "id", aMessage->getId() },
			{ "text", aMessage->getText() },
			{ "time", aMessage->getTime() },
			{ "severity", getMessageSeverity(aMessage->getSeverity()) },
			{ "is_read", aMessage->getRead() },
			{ "highlights", Serializer::serializeList(aMessage->getHighlights(), serializeMessageHighlight) }
		};
	}

	json MessageUtils::serializeCacheInfo(const MessageCache& aCache, const UnreadSerializerF& unreadF) noexcept {
		return {
			{ "total", aCache.size() },
			{ "unread", unreadF(aCache) },
		};
	}

	json MessageUtils::serializeUnreadLog(const MessageCache& aCache) noexcept {
		return {
			{ "info", aCache.countUnreadLogMessages(LogMessage::SEV_INFO) },
			{ "warning", aCache.countUnreadLogMessages(LogMessage::SEV_WARNING) },
			{ "error", aCache.countUnreadLogMessages(LogMessage::SEV_ERROR) },
		};
	}

	bool MessageUtils::hasMention(const ChatMessagePtr& aMessage) noexcept {
		return !aMessage->getMentionedNick().empty();
	}

	bool MessageUtils::isBot(const ChatMessagePtr& aMessage) noexcept {
		return !isUser(aMessage);
	}

	bool MessageUtils::isUser(const ChatMessagePtr& aMessage) noexcept {
		return aMessage->getFrom()->getIdentity().isUser();
	}

	json MessageUtils::serializeUnreadChat(const MessageCache& aCache) noexcept {
		return {
			{ "mention", aCache.countUnreadChatMessages(hasMention) },
			{ "user", aCache.countUnreadChatMessages(isUser) },
			{ "bot", aCache.countUnreadChatMessages(isBot) },
			{ "status", aCache.countUnreadLogMessages(LogMessage::SEV_LAST) },
		};
	}

	json MessageUtils::getContentType(const MessageHighlight::Ptr& aHighlight) noexcept {
		if (!aHighlight->getMagnet()) {
			return json();
		}

		const auto ext = Util::formatFileType((*aHighlight->getMagnet()).fname);
		return Serializer::toFileContentType(ext);
	}

	json MessageUtils::serializeMessageHighlight(const MessageHighlight::Ptr& aHighlight) {
		return {
			{ "id", aHighlight->getToken() },
			{ "text", aHighlight->getText() },
			{ "type", getHighlighType(aHighlight->getType()) },
			{ "position", {
				{ "start", aHighlight->getStart() },
				{ "end", aHighlight->getEnd() },
			}},
			{ "dupe", Serializer::serializeFileDupe(aHighlight->getDupe(), aHighlight->getMagnet() ? (*aHighlight->getMagnet()).getTTH() : TTHValue()) },
			{ "content_type", getContentType(aHighlight) },
		};
	}
}