/*
 * Copyright (C) 2011-2013 AirDC++ Project
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

#ifndef SEARCH_H
#define SEARCH_H

#pragma once

#include "typedefs.h"

namespace dcpp {

class Search {
public:
	Search() { }
	~Search() { }

	enum searchType {
		MANUAL,
		ALT,
		ALT_AUTO,
		AUTO_SEARCH,
	};

	int32_t		sizeType;
	int64_t		size;
	int32_t		fileType;
	string		query;
	string		token;
	StringList	exts;
	StringList	excluded;
	set<void*>	owners;
	searchType	type;
	string		key;
	int			dateMode;
	time_t		date;
	bool		aschOnly;

	
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