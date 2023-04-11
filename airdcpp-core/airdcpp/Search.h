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

#ifndef DCPLUSPLUS_DCPP_SEARCH_H
#define DCPLUSPLUS_DCPP_SEARCH_H

#include "constants.h"
#include "typedefs.h"
#include "Priority.h"

namespace dcpp {

class Search {
public:
	enum SizeModes : uint8_t {
		SIZE_DONTCARE = 0x00,
		SIZE_ATLEAST = 0x01,
		SIZE_ATMOST = 0x02,
		SIZE_EXACT = 0x03
	};

	enum TypeModes: uint8_t {
		TYPE_ANY = 0,
		TYPE_AUDIO,
		TYPE_COMPRESSED,
		TYPE_DOCUMENT,
		TYPE_EXECUTABLE,
		TYPE_PICTURE,
		TYPE_VIDEO,
		TYPE_DIRECTORY,
		TYPE_TTH,
		TYPE_FILE,
		TYPE_LAST
	};

	enum MatchType : uint8_t {
		MATCH_PATH_PARTIAL = 0,
		MATCH_NAME_PARTIAL,
		MATCH_NAME_EXACT
	};

	// Typical priorities: 
	// HIGH - manual foreground searches
	// NORMAL - manual background searches
	// LOW - automated queue searches
	Search(Priority aPriority, const string& aToken) noexcept : priority(aPriority), token(aToken) { }
	~Search() { }

	SizeModes	sizeType = SIZE_DONTCARE;
	int64_t		size = 0;
	TypeModes	fileType = TYPE_ANY;
	string		query;
	StringList	exts;
	StringList	excluded;
	const void*	owner = nullptr;
	string		key;

	bool		aschOnly = false;

	optional<time_t> minDate;
	optional<time_t> maxDate;

	// Direct searches
	bool returnParents = false;
	MatchType matchType = MATCH_PATH_PARTIAL;
	int maxResults = 10;
	bool requireReply = false;
	string path = ADC_ROOT_STR;

	/*optional<int64_t> minSize;
	optional<int64_t> maxSize;

	pair<int64_t, SizeModes> parseNmdcSize() const noexcept {
		if (minSize && maxSize && *minSize == *maxSize) {
			return { *minSize, SIZE_EXACT };
		}

		if (minSize) {
			return { *minSize, SIZE_ATLEAST };
		}

		if (maxSize) {
			return { *maxSize, SIZE_ATMOST };
		}

		return { 0, SIZE_DONTCARE };
	}*/

	const string token;
	const Priority priority;
	
	bool operator==(const Search& rhs) const {
		return this->sizeType == rhs.sizeType && 
				this->size == rhs.size && 
				this->fileType == rhs.fileType && 
				this->query == rhs.query;
	}

	bool operator<(const Search& rhs) const {
		return this->priority > rhs.priority;
	}

	typedef function<bool(const SearchPtr&)> CompareF;
	class ComparePtr {
	public:
		ComparePtr(const SearchPtr& compareTo) : a(compareTo) { }
		bool operator()(const SearchPtr& p) const noexcept {
			return p == a;
		}
	private:
		ComparePtr& operator=(const ComparePtr&) = delete;
		const SearchPtr& a;
	};

	class CompareOwner {
	public:
		CompareOwner(const void* compareTo) : a(compareTo) { }
		bool operator()(const SearchPtr& p) const noexcept {
			return !a ? true : p->owner == a;
		}
	private:
		CompareOwner& operator=(const CompareOwner&) = delete;
		const void* a;
	};

	struct PrioritySort {
		bool operator()(const SearchPtr& left, const SearchPtr& right) const noexcept {
			return left->priority > right->priority;
		}
	};
};

}

#endif // !defined(DCPLUSPLUS_DCPP_SEARCH_H)