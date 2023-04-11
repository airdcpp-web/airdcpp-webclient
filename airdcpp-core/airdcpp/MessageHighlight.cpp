/*
* Copyright (C) 2011-2023 AirDC++ Project
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

#include "MessageHighlight.h"

#include "AirUtil.h"
#include "FavoriteManager.h"
#include "ShareManager.h"
#include "OnlineUser.h"


namespace dcpp {

	string MessageHighlight::TAG_ME = "me";
	string MessageHighlight::TAG_FAVORITE = "favorite";
	string MessageHighlight::TAG_RELEASE = "release";
	string MessageHighlight::TAG_MAGNET = "magnet";
	string MessageHighlight::TAG_TEMP_SHARE = "temp_share";

	atomic<MessageHighlightToken> messageHighlightIdCounter { 1 };

	MessageHighlight::MessageHighlight(size_t aStart, const string& aText, HighlightType aType, const string& aTag) : 
		token(messageHighlightIdCounter++), 
		Position({ aStart, aStart + aText.size() }), 
		text(aText), type(aType), tag(aTag)
	{

	}

	int MessageHighlight::HighlightSort::operator()(const MessageHighlight::KeyT& a, const MessageHighlight::KeyT& b) const noexcept {
		// Overlapping ranges can't be added
		if (a.getStart() <= b.getEnd() && b.getStart() <= a.getEnd()) {
			return 0;
		}

		return compare(a.getStart(), b.getStart());
	}

	const MessageHighlight::KeyT& MessageHighlight::HighlightPosition::operator()(const MessageHighlightPtr& aHighlight) const noexcept {
		return *aHighlight;
	}

	MessageHighlight::SortedList MessageHighlight::parseHighlights(const string& aText, const string& aMyNick, const UserPtr& aTo) {
		MessageHighlight::SortedList ret;

		// Note: the earlier formatters will override the later ones in case of duplicates
		parseLinkHighlights(aText, ret, aTo);
		parseReleaseHighlights(aText, ret);
		parseUserHighlights(aText, ret, aMyNick);
		return ret;
	}

	void MessageHighlight::parseLinkHighlights(const string& aText, MessageHighlight::SortedList& highlights_, const UserPtr& aTo) {
		try {
			auto start = aText.cbegin();
			auto end = aText.cend();
			boost::match_results<string::const_iterator> result;
			int pos = 0;

			while (boost::regex_search(start, end, result, AirUtil::urlReg, boost::match_default)) {
				string link(result[0].first, result[0].second);

				auto highlight = make_shared<MessageHighlight>(pos + result.position(), link, MessageHighlight::HighlightType::TYPE_LINK_URL, "url");

				if (link.find("magnet:?") == 0) {
					auto m = Magnet::parseMagnet(link, aTo);
					if (m) {
						highlight->setMagnet(m);

						if (ShareManager::getInstance()->isTempShared(aTo, (*m).getTTH())) {
							highlight->setTag(TAG_TEMP_SHARE);
						}
						else {
							highlight->setTag(TAG_MAGNET);
						}
					}
				}

				highlights_.insert_sorted(std::move(highlight));

				start = result[0].second;
				pos += result.position() + link.length();
			}

		} catch (...) {
			//...
		}
	}

	void MessageHighlight::parseReleaseHighlights(const string& aText, MessageHighlight::SortedList& highlights_) {
		if (SETTING(FORMAT_RELEASE)) {
			auto start = aText.cbegin();
			auto end = aText.cend();
			boost::match_results<string::const_iterator> result;
			int pos = 0;

			while (boost::regex_search(start, end, result, AirUtil::releaseRegChat, boost::match_default)) {
				std::string link(result[0].first, result[0].second);

				highlights_.insert_sorted(make_shared<MessageHighlight>(pos + result.position(), link, MessageHighlight::HighlightType::TYPE_LINK_TEXT, TAG_RELEASE));
				start = result[0].second;
				pos += result.position() + link.length();
			}
		}
	}

	void MessageHighlight::parseUserHighlights(const string& aText, MessageHighlight::SortedList& highlights_, const string& aMyNick) {
		// My nick
		if (!aMyNick.empty()) {
			size_t start = string::npos;
			size_t pos = 0;
			while ((start = aText.find(aMyNick, pos)) != string::npos) {
				auto nickEnd = start + aMyNick.size();
				pos = nickEnd;

				highlights_.insert_sorted(make_shared<MessageHighlight>(start, aMyNick, MessageHighlight::HighlightType::TYPE_USER, TAG_ME));
			}
		}

		// Favorite users
		{
			RLock l(FavoriteManager::getInstance()->cs);
			auto& ul = FavoriteManager::getInstance()->getFavoriteUsers();
			for (const auto& favUser : ul | map_values) {
				decltype(auto) nick = favUser.getNick();
				if (nick.empty()) continue;

				size_t start = string::npos;
				size_t pos = 0;
				while ((start = (long)aText.find(nick, pos)) != tstring::npos) {
					auto lMyNickEnd = start + nick.size();
					pos = lMyNickEnd;

					highlights_.insert_sorted(make_shared<MessageHighlight>(start, nick, MessageHighlight::HighlightType::TYPE_USER, TAG_FAVORITE));
				}
			}
		}
	}

	DupeType MessageHighlight::getDupe() const noexcept {
		switch (type) {
			case TYPE_LINK_TEXT: {
				return AirUtil::checkAdcDirectoryDupe(text, 0);
			}
			case TYPE_LINK_URL: {
				if (magnet) {
					return (*magnet).getDupeType();
				}

				return DUPE_NONE;
			}
			case TYPE_BOLD:
			case TYPE_USER:
			default: return DUPE_NONE;
		}
	}
}