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

#ifndef DCPLUSPLUS_DCPP_MESSAGECACHE_H
#define DCPLUSPLUS_DCPP_MESSAGECACHE_H

#include "stdinc.h"

#include "typedefs.h"
#include "Message.h"
#include "CriticalSection.h"
#include "SettingsManager.h"

namespace dcpp {
	typedef deque<Message> MessageList;

	class MessageCache : private boost::noncopyable {
	public:
		MessageCache(SettingsManager::IntSetting aSetting) : setting(aSetting) { }

		template<class T>
		void addMessage(const T& aMessage) noexcept {
			add(Message(aMessage));
		}

		MessageList getMessages() const noexcept {
			RLock l(cs);
			return messages;
		}

		int setRead() noexcept {
			RLock l(cs);
			int updated = 0;
			for (auto& message : messages) {
				if (message.type != Message::TYPE_CHAT) {
					continue;
				}

				//message.
				if (!message.chatMessage->getRead()) {
					updated++;
					message.chatMessage->setRead(true);
				}
			}

			return updated;
		}

		int size() const noexcept {
			RLock l(cs);
			return static_cast<int>(messages.size());
		}

		int countUnread() const noexcept {
			RLock l(cs);
			return std::accumulate(messages.begin(), messages.end(), 0, [](int aOld, const Message& aMessage) {
				if (aMessage.type != Message::TYPE_CHAT) {
					return aOld;
				}

				if (!aMessage.chatMessage->getRead()) {
					return aOld + 1;
				}

				return aOld;
			});
		}
	private:
		void add(Message&& aMessage) {
			WLock l(cs);
			messages.push_back(move(aMessage));

			if (messages.size() > SettingsManager::getInstance()->get(setting)) {
				messages.pop_front();
			}
		}

		SettingsManager::IntSetting setting;
		MessageList messages;

		mutable SharedMutex cs;
	};
}

#endif