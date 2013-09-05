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

#ifndef ADCSEARCH_H
#define ADCSEARCH_H

#include "typedefs.h"
#include "forward.h"

#include "StringSearch.h"
#include "HashValue.h"
#include "TigerHash.h"

#include <string>

namespace dcpp {

	class AdcSearch {
	public:
		enum MatchType {
			MATCH_FULL_PATH,
			MATCH_NAME,
			MATCH_EXACT
		};

		enum ItemType {
			TYPE_ANY,
			TYPE_FILE,
			TYPE_DIRECTORY
		};

		static AdcSearch* getSearch(const string& aSearchString, const string& aExcluded, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, MatchType aMatchType, bool returnParents);
		static StringList parseSearchString(const string& aString);

		AdcSearch(const StringList& params);

		AdcSearch(const string& aString, const string& aExcluded, const StringList& aExt, MatchType aMatchType);
		AdcSearch(const TTHValue& aRoot);

		bool isIndirectExclude(const string& aName) const;
		bool isExcluded(const string& str) const;
		bool hasExt(const string& name);

		StringSearch::List* include = &includeX;
		StringSearch::List includeX;
		StringSearch::List exclude;
		StringList ext;
		StringList noExt;

		int64_t gt = 0;
		int64_t lt = numeric_limits<int64_t>::max();

		uint32_t minDate = 0;
		uint32_t maxDate = numeric_limits<uint32_t>::max();

		optional<TTHValue> root;

		MatchType matchType = MATCH_FULL_PATH;
		bool addParents = false;

		ItemType itemType = TYPE_ANY;

		bool matchesFileLower(const string& aName, int64_t aSize, uint64_t aDate);
		bool matchesDirectory(const string& aName);

		//returns list of search terms that didn't match the name
		StringSearch::List* matchesDirectoryReLower(const string& aName);

		bool matchesSize(int64_t aSize);
		bool matchesDate(uint32_t aDate);
	};
}

#endif // !defined(ADCSEARCH_H)