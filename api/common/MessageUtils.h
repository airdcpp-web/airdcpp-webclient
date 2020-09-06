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

#ifndef DCPLUSPLUS_DCPP_MESSAGEUTILS_H
#define DCPLUSPLUS_DCPP_MESSAGEUTILS_H

#include <airdcpp/typedefs.h>

#include <airdcpp/DupeType.h>
#include <airdcpp/Message.h>
#include <airdcpp/SortedVector.h>

#include "MessageHighlight.h"


namespace dcpp {
	class MessageCache;
}

namespace webserver {
	class MessageUtils {
	public:
		static json serializeHighlights(const string& aText, const string& aMyNick, const UserPtr& aUser);
		static json serializeMessageHighlight(const MessageHighlight& aHighlight);


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
		static json getContentType(const MessageHighlight& aHighlight) noexcept;
	private:

	};
}

#endif