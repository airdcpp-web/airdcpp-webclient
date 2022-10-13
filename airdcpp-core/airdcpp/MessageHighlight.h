/*
* Copyright (C) 2011-2022 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_MESSAGEHIGHLIGHT_H
#define DCPLUSPLUS_DCPP_MESSAGEHIGHLIGHT_H

#include "typedefs.h"

#include "DupeType.h"
#include "GetSet.h"
#include "Magnet.h"
#include "SortedVector.h"


namespace dcpp {
	typedef uint32_t MessageHighlightToken;

	struct Position {
		Position(size_t aStart, size_t aEnd) : start(aStart), end(aEnd) {}

		GETSET(size_t, start, Start);
		GETSET(size_t, end, End);
	};

	class MessageHighlight : public Position {
	public:
		enum HighlightType {
			TYPE_LINK_URL,
			TYPE_LINK_TEXT,
			TYPE_BOLD,
			TYPE_USER,
		};

		explicit MessageHighlight(size_t aStart, const string& aText, HighlightType aType, const string& aTag);

		MessageHighlightToken getToken() const noexcept {
			return token;
		}

		const string& getText() const noexcept {
			return text;
		}

		GETSET(string, tag, Tag);
		GETSET(HighlightType, type, Type);
		GETSET(optional<Magnet>, magnet, Magnet);

		DupeType getDupe() const noexcept;

		// typedef size_t KeyT;
		typedef Position KeyT;

		struct HighlightSort {
			int operator()(const KeyT& a, const KeyT& b) const noexcept;
		};

		struct HighlightPosition {
			const KeyT& operator()(const MessageHighlightPtr& a) const noexcept;
		};

		typedef SortedVector<MessageHighlightPtr, vector, KeyT, HighlightSort, HighlightPosition> SortedList;

		static MessageHighlight::SortedList parseHighlights(const string& aText, const string& aMyNick, const UserPtr& aUser);
	private:
		MessageHighlightToken token;
		string text;
	};

}

#endif