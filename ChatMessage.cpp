/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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
#include "ChatMessage.h"
#include "ResourceManager.h"
#include "Magnet.h"

#include "OnlineUser.h"
#include "Util.h"

#include "QueueManager.h"
#include "ShareManager.h"

namespace dcpp {


ChatLink::ChatLink(const string& aUrl, LinkType aLinkType, const UserPtr& aUser) : url(Text::toUtf8(aUrl)), type(aLinkType), dupe(DUPE_NONE) {
	updateDupeType(aUser);
}

DupeType ChatLink::updateDupeType(const UserPtr& aUser) {
	if (type == TYPE_RELEASE) {
		if (ShareManager::getInstance()->isDirShared(url)) {
			dupe = DUPE_SHARE;
		} else {
			auto qd = QueueManager::getInstance()->isDirQueued(url);
			if (qd == 1) {
				dupe = DUPE_QUEUE;
			} else if (qd == 2) {
				dupe = DUPE_FINISHED;
			}
		}
	} else if (type == TYPE_MAGNET) {
		Magnet m = Magnet(url);
		dupe = m.getDupeType();
		if (dupe == DUPE_NONE && ShareManager::getInstance()->isTempShared(aUser ? aUser->getCID().toBase32() : Util::emptyString, m.getTTH())) {
			dupe = DUPE_SHARE;
		}
	}
	return dupe;
}

string ChatLink::getDisplayText() {
	if (type == TYPE_SPOTIFY) {
		boost::regex regSpotify;
		regSpotify.assign("(spotify:(artist|track|album):[A-Z0-9]{22})", boost::regex_constants::icase);

		if (boost::regex_match(url, regSpotify)) {
			string sType, displayText;
			size_t found = url.find_first_of(":");
			string tmp = url.substr(found+1,url.length());
			found = tmp.find_first_of(":");
			if (found != string::npos) {
				sType = tmp.substr(0,found);
			}

			if (Util::stricmp(sType.c_str(), "track") == 0) {
				displayText = STRING(SPOTIFY_TRACK);
			} else if (Util::stricmp(sType.c_str(), "artist") == 0) {
				displayText = STRING(SPOTIFY_ARTIST);
			} else if (Util::stricmp(sType.c_str(), "album") == 0) {
				displayText = STRING(SPOTIFY_ALBUM);
			}
			return displayText;
		}
		//some other spotify link, just show the original url
	} else if (type == TYPE_MAGNET) {
		Magnet m = Magnet(url);
		if(!m.fname.empty()) {
			return m.fname + " (" + Util::formatBytes(m.fsize) + ")";
		}
	}

	return url;
}

string ChatMessage::format() const {
	string tmp;

	if(timestamp) {
		tmp += '[' + Util::getShortTimeString(timestamp) + "] ";
	}

	const string& nick = from->getIdentity().getNick();
	// let's *not* obey the spec here and add a space after the star. :P
	tmp += (thirdPerson ? "* " + nick + ' ' : '<' + nick + "> ") + text;

	// Check all '<' and '[' after newlines as they're probably pastes...
	size_t i = 0;
	while( (i = tmp.find('\n', i)) != string::npos) {
		if(i + 1 < tmp.length()) {
			if(tmp[i+1] == '[' || tmp[i+1] == '<') {
				tmp.insert(i+1, "- ");
				i += 2;
			}
		}
		i++;
	}

	return Text::toDOS(tmp);
}

} // namespace dcpp
