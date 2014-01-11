/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_STRING_SEARCH_H
#define DCPLUSPLUS_DCPP_STRING_SEARCH_H

#include "typedefs.h"
#include "noexcept.h"

namespace dcpp {

/**
* A class that implements a fast substring search algo suited for matching
* one pattern against many strings (currently Quick Search, a variant of
* Boyer-Moore. Code based on "A very fast substring search algorithm" by
* D. Sunday).
* @todo Perhaps find an algo suitable for matching multiple substrings.
*/
class StringSearch {
public:
	typedef vector<size_t> ResultList;

	class Pattern {
	public:
		explicit Pattern(const string& aPattern) noexcept;
		Pattern(const Pattern& rhs) noexcept;

		const Pattern& operator=(const Pattern& rhs);
		const Pattern& operator=(const string& rhs);

		bool operator==(const Pattern& rhs) { return pattern.compare(rhs.pattern) == 0; }

		/** Match a text against the pattern */
		size_t matchLower(const string& aText, int aStartPos = 0) const noexcept;

		const string& str() const { return pattern; }
		inline string::size_type size() const { return plen; }
	private:
		enum { ASIZE = 256 };
		/**
		* Delta1 shift, uint16_t because we expect all patterns to be shorter than 2^16
		* chars.
		*/
		uint16_t delta1[ASIZE];
		string pattern;
		string::size_type plen;

		void initDelta1();
	};

	typedef vector<Pattern> PatternList;

	bool match_all(const string& aText) const;
	bool match_any(const string& aText) const;
	bool match_any_lower(const string& aText) const;

	int matchLower(const string& aText, bool aResumeOnNoMatch, ResultList* results_ = nullptr) const;
	void addString(const string& aPattern);
	void clear();

	inline size_t count() const { return patterns.size(); }
	inline bool empty() const { return patterns.empty(); }
	inline const PatternList& getPatterns() const { return patterns; }
private:
	PatternList patterns;
};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_STRING_SEARCH_H