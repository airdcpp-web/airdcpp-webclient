/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_STRING_MATCH_H
#define DCPLUSPLUS_DCPP_STRING_MATCH_H

#include "forward.h"
#include "StringSearch.h"

#include <string>

#include <boost/variant.hpp>

namespace dcpp {

using std::string;

/** Provides ways of matching a pattern against strings. */
struct StringMatch {
	enum Method {
		PARTIAL, /// case-insensitive pattern matching (multiple patterns separated with spaces)
		REGEX, /// regular expression
		WILDCARD,
		EXACT, /// case-sensitive, character-for-character equality

		METHOD_LAST
	};

	string pattern;

	//Method getMethod() const { return m; }
	Method getMethod() const;
	void setMethod(Method method);

	bool operator==(const StringMatch& rhs) const;

	bool prepare();
	bool match(const string& str) const;
private:
	boost::variant<StringSearch::List, string, boost::regex> search;
	bool isWildCard;
	//Method m;
};

} // namespace dcpp

#endif
