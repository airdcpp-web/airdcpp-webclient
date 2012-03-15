/*
 * Copyright (C) 2011 AirDC++ Project
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

#include "stdinc.h"

#include "boost/regex.hpp"

#include <string>

#include "forward.h"
#include "StringTokenizer.h"
#include "StringSearch.h"
#include "Text.h"
#include "Wildcards.h"

namespace dcpp {

class StringMatcher {
public:
	enum Type {
		MATCHER_STRING,
		MATCHER_REGEX,
		MATCHER_WILDCARD,
	};

	StringMatcher(const string& aStr) { }
	virtual bool match(const string& aStr) = 0;
	//virtual const string& getPattern() const = 0;
	const string& getPattern() const { return pattern; }
	virtual Type getType() = 0;

	virtual ~StringMatcher() { }
private:
	string pattern;
};

class RegExMatcher : public StringMatcher {
public:
	RegExMatcher(const string& aStr) : StringMatcher(aStr) {
		reg.assign(aStr); 
	}
	~RegExMatcher() { }

	bool match(const string& aStr) { return regex_match(aStr, reg); }
	Type getType() { return Type::MATCHER_REGEX; }
private:
	boost::regex reg;
};


class WildcardMatcher : public StringMatcher {
public:
	WildcardMatcher(const string& aStr) : StringMatcher(aStr) { pattern = Text::utf8ToAcp(aStr); }
	~WildcardMatcher() { }

	bool match(const string& aStr) { return Wildcard::patternMatch(Text::utf8ToAcp(aStr), pattern, '|'); }
	Type getType() { return Type::MATCHER_WILDCARD; }
private:
	string pattern;
};


class TokenMatcher : public StringMatcher {
public:
	TokenMatcher(const string& aStr) : StringMatcher(aStr) { 
		StringTokenizer<string> st(aStr, ' ');
		for(auto i = st.getTokens().begin(); i != st.getTokens().end(); ++i) {
			if(i->size() > 0) {
				// Add substring search
				stringSearchList.push_back(StringSearch(*i));
			}
		}
	}
	~TokenMatcher() { }

	bool match(const string& aStr) {
		for(auto i = stringSearchList.begin(); i != stringSearchList.end(); ++i) {
			if(!i->match(aStr)) {
				return false;
			}
		}
		return true;
	}
	Type getType() { return Type::MATCHER_STRING; }
private:
	StringSearch::List stringSearchList;
};

}