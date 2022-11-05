/*
 * Copyright (C) 2001-2022 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_MESSAGE_H
#define DCPLUSPLUS_DCPP_MESSAGE_H

#include "forward.h"
#include "GetSet.h"
#include "MessageHighlight.h"

namespace dcpp {

struct OutgoingChatMessage {
	OutgoingChatMessage(const string& aMessage, const void* aOwner, bool aThirdPerson) noexcept : text(aMessage), owner(aOwner), thirdPerson(aThirdPerson) {}

	const string text;
	const void* owner;
	const bool thirdPerson;
};

class ChatMessage {
public:
	ChatMessage(const string& aText, const OnlineUserPtr& aFrom, const OnlineUserPtr& aTo = nullptr, const OnlineUserPtr& aReplyTo = nullptr) noexcept;

	GETSET(OnlineUserPtr, from, From);
	GETSET(OnlineUserPtr, to, To);
	GETSET(OnlineUserPtr, replyTo, ReplyTo);

	GETSET(time_t, time, Time);
	IGETSET(bool, thirdPerson, ThirdPerson, false);

	GETSET(bool, read, Read);

	string format() const noexcept;
	string formatAuthor() const noexcept;
	void parseMention(const Identity& aMe) noexcept;
	void parseHighlights(const Identity& aMe, const MessageHighlightList& aHighlights) noexcept;

	const string& getText() const noexcept {
		return text;
	}

	uint64_t getId() const noexcept {
		return id;
	}

	const string& getMentionedNick() const noexcept {
		return mentionedNick;
	}

	const MessageHighlight::SortedList& getHighlights() const noexcept {
		return highlights;
	}
private:
	static string cleanText(const string& aText) noexcept;

	MessageHighlight::SortedList highlights;
	string mentionedNick;
	string text;
	const uint64_t id;
};

class LogMessage {
public:
	enum Severity : uint8_t {
		SEV_NOTIFY, // Messages with this severity won't be saved to system log, only the event is fired
		SEV_INFO, 
		SEV_WARNING, 
		SEV_ERROR, 
		SEV_LAST 
	};


	enum Flags {
		FLAG_NORMAL = 0x00,
		FLAG_READ = 0x01,
		FLAG_DISABLE_HIGHLIGHTS = 0x02,
		FLAG_DISABLE_TIMESTAMP = 0x04,
		/** Match the queue against this list */
		//FLAG_MATCH_QUEUE = 0x08,
		/** The file list downloaded was actually an .xml.bz2 list */
		//FLAG_XML_BZLIST = 0x10,
	};

	LogMessage(const string& aMessage, Severity sev, const string& aLabel, int aFlags = LogMessage::Flags::FLAG_NORMAL) noexcept;

	uint64_t getId() const noexcept {
		return id;
	}

	const string& getText() const noexcept {
		return text;
	}

	string format() const noexcept;

	Severity getSeverity() const noexcept {
		return severity;
	}

	time_t getTime() const noexcept {
		return time;
	}

	bool isHistory() const noexcept {
		return time == 0;
	}

	IGETSET(bool, read, Read, false);

	const MessageHighlight::SortedList& getHighlights() const noexcept {
		return highlights;
	}

	const string& getLabel() const noexcept {
		return label;
	}
private:
	const uint64_t id;
	string text;
	const string label;
	const time_t time;
	const Severity severity;
	MessageHighlight::SortedList highlights;
};

struct Message {
	Message(const ChatMessagePtr& aMessage) noexcept : type(TYPE_CHAT), chatMessage(aMessage) {}
	Message(const LogMessagePtr& aMessage) noexcept : type(TYPE_LOG), logMessage(aMessage) {}
	static Message fromText(const string& aMessage, int aFlags = LogMessage::Flags::FLAG_NORMAL) noexcept;

	enum Type {
		TYPE_CHAT,
		TYPE_LOG
	};

	const ChatMessagePtr chatMessage = nullptr;
	const LogMessagePtr logMessage = nullptr;
	const MessageHighlight::SortedList& getHighlights() const noexcept {
		return type == TYPE_CHAT ? chatMessage->getHighlights() : logMessage->getHighlights();
	}

	const string& getText() const noexcept {
		return type == TYPE_CHAT ? chatMessage->getText() : logMessage->getText();
	}

	time_t getTime() const noexcept {
		return type == TYPE_CHAT ? chatMessage->getTime() : logMessage->getTime();
	}

	const string format() const noexcept {
		return type == TYPE_CHAT ? chatMessage->format() : logMessage->format();
	}

	const Type type;

	static string unifyLineEndings(const string& aText);
};

typedef std::function<void(const string&, LogMessage::Severity)> ModuleLogger;

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_CHAT_MESSAGE_H)
