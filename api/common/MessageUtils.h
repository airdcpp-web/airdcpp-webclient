/*
* Copyright (C) 2011-2023 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_MESSAGEUTILS_H
#define DCPLUSPLUS_DCPP_MESSAGEUTILS_H

#include "forward.h"

#include <airdcpp/typedefs.h>

#include <airdcpp/DupeType.h>
#include <airdcpp/Message.h>
#include <airdcpp/MessageHighlight.h>
#include <airdcpp/SortedVector.h>


namespace dcpp {
	class MessageCache;
}

namespace webserver {
	class MessageUtils {
	public:
		static json serializeMessageHighlight(const MessageHighlightPtr& aHighlight);

		typedef std::function<MessageHighlightList(const json&, const ActionHookResultGetter<MessageHighlightList>&)> MessageHighlightDeserializer;
		static MessageHighlightDeserializer getMessageHookHighlightDeserializer(const string& aMessage);

		static json serializeMessage(const Message& aMessage) noexcept;
		static json serializeChatMessage(const ChatMessagePtr& aMessage) noexcept;
		static json serializeLogMessage(const LogMessagePtr& aMessageData) noexcept;

		typedef std::function<json(const MessageCache& aCache)> UnreadSerializerF;
		static json serializeCacheInfo(const MessageCache& aCache, const UnreadSerializerF& unreadF) noexcept;
		static json serializeUnreadChat(const MessageCache& aCache) noexcept;
		static json serializeUnreadLog(const MessageCache& aCache) noexcept;

		static bool hasMention(const ChatMessagePtr& aMessage) noexcept;
		static bool isBot(const ChatMessagePtr& aMessage) noexcept;
		static bool isUser(const ChatMessagePtr& aMessage) noexcept;

		static string getMessageSeverity(LogMessage::Severity aSeverity) noexcept;

		static string getHighlighType(MessageHighlight::HighlightType aType) noexcept;
		static json getContentType(const MessageHighlightPtr& aHighlight) noexcept;

		static string parseStatusMessageLabel(const SessionPtr& aSession) noexcept;
	private:
		static MessageHighlightList deserializeHookMessageHighlights(const json& aData, const ActionHookResultGetter<MessageHighlightList>& aResultGetter, const string& aMessageText);
		static MessageHighlightPtr deserializeMessageHighlight(const json& aJson, const string& aMessageText, const string& aDefaultDescriptionId);
		static MessageHighlight::HighlightType parseHighlightType(const string& aTypeStr);
	};
}

#endif