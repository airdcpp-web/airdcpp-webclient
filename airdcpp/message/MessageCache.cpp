/*
* Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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
#include <airdcpp/message/MessageCache.h>

namespace dcpp {
	MessageCache::MessageCache(SettingsManager::IntSetting aSetting) noexcept : setting(aSetting) {}

	MessageCache::MessageCache(const MessageCache& aCache) noexcept : setting(aCache.setting), messages(aCache.getMessages()), highlights(aCache.getHighlights()) {

	}

	MessageList MessageCache::getMessages() const noexcept {
		RLock l(cs);
		return messages;
	}

	MessageCache::HighlightList MessageCache::getHighlights() const noexcept {
		RLock l(cs);
		return highlights;
	}

	LogMessageList MessageCache::getLogMessages() const noexcept {
		LogMessageList ret;

		RLock l(cs);
		for (const auto& m : messages) {
			if (m.type == Message::TYPE_LOG) {
				ret.push_back(m.logMessage);
			}
		}

		return ret;
	}

	ChatMessageList MessageCache::getChatMessages() const noexcept {
		ChatMessageList ret;

		RLock l(cs);
		for (const auto& m : messages) {
			if (m.type == Message::TYPE_CHAT) {
				ret.push_back(m.chatMessage);
			}
		}

		return ret;
	}

	MessageCount MessageCache::setRead() noexcept {
		MessageCount updated;

		WLock l(cs);
		for (auto& message : messages) {
			if (message.type == Message::TYPE_CHAT) {
				if (!message.chatMessage->getRead()) {
					updated.chatMessages++;
					message.chatMessage->setRead(true);
				}
			} else if (!message.logMessage->getRead()) {
				updated.logMessages++;
				message.logMessage->setRead(true);
			}
		}

		return updated;
	}

	MessageHighlightPtr MessageCache::findMessageHighlight(MessageHighlightToken aToken) const noexcept {
		RLock l(cs);
		auto i = highlights.find(aToken);
		return i != highlights.end() ? i->second : nullptr;
	}

	int MessageCache::size() const noexcept {
		RLock l(cs);
		return static_cast<int>(messages.size());
	}

	int MessageCache::clear() noexcept {
		auto ret = size();

		WLock l(cs);
		messages.clear();
		highlights.clear();
		return ret;
	}

	int MessageCache::countUnreadChatMessages(const ChatMessageFilterF& filterF) const noexcept {
		RLock l(cs);
		return std::accumulate(messages.begin(), messages.end(), 0, [&](int aOld, const Message& aMessage) {
			if (aMessage.type != Message::TYPE_CHAT || (filterF && !filterF(aMessage.chatMessage))) {
				return aOld;
			}

			if (!aMessage.chatMessage->getRead()) {
				return aOld + 1;
			}

			return aOld;
		});
	}

	int MessageCache::countUnreadLogMessages(LogMessage::Severity aSeverity) const noexcept {
		RLock l(cs);
		return std::accumulate(messages.begin(), messages.end(), 0, [aSeverity](int aOld, const Message& aMessage) {
			if (aMessage.type != Message::TYPE_LOG) {
				return aOld;
			}

			if (aSeverity == LogMessage::SEV_LAST && aMessage.logMessage->getSeverity() == LogMessage::SEV_VERBOSE) {
				return aOld;
			}

			if (aSeverity != LogMessage::SEV_LAST && aMessage.logMessage->getSeverity() != aSeverity) {
				return aOld;
			}

			if (!aMessage.logMessage->getRead()) {
				return aOld + 1;
			}

			return aOld;
		});
	}

	void MessageCache::add(Message&& aMessage) noexcept {
		WLock l(cs);
		messages.push_back(std::move(aMessage));
		for (const auto& hl: aMessage.getHighlights()) {
			highlights.try_emplace(hl->getToken(), hl);
		}

		if (static_cast<int>(messages.size()) > SettingsManager::getInstance()->get(setting)) {
			decltype(auto) toRemove = messages.front();
			for (const auto& hl : toRemove.getHighlights()) {
				highlights.erase(hl->getToken());
			}

			messages.pop_front();
		}
	}

} // namespace dcpp
