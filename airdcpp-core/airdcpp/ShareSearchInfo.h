/*
* Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
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

#ifndef DCPLUSPLUS_DCPP_SHARE_SEARCH_INFO_H
#define DCPLUSPLUS_DCPP_SHARE_SEARCH_INFO_H

#include "typedefs.h"

namespace dcpp {

class SearchQuery;
struct ShareSearchStats;

struct ShareSearch {
	ShareSearch(SearchQuery& aSearch, const OptionalProfileToken& aProfile, const UserPtr& aOptionalUser, const string& aVirtualPath) :
		search(aSearch), profile(aProfile), optionalUser(aOptionalUser), virtualPath(aVirtualPath) {}

	SearchQuery& search;
	const OptionalProfileToken profile;
	const UserPtr& optionalUser;
	const string virtualPath;
	bool isAutoSearch = false;
};

struct ShareSearchCounters {
	uint64_t totalSearches = 0;
	uint64_t tthSearches = 0;
	uint64_t recursiveSearches = 0;
	uint64_t recursiveSearchTime = 0;
	uint64_t filteredSearches = 0;
	uint64_t recursiveSearchesResponded = 0;
	uint64_t searchTokenCount = 0;
	uint64_t searchTokenLength = 0;
	uint64_t autoSearches = 0;

	ShareSearchStats toStats() const noexcept;

	Callback onMatchingRecursiveSearch(const SearchQuery& aSearch) noexcept;
};


}

#endif