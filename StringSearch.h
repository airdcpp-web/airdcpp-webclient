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
	typedef vector<StringSearch> List;

	explicit StringSearch(const string& aPattern) noexcept;
	StringSearch(const StringSearch& rhs) noexcept;

	const StringSearch& operator=(const StringSearch& rhs);
	const StringSearch& operator=(const string& rhs);
	
	bool operator==(const StringSearch& rhs) { return pattern.compare(rhs.pattern) == 0; }

	const string& getPattern() const { return pattern; }

	bool match(const string& aText) const noexcept;
	
	/** Match a text against the pattern */
	bool matchLower(const string& aText) const noexcept;
private:
	enum { ASIZE = 256 };
	/** 
	 * Delta1 shift, uint16_t because we expect all patterns to be shorter than 2^16
	 * chars.
	 */
	uint16_t delta1[ASIZE];
	string pattern;

	void initDelta1();
};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_STRING_SEARCH_H