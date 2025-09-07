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

#ifndef DCPLUSPLUS_DCPP_MESSAGECACHE_H
#define DCPLUSPLUS_DCPP_MESSAGECACHE_H

#include "stdinc.h"

#include <airdcpp/core/header/typedefs.h>
#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/util/Util.h>

namespace dcpp {
	using MessageList = deque<Message>;

	struct MessageCount {
		int logMessages = 0;
		int chatMessages = 0;

		bool hasMessages() const noexcept {
			return logMessages > 0 || chatMessages > 0;
		}
	};

	class MessageCache {
	public:
		using HighlightList = map<MessageHighlightToken, MessageHighlightPtr>;

		using ChatMessageFilterF = std::function<bool (const ChatMessagePtr &)>;

		explicit MessageCache(SettingsManager::IntSetting aSetting) noexcept;
		MessageCache(const MessageCache& aCache) noexcept;

		template<class T>
		void addMessage(const T& aMessage) noexcept {
			add(Message(aMessage));
		}

		MessageList getMessages() const noexcept;
		const MessageList& getMessagesUnsafe() const noexcept {
			return messages;
		}

		HighlightList getHighlights() const noexcept;

		LogMessageList getLogMessages() const noexcept;
		ChatMessageList getChatMessages() const noexcept;

		int size() const noexcept;
		int clear() noexcept;

		// Use the severity SEV_LAST to count all messages
		int countUnreadLogMessages(LogMessage::Severity aSeverity) const noexcept;
		int countUnreadChatMessages(const ChatMessageFilterF& filterF = nullptr) const noexcept;
		MessageCount setRead() noexcept;

		SharedMutex& getCS() const noexcept { return cs; }
		MessageHighlightPtr findMessageHighlight(MessageHighlightToken aToken) const noexcept;
	private:
		void add(Message&& aMessage) noexcept;

		SettingsManager::IntSetting setting;
		MessageList messages;
		HighlightList highlights;

		mutable SharedMutex cs;
	};

	class ChatHandlerBase {
	public:
		virtual const string& getHubUrl() const noexcept = 0;
		virtual int clearCache() noexcept = 0;
		virtual void setRead() noexcept = 0;

		virtual const MessageCache& getCache() const noexcept = 0;
		virtual bool sendMessageHooked(const OutgoingChatMessage& aMessage, string& error_) = 0;
		virtual void statusMessage(const string& aMessage, LogMessage::Severity aSeverity, LogMessage::Type aType, const string& aLabel = Util::emptyString, const string& aOwner = Util::emptyString) noexcept = 0;
	};
}

#endif