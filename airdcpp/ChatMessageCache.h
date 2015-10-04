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

#ifndef DCPLUSPLUS_DCPP_CHATMESSAGECACHE_H
#define DCPLUSPLUS_DCPP_CHATMESSAGECACHE_H

#include "stdinc.h"

#include "typedefs.h"
#include "ChatMessage.h"
#include "CriticalSection.h"
#include "SettingsManager.h"

namespace dcpp {
	class ChatMessageCache : private boost::noncopyable {
	public:
		ChatMessageCache(SettingsManager::IntSetting aSetting) : setting(aSetting) { }

		void addMessage(const ChatMessagePtr& aMessage) noexcept {
			WLock l(cs);
			messages.push_back(aMessage);

			if (messages.size() > SettingsManager::getInstance()->get(setting)) {
				messages.pop_front();
			}
		}

		ChatMessageList getMessages() const noexcept {
			RLock l(cs);
			return messages;
		}

		int setRead() noexcept {
			RLock l(cs);
			int updated = 0;
			for (auto& message : messages) {
				if (!message->getRead()) {
					updated++;
					message->setRead(true);
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
			return std::accumulate(messages.begin(), messages.end(), 0, [](int aOld, const ChatMessagePtr& aMessage) {
				if (!aMessage->getRead()) {
					return aOld++;
				}

				return aOld;
			});
		}
	private:
		SettingsManager::IntSetting setting;
		ChatMessageList messages;

		mutable SharedMutex cs;
	};
}

#endif