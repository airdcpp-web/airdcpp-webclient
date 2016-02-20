/*
 * Copyright (C) 2011-2016 AirDC++ Project
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

#ifndef DCPP_SEARCH_H
#define DCPP_SEARCH_H

#include "typedefs.h"

namespace dcpp {

class Search {
public:
	enum Type : uint8_t {
		MANUAL,
		ALT,
		ALT_AUTO,
		AUTO_SEARCH,
	};

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

	Search(Type aSearchType, const string& aQuery, const string& aToken) noexcept : query(aQuery), type(aSearchType), token(aToken) { }
	~Search() { }

	SizeModes	sizeType = SIZE_DONTCARE;
	int64_t		size = 0;
	TypeModes	fileType = TYPE_ANY;
	string		query;
	const string		token;
	StringList	exts;
	StringList	excluded;
	set<void*>	owners;
	const Type	type;
	string		key;

	bool		aschOnly;

	optional<time_t> minDate;
	optional<time_t> maxDate;

	//optional<int64_t> minSize;
	//optional<int64_t> maxSize;
	
	bool operator==(const Search& rhs) const {
		 return this->sizeType == rhs.sizeType && 
		 		this->size == rhs.size && 
		 		this->fileType == rhs.fileType && 
		 		this->query == rhs.query;
	}

	bool operator<(const Search& rhs) const {
		 return this->type < rhs.type;
	}
};

}

#endif // !defined(SEARCH_H)