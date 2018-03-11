/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_MESSAGECACHE_H
#define DCPLUSPLUS_DCPP_MESSAGECACHE_H

#include "stdinc.h"

#include "typedefs.h"
#include "Message.h"
#include "CriticalSection.h"
#include "SettingsManager.h"

namespace dcpp {
	typedef deque<Message> MessageList;

	struct MessageCount {
		int logMessages = 0;
		int chatMessages = 0;

		bool hasMessages() const noexcept {
			return logMessages > 0 || chatMessages > 0;
		}
	};

	class MessageCache {
	public:

		typedef std::function<bool(const ChatMessagePtr& aMessage)> ChatMessageFilterF;

		MessageCache(SettingsManager::IntSetting aSetting) noexcept : setting(aSetting) { }
		MessageCache(const MessageCache& aCache) noexcept;

		template<class T>
		void addMessage(const T& aMessage) noexcept {
			add(Message(aMessage));
		}

		MessageList getMessages() const noexcept;
		const MessageList& getMessagesUnsafe() const noexcept {
			return messages;
		}

		LogMessageList getLogMessages() const noexcept;
		ChatMessageList getChatMessages() const noexcept;

		int size() const noexcept;
		int clear() noexcept;

		// Use the severity SEV_LAST to count all messages
		int countUnreadLogMessages(LogMessage::Severity aSeverity) const noexcept;
		int countUnreadChatMessages(ChatMessageFilterF filterF = nullptr) const noexcept;
		MessageCount setRead() noexcept;

		SharedMutex& getCS() const noexcept { return cs; }
	private:
		void add(Message&& aMessage) noexcept;

		SettingsManager::IntSetting setting;
		MessageList messages;

		mutable SharedMutex cs;
	};
}

#endif