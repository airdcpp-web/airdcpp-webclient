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

#include "MessageUtils.h"
#include "Serializer.h"

#include <web-server/JsonUtil.h>
#include <web-server/Session.h>
#include <web-server/WebUser.h>

#include <airdcpp/core/ActionHook.h>
#include <airdcpp/core/classes/Magnet.h>
#include <airdcpp/message/MessageCache.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/user/OnlineUser.h>



namespace webserver {
	string MessageUtils::getHighlightType(MessageHighlight::HighlightType aType) noexcept {
		switch (aType) {
			case MessageHighlight::HighlightType::TYPE_BOLD: return "bold";
			case MessageHighlight::HighlightType::TYPE_USER: return "user";
			case MessageHighlight::HighlightType::TYPE_LINK_URL: return "link_url";
			case MessageHighlight::HighlightType::TYPE_LINK_TEXT: return "link_text";
			default: return Util::emptyString;
		}
	}

	MessageHighlight::HighlightType MessageUtils::parseHighlightType(const string& aTypeStr) {
		if (aTypeStr == "link_text") {
			return MessageHighlight::HighlightType::TYPE_LINK_TEXT;
		} else if (aTypeStr == "link_url") {
			return MessageHighlight::HighlightType::TYPE_LINK_URL;
		} else if (aTypeStr == "bold") {
			return MessageHighlight::HighlightType::TYPE_BOLD;
		} else if (aTypeStr == "user") {
			return MessageHighlight::HighlightType::TYPE_USER;
		}

		throw std::domain_error("Invalid highlight type");
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
		case LogMessage::SEV_VERBOSE: return "verbose";
		case LogMessage::SEV_INFO: return "info";
		case LogMessage::SEV_WARNING: return "warning";
		case LogMessage::SEV_ERROR: return "error";
		default: return Util::emptyString;
		}
	}

	string MessageUtils::getMessageType(LogMessage::Type aType) noexcept {
		switch (aType) {
		case dcpp::LogMessage::Type::SYSTEM:
			return "system";
		case dcpp::LogMessage::Type::PRIVATE:
			return "private";
		case dcpp::LogMessage::Type::HISTORY:
			return "history";
		case dcpp::LogMessage::Type::SPAM:
			return "spam";
		case dcpp::LogMessage::Type::SERVER:
			return "server";
		default:
			return Util::emptyString;
		}
	}

	json MessageUtils::serializeLogMessage(const LogMessagePtr& aMessage) noexcept {
		return {
			{ "id", aMessage->getId() },
			{ "text", aMessage->getText() },
			{ "time", aMessage->getTime() },
			{ "severity", getMessageSeverity(aMessage->getSeverity()) },
			{ "label", aMessage->getLabel() },
			{ "is_read", aMessage->getRead() },
			{ "highlights", Serializer::serializeList(aMessage->getHighlights(), serializeMessageHighlight) },
			{ "type", getMessageType(aMessage->getType()) }
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
			{ "verbose", aCache.countUnreadLogMessages(LogMessage::SEV_VERBOSE) },
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
			{ "verbose", aCache.countUnreadLogMessages(LogMessage::SEV_VERBOSE) },
		};
	}

	json MessageUtils::getContentType(const MessageHighlightPtr& aHighlight) noexcept {
		if (!aHighlight->getMagnet()) {
			return json();
		}

		const auto ext = Util::formatFileType((*aHighlight->getMagnet()).fname);
		return Serializer::toFileContentType(ext);
	}

	json MessageUtils::serializeMessageHighlight(const MessageHighlightPtr& aHighlight) {
		return {
			{ "id", aHighlight->getToken() },
			{ "text", aHighlight->getText() },
			{ "type", getHighlightType(aHighlight->getType()) },
			{ "tag", aHighlight->getTag() },
			{ "position", {
				{ "start", aHighlight->getStart() },
				{ "end", aHighlight->getEnd() },
			}},
			{ "dupe", Serializer::serializeFileDupe(aHighlight->getDupe(), aHighlight->getMagnet() ? (*aHighlight->getMagnet()).getTTH() : TTHValue()) },
			{ "content_type", getContentType(aHighlight) },
		};
	}

	MessageHighlightPtr MessageUtils::deserializeMessageHighlight(const json& aJson, const string& aMessageText, const string& aDefaultDescriptionId) {
		const auto type = parseHighlightType(JsonUtil::getField<string>("type", aJson, false));

		const auto start = JsonUtil::getField<size_t>("start", aJson, false);
		const auto end = JsonUtil::getField<size_t>("end", aJson, false);
		const auto descriptionId = JsonUtil::getOptionalFieldDefault<string>("tag", aJson, aDefaultDescriptionId);

		if (end > aMessageText.size() || end <= start) {
			throw RequestException(http_status::bad_request, "Invalid range");
		}

		return make_shared<MessageHighlight>(start, aMessageText.substr(start, end - start), type, descriptionId);
	}

	MessageUtils::MessageHighlightDeserializer MessageUtils::getMessageHookHighlightDeserializer(const string& aMessageText) {
		return [aMessageText](const json& aData, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) {
			return deserializeHookMessageHighlights(aData, aResultGetter, aMessageText);
		};
	}

	MessageHighlightList MessageUtils::deserializeHookMessageHighlights(const json& aData, const ActionHookResultGetter<MessageHighlightList>& aResultGetter, const string& aMessageText) {
		const auto& highlightItems = JsonUtil::getOptionalArrayField("highlights", aData);

		MessageHighlightList ret;
		if (!highlightItems.is_null()) {
			for (const auto& hl : highlightItems) {
				ret.push_back(MessageUtils::deserializeMessageHighlight(hl, aMessageText, aResultGetter.getSubscriber().getId()));
			}
		}

		return ret;
	}

	string MessageUtils::parseStatusMessageLabel(const SessionPtr& aSession) noexcept {
		return aSession->getUser()->getUserName();
	}
}