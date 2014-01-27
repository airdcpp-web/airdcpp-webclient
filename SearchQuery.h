/*
 * Copyright (C) 2011-2014 AirDC++ Project
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

#ifndef SEARCHQUERY_H
#define SEARCHQUERY_H

#include "typedefs.h"
#include "forward.h"

#include "StringSearch.h"
#include "HashValue.h"
#include "TigerHash.h"

#include <string>

namespace dcpp {

	class SearchQuery {
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

		typedef vector<pair<size_t, int>> ResultPointsList;

		// Gets a score (0-1) based on how well the current item matches the provided search (which must have been fully matched first)
		static double getRelevancyScores(const SearchQuery& aSearch, int aLevel, bool aIsDirectory, const string& aName);

		// Count points per pattern based on the matching positions (based on the surrounding separators)
		ResultPointsList toPointList(const string& aName) const;

		// General initialization
		static SearchQuery* getSearch(const string& aSearchString, const string& aExcluded, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, MatchType aMatchType, bool returnParents, size_t aMaxResults = 0);
		static StringList parseSearchString(const string& aString);
		SearchQuery(const string& aString, const string& aExcluded, const StringList& aExt, MatchType aMatchType);
		SearchQuery(const TTHValue& aRoot);

		// Protocol-specific
		SearchQuery(const StringList& adcParams, size_t maxResults);
		SearchQuery(const string& nmdcString, int searchType, int64_t size, int fileType, size_t maxResults);

		inline bool isExcluded(const string& str) const { return exclude.match_any(str); }
		inline bool isExcludedLower(const string& str) const { return exclude.match_any_lower(str); }
		bool hasExt(const string& name);

		StringSearch include;
		StringSearch exclude;
		StringList ext;
		StringList noExt;

		// get information about the previous matching
		const StringSearch::ResultList& getLastPositions() const { return lastIncludePositions; }
		int getLastIncludeMatches() const { return lastIncludeMatches; }

		// get the merged positions
		ResultPointsList getResultPositions(const string& aName) const;
		bool positionsComplete() const;


		// We count the positions from the beginning of name of the first matching item
		// This struct will keep the positions from the upper levels
		struct Recursion{
			Recursion() { }
			Recursion(const SearchQuery& aSearch, const string& aName);

			inline void increase(string::size_type aLen) { recursionLevel++; depthLen += aLen; }
			inline void decrease(string::size_type aLen) { recursionLevel--; depthLen -= aLen; }

			// are we complete after the new results?
			bool completes(const StringSearch::ResultList& compareTo) const;

			// are the positions complete already?
			bool isComplete() const;

			// merge old position to a new set of positions (new positions are preferred)
			// returns true if something from the parent list was needed
			static bool merge(ResultPointsList& mergeTo, const Recursion* parent);

			size_t depthLen = 0;
			int recursionLevel = 0;
			ResultPointsList positions;
		};

		Recursion* recursion = nullptr;

		int64_t gt = 0;
		int64_t lt = numeric_limits<int64_t>::max();

		time_t minDate = 0;
		time_t maxDate = numeric_limits<time_t>::max();

		optional<TTHValue> root;
		size_t maxResults = 0;

		MatchType matchType = MATCH_FULL_PATH;
		bool addParents = false;

		ItemType itemType = TYPE_ANY;

		// Returns true if any of the include strings were matched
		bool matchesAnyDirectoryLower(const string& aName);

		// Returns true if the file is a valid result
		bool matchesFileLower(const string& aName, int64_t aSize, uint64_t aDate);

		bool matchesDirectory(const string& aName);
		bool matchesFile(const string& aName, int64_t aSize, uint64_t aDate, const TTHValue& aTTH);

		// Returns true if all include strings were matched (no other checks)
		// The caller must ensure that recursion exists as long as the matches are used
		bool matchesNmdcPath(const string& aPath, Recursion& recursion_);

		inline bool matchesSize(int64_t aSize) { return aSize >= gt && aSize <= lt; }
		inline bool matchesDate(time_t aDate) { return aDate == 0 || (aDate >= minDate && aDate <= maxDate); }
	private:
		// Reset positions from the previous matching
		void resetPositions();
		void prepare();
		StringSearch::ResultList lastIncludePositions;
		int lastIncludeMatches = 0;
	};
}

#endif // !defined(SEARCHQUERY_H)