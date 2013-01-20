/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_CHAT_MESSAGE_H
#define DCPLUSPLUS_DCPP_CHAT_MESSAGE_H

#include "forward.h"
#include "AirUtil.h"
#include "GetSet.h"

namespace dcpp {

class ChatLink {

public:
	enum LinkType {
		TYPE_URL,
		TYPE_MAGNET,
		TYPE_RELEASE,
		TYPE_SPOTIFY,
		TYPE_PATH,
	};

	explicit ChatLink(const string& aLink, LinkType aLinkType, const UserPtr& aUser);
	explicit ChatLink() { }

	string url;
	string getDisplayText();
	DupeType updateDupeType(const UserPtr& aUser);

	GETSET(LinkType, type, Type);
	GETSET(DupeType, dupe, Dupe);
};


struct ChatMessage {
	string text;

	OnlineUserPtr from;
	OnlineUserPtr to;
	OnlineUserPtr replyTo;

	bool thirdPerson;
	time_t timestamp;

	string format() const;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_CHAT_MESSAGE_H)
