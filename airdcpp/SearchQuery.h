/*
 * Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPP_SEARCHQUERY_H
#define DCPP_SEARCHQUERY_H

#include "typedefs.h"
#include "forward.h"

#include "HashValue.h"
#include "Search.h"
#include "StringSearch.h"
#include "TigerHash.h"


namespace dcpp {

	class SearchQuery {
	public:
		/*enum MatchType {
			MATCH_FULL_PATH,
			MATCH_NAME,
			MATCH_EXACT
		};*/

		enum ItemType {
			TYPE_ANY,
			TYPE_FILE,
			TYPE_DIRECTORY
		};

		typedef vector<pair<size_t, int>> ResultPointsList;

		// Gets a score (0-1) based on how well the current item matches the provided search (which must have been fully matched first)
		static double getRelevanceScore(const SearchQuery& aSearch, int aLevel, bool aIsDirectory, const string& aName) noexcept;

		// Count points per pattern based on the matching positions (based on the surrounding separators)
		ResultPointsList toPointList(const string& aName) const noexcept;

		// General initialization
		static SearchQuery* getSearch(const SearchPtr& aSearch) noexcept;
		static StringList parseSearchString(const string& aString) noexcept;
		SearchQuery(const string& aString, const StringList& aExcluded, const StringList& aExt, Search::MatchType aMatchType) noexcept;
		SearchQuery(const TTHValue& aRoot) noexcept;

		// Protocol-specific
		SearchQuery(const StringList& adcParams, size_t maxResults) noexcept;
		SearchQuery(const string& nmdcString, Search::SizeModes aSizeMode, int64_t aSize, Search::TypeModes aTypeMode, size_t maxResults) noexcept;

		inline bool isExcluded(const string& str) const noexcept { return exclude.match_any(str); }
		inline bool isExcludedLower(const string& str) const noexcept { return exclude.match_any_lower(str); }
		bool hasExt(const string& name) noexcept;

		StringSearch include;
		StringSearch exclude;
		StringList ext;
		StringList noExt;

		// get information about the previous matching
		const StringSearch::ResultList& getLastPositions() const noexcept { return lastIncludePositions; }
		int getLastIncludeMatches() const noexcept { return lastIncludeMatches; }

		// get the merged positions
		ResultPointsList getResultPositions(const string& aName) const noexcept;
		bool positionsComplete() const noexcept;


		// We count the positions from the beginning of name of the first matching item
		// This struct will keep the positions from the upper levels
		struct Recursion{
			Recursion() noexcept { }
			Recursion(const SearchQuery& aSearch, const string& aName) noexcept;

			inline void increase(string::size_type aLen) noexcept { recursionLevel++; depthLen += aLen; }
			inline void decrease(string::size_type aLen) noexcept { recursionLevel--; depthLen -= aLen; }

			// are we complete after the new results?
			bool completes(const StringSearch::ResultList& compareTo) const noexcept;

			// are the positions complete already?
			bool isComplete() const noexcept;

			// merge old position to a new set of positions (new positions are preferred)
			// returns true if something from the parent list was needed
			static bool merge(ResultPointsList& mergeTo, const Recursion* parent) noexcept;

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

		Search::MatchType matchType = Search::MATCH_PATH_PARTIAL;
		bool addParents = false;

		ItemType itemType = TYPE_ANY;

		// Returns true if any of the include strings were matched. Saves positions
		bool matchesAnyDirectoryLower(const string& aName) noexcept;

		// Returns true if the file is a valid result. Saves positions
		bool matchesFileLower(const string& aName, int64_t aSize, uint64_t aDate) noexcept;

		// Plain string match with position storing
		bool matchesStr(const string& aStr) noexcept;

		// Simple match, no storing of positions
		bool matchesDirectory(const string& aName) noexcept;

		// Simple match, no storing of positions
		bool matchesFile(const string& aName, int64_t aSize, uint64_t aDate, const TTHValue& aTTH) noexcept;

		// Returns true if all include strings were matched (no other checks)
		// The caller must ensure that recursion exists as long as the matches are used
		bool matchesAdcPath(const string& aPath, Recursion& recursion_) noexcept;

		inline bool matchesSize(int64_t aSize) const noexcept { return aSize >= gt && aSize <= lt; }
		inline bool matchesDate(time_t aDate) const noexcept { return aDate == 0 || (aDate >= minDate && aDate <= maxDate); }
	private:
		// Reset positions from the previous matching
		void resetPositions() noexcept;
		void prepare() noexcept;
		StringSearch::ResultList lastIncludePositions;
		int lastIncludeMatches = 0;
	};
}

#endif // !defined(SEARCHQUERY_H)