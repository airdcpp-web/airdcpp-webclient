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

#ifndef DCPLUSPLUS_DCPP_MESSAGEHIGHLIGHT_H
#define DCPLUSPLUS_DCPP_MESSAGEHIGHLIGHT_H

#include <airdcpp/typedefs.h>

#include <airdcpp/DupeType.h>
#include <airdcpp/GetSet.h>
#include <airdcpp/Magnet.h>
#include <airdcpp/Message.h>
#include <airdcpp/SortedVector.h>


namespace dcpp {
	class MessageCache;
}

namespace webserver {

	class MessageHighlight {
	public:
		enum HighlightType {
			TYPE_URL,
			TYPE_RELEASE,
			TYPE_TEMP_SHARE,
			TYPE_ME,
		};

		explicit MessageHighlight(size_t aStart, const string& aText, HighlightType aType, DupeType aDupeType = DUPE_NONE);
		// explicit MessageHighligh() { }

		string text;

		GETSET(HighlightType, type, Type);
		GETSET(DupeType, dupe, Dupe);
		GETSET(optional<Magnet>, magnet, Magnet);

		GETSET(size_t, start, Start);
		GETSET(size_t, end, End);

		// static boost::regex chatReleaseReg;
		// static boost::regex chatLinkReg;


		struct LinkSortOrder {
			int operator()(size_t a, size_t b) const noexcept;
		};

		struct LinkStartPos {
			size_t operator()(const MessageHighlight& a) const { return a.getStart(); }
		};

		typedef SortedVector<MessageHighlight, vector, size_t, LinkSortOrder, LinkStartPos> List;
		static List parseHighlights(const string& aText, const string& aMyNick, const UserPtr& aUser);
	};

	/*class MessageUtils {
	public:

		static boost::regex chatReleaseReg;
		static boost::regex chatLinkReg;

		static MessageHighlightList parseHighlights(const string& aText, const string& aMyNick, const UserPtr& aUser);

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

	};*/
}

#endif