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

#ifndef DCPLUSPLUS_DCPP_SHARESTATS_H
#define DCPLUSPLUS_DCPP_SHARESTATS_H

#include "typedefs.h"

namespace dcpp {

struct ShareSearchStats {
	uint64_t totalSearches = 0;
	double totalSearchesPerSecond = 0;
	uint64_t recursiveSearches = 0, filteredSearches = 0;
	uint64_t averageSearchMatchMs = 0;
	uint64_t recursiveSearchesResponded = 0;

	double unfilteredRecursiveSearchesPerSecond = 0;

	double averageSearchTokenCount = 0;
	double averageSearchTokenLength = 0;

	uint64_t autoSearches = 0, tthSearches = 0;
};

struct ShareItemStats {
	int profileCount = 0;
	size_t rootDirectoryCount = 0;

	int64_t totalSize = 0;
	size_t totalFileCount = 0;
	size_t totalDirectoryCount = 0;
	size_t uniqueFileCount = 0;
	size_t lowerCaseFiles = 0;
	double averageNameLength = 0;
	size_t totalNameSize = 0;
	time_t averageFileAge = 0;
};

}

#endif