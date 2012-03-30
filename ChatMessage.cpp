/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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


ChatLink::ChatLink(const string& aUrl, LinkType aLinkType) : url(aUrl), type(aLinkType), dupe(DUPE_NONE) {
	if (aLinkType == TYPE_SPOTIFY) {
		string type;
		string hash;
		boost::regex regSpotify;
		regSpotify.assign("(spotify:(artist|track|album):[A-Z0-9]{22})", boost::regex_constants::icase);

		if (boost::regex_match(aUrl, regSpotify)) {
			size_t found = aUrl.find_first_of(":");
			string tmp = aUrl.substr(found+1,aUrl.length());
			found = tmp.find_first_of(":");
			if (found != string::npos) {
				type = tmp.substr(0,found);
				hash = tmp.substr(found+1,tmp.length());
			}

			if (strcmpi(type.c_str(), "track") == 0) {
				displayText = STRING(SPOTIFY_TRACK);
			} else if (strcmpi(type.c_str(), "artist") == 0) {
				displayText = STRING(SPOTIFY_ARTIST);
			} else if (strcmpi(type.c_str(), "album") == 0) {
				displayText = STRING(SPOTIFY_ALBUM);
			}
		} else {
			//some other spotify link, just show the original url
			displayText = aUrl;
		}
	} else if (aLinkType == TYPE_RELEASE) {
		if (ShareManager::getInstance()->isDirShared(aUrl)) {
			dupe = DUPE_SHARE;
		} else {
			auto qd = QueueManager::getInstance()->isDirQueued(aUrl);
			if (qd == 1) {
				dupe = DUPE_QUEUE;
			} else if (qd == 2) {
				dupe = DUPE_FINISHED;
			}
		}
		displayText = aUrl;
	} else if (aLinkType == TYPE_MAGNET) {
		Magnet m = Magnet(aUrl);
		string::size_type dn = aUrl.find("dn=");
		if(dn != tstring::npos) {
			displayText = m.fname + " (" + Util::formatBytes(m.fsize) + ")";
			if (!m.hash.empty()) {
				if (m.isShareDupe()) {
					dupe = DUPE_SHARE;
				} else if (m.isQueueDupe() > 0) {
					dupe = DUPE_QUEUE;
				}
			}
		}
	} else {
		displayText = aUrl;
	}
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
