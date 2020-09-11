/*
* Copyright (C) 2011-2019 AirDC++ Project
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
#include "ShareManager.h"
#include "OnlineUser.h"


namespace dcpp {

	atomic<MessageHighlightToken> messageHighlightIdCounter { 1 };

	MessageHighlight::MessageHighlight(size_t aStart, const string& aText, HighlightType aType) : token(messageHighlightIdCounter++), start(aStart), end(aStart + aText.size()), text(aText), type(aType) {

	}

	int MessageHighlight::LinkSortOrder::operator()(size_t a, size_t b) const noexcept {
		return compare(a, b);
	}

	MessageHighlight::List MessageHighlight::parseHighlights(const string& aText, const string& aMyNick, const UserPtr& aUser) {
		MessageHighlight::List ret;

		// My nick
		if (!aMyNick.empty()) {
			size_t lMyNickStart = string::npos;
			size_t lSearchFrom = 0;
			while ((lMyNickStart = aText.find(aMyNick, lSearchFrom)) != string::npos) {
				auto lMyNickEnd = lMyNickStart + aMyNick.size();
				lSearchFrom = lMyNickEnd;

				ret.insert_sorted(make_shared<MessageHighlight>(lMyNickStart, aMyNick, MessageHighlight::HighlightType::TYPE_ME));
			}
		}


		// Parse release names
		if (SETTING(FORMAT_RELEASE) || SETTING(DUPES_IN_CHAT)) {
			auto start = aText.cbegin();
			auto end = aText.cend();
			boost::match_results<string::const_iterator> result;
			int pos = 0;

			while (boost::regex_search(start, end, result, AirUtil::releaseRegChat, boost::match_default)) {
				std::string link(result[0].first, result[0].second);

				ret.insert_sorted(make_shared<MessageHighlight>(pos + result.position(), link, MessageHighlight::HighlightType::TYPE_RELEASE));
				start = result[0].second;
				pos += result.position() + link.length();
			}
		}

		// Parse links
		{
			try {
				auto start = aText.cbegin();
				auto end = aText.cend();
				boost::match_results<string::const_iterator> result;
				int pos = 0;

				while (boost::regex_search(start, end, result, AirUtil::urlReg, boost::match_default)) {
					string link(result[0].first, result[0].second);

					auto highlight = make_shared<MessageHighlight>(pos + result.position(), link, MessageHighlight::HighlightType::TYPE_URL);

					if (link.find("magnet:?") == 0) {
						auto m = Magnet::parseMagnet(link);
						if (m) {
							highlight->setMagnet(m);

							if (ShareManager::getInstance()->isTempShared(aUser, (*m).getTTH())) {
								highlight->setType(MessageHighlight::HighlightType::TYPE_TEMP_SHARE);
							}
						}
					}

					ret.insert_sorted(std::move(highlight));

					start = result[0].second;
					pos += result.position() + link.length();
				}

			} catch (...) {
				//...
			}
		}

		return ret;
	}

	DupeType MessageHighlight::getDupe() const noexcept {
		switch (type) {
			case TYPE_RELEASE: {
				return AirUtil::checkAdcDirectoryDupe(text, 0);
			}
			case TYPE_URL: {
				if (magnet) {
					return (*magnet).getDupeType();
				}
				break;
			}
		}

		return DUPE_NONE;
	}
}