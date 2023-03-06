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

#include "stdinc.h"

#include "Message.h"
#include "ClientManager.h"
#include "OnlineUser.h"

namespace dcpp {

atomic<uint64_t> messageIdCounter { 1 };

ChatMessage::ChatMessage(const string& aOriginalText, const OnlineUserPtr& aFrom, const OnlineUserPtr& aTo, const OnlineUserPtr& aReplyTo) noexcept :
	text(cleanText(aOriginalText)), from(aFrom), to(aTo), replyTo(aReplyTo), id(messageIdCounter++), time(GET_TIME()) {

	read = aFrom && aFrom->getUser() == ClientManager::getInstance()->getMe();
}

LogMessage::LogMessage(const string& aOriginalText, LogMessage::Severity aSeverity, const string& aLabel, int aFlags) noexcept :
	id(messageIdCounter++), text(Message::unifyLineEndings(aOriginalText)), label(aLabel),
	time(aFlags & FLAG_DISABLE_TIMESTAMP ? 0 : GET_TIME()), severity(aSeverity), read(aFlags & FLAG_DISABLE_TIMESTAMP) {

	if (!(aFlags & FLAG_DISABLE_HIGHLIGHTS)) {
		highlights = MessageHighlight::parseHighlights(text, Util::emptyString, nullptr);
	}
}

string LogMessage::format() const noexcept {
	return time == 0 ? text : "*** " + text;
}

string ChatMessage::formatAuthor() const noexcept {
	string tmp;

	const string& nick = from->getIdentity().getNick();
	// let's *not* obey the spec here and add a space after the star. :P
	tmp += (thirdPerson ? "* " + nick : '<' + nick + ">");

	return tmp;
}

string ChatMessage::cleanText(const string& aText) noexcept {
	auto tmp = Message::unifyLineEndings(aText);

	// Check all '<' and '[' after newlines as they're probably pastes...
	size_t i = 0;
	while ((i = tmp.find('\n', i)) != string::npos) {
		if (i + 1 < tmp.length()) {
			if (tmp[i + 1] == '[' || tmp[i + 1] == '<') {
				tmp.insert(i + 1, "- ");
				i += 2;
			}
		}
		i++;
	}

	return tmp;
}

string ChatMessage::format() const noexcept {
	return formatAuthor() + " " + text;
}

void ChatMessage::parseMention(const Identity& aMe) noexcept {
	// highlights = MessageHighlight::parseHighlights(text, aMe.getNick(), from->getUser());

	if (from->getIdentity().getSID() == aMe.getSID() || !from->getIdentity().isUser()) {
		return;
	}

	if (text.find(aMe.getNick()) != string::npos) {
		mentionedNick = aMe.getNick();
	}
}

void ChatMessage::parseHighlights(const Identity& aMe, const MessageHighlightList& aHookHighlights) noexcept {
	// Insert hook highlights
	for (const auto& hl: aHookHighlights) {
		highlights.insert_sorted(hl);
	}

	// Insert our highlights (that won't overlap)
	const auto defaultHighlights = MessageHighlight::parseHighlights(text, aMe.getNick(), to ? to->getUser() : nullptr);
	for (const auto& hl: defaultHighlights) {
		highlights.insert_sorted(hl);
	}
}

string Message::unifyLineEndings(const string& aText) {
	auto text = aText;

	string::size_type j = 0;
	while ((j = text.find("\r", j)) != string::npos)
		text.erase(j, 1);

	return text;
}

Message Message::fromText(const string& aMessage, int aFlags) noexcept {
	auto logMessage = std::make_shared<LogMessage>(aMessage, LogMessage::SEV_INFO, Util::emptyString, aFlags);
	return Message(logMessage);
}

} // namespace dcpp
