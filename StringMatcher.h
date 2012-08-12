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

#ifndef STRINGMATCHER_H
#define STRINGMATCHER_H

#include "pme.h"

#include <string>

#include "AirUtil.h"
#include "LogManager.h"
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
		MATCHER_TTH,
	};

	//StringMatcher(const string& aStr) : pattern(aStr) { }
	StringMatcher(const string& aStr) { }
	virtual bool match(const string& aStr) = 0;
	virtual bool match(const TTHValue& aTTH) = 0;
	virtual bool isCaseSensitive() = 0;
	virtual void setPattern(const string& aStr, bool isCaseSensitive=false) = 0;
	virtual const string& getPattern() const = 0;
	//const string& getPattern() const { return pattern; }
	virtual Type getType() = 0;

	virtual ~StringMatcher() { }
private:
	//string pattern;
};


class RegExMatcher : public StringMatcher {
public:
	RegExMatcher(const string& aStr, bool aCaseSensitive=false) : StringMatcher(aStr) {
		//reg.assign(aStr);
		setPattern(aStr, aCaseSensitive);
	}
	~RegExMatcher() { }

	void setPattern(const string& aStr, bool aCaseSensitive) {
		pattern = aStr;
		caseSensitive = aCaseSensitive;
		reg.Init(aStr, aCaseSensitive ? "" : "i");
		if (reg.IsValid()) {
			reg.study();
		} else {
			LogManager::getInstance()->message("Invalid regex: " + pattern, LogManager::LOG_ERROR);
		}
	}

	//bool match(const string& aStr) { return regex_match(aStr, reg); }
	bool match(const string& aStr) {
		try {
			return reg.match(aStr) > 0;
		} catch(const std::runtime_error&) {
			// most likely a stack overflow, ignore...
			return false;
		}
	}
	bool match(const TTHValue& aTTH) { return false; }
	bool isCaseSensitive() { return caseSensitive; }
	Type getType() { return MATCHER_REGEX; }
	const string& getPattern() const { return pattern; }
private:
	string pattern;
	//boost::regex reg;
	bool caseSensitive;
	PME reg;
};


class WildcardMatcher : public StringMatcher {
public:
	WildcardMatcher(const string& aStr, bool aCaseSensitive=false) : StringMatcher(aStr) { 
		setPattern(aStr, aCaseSensitive);
	}
	~WildcardMatcher() { }

	void setPattern(const string& aStr, bool aCaseSensitive=false) {
		pattern = aStr;
		caseSensitive = aCaseSensitive;
		string regex = AirUtil::regexEscape(aStr, true);
		reg.Init(regex, aCaseSensitive ? "" : "i");
		if (reg.IsValid()) {
			reg.study();
		} else {
			LogManager::getInstance()->message("Invalid wildcard: " + pattern, LogManager::LOG_ERROR);
		}
	}

	//bool match(const string& aStr) { return Wildcard::patternMatch(Text::utf8ToAcp(aStr), pattern, '|'); }
	bool match(const string& aStr) {
		try {
			return reg.match(aStr) > 0;
		} catch(const std::runtime_error&) {
			// most likely a stack overflow, ignore...
			return false;
		}
	}
	bool match(const TTHValue& aTTH) { return false; }
	bool isCaseSensitive() { return caseSensitive; }
	Type getType() { return MATCHER_WILDCARD; }
	const string& getPattern() const { return pattern; }
private:
	PME reg;
	string pattern;
	bool caseSensitive;
};


class TTHMatcher : public StringMatcher {
public:
	TTHMatcher(const string& aStr) : StringMatcher(aStr) { 
		setPattern(aStr);
	}
	~TTHMatcher() { }

	void setPattern(const string& aStr, bool aCaseSensitive=false) {
		pattern = aStr;
		tth = TTHValue(aStr);
	}

	bool match(const string& aStr) { return pattern == aStr; }
	//bool match(const string& aStr) { return tth.toBase32() == aStr; }
	bool match(const TTHValue& aTTH) { return tth == aTTH; }
	//const string& getPattern() const { return tth.toBase32(); }
	const string& getPattern() const { return pattern; }
	Type getType() { return MATCHER_TTH; }
	bool isCaseSensitive() { return false; }
private:
	TTHValue tth;
	string pattern;
};


class TokenMatcher : public StringMatcher {
public:
	TokenMatcher(const string& aStr) : StringMatcher(aStr) {
		setPattern(aStr);
	}
	~TokenMatcher() { }

	void setPattern(const string& aStr, bool aCaseSensitive=false) {
		pattern = aStr;
		StringTokenizer<string> st(aStr, ' ');
		for(auto i = st.getTokens().begin(); i != st.getTokens().end(); ++i) {
			if(i->size() > 0) {
				// Add substring search
				stringSearchList.push_back(StringSearch(*i));
			}
		}
	}

	bool match(const string& aStr) {
		for(auto i = stringSearchList.begin(); i != stringSearchList.end(); ++i) {
			if(!i->match(aStr)) {
				return false;
			}
		}
		return true;
	}
	bool match(const TTHValue& aTTH) { return false; }
	Type getType() { return MATCHER_STRING; }
	const string& getPattern() const { return pattern; }
	bool isCaseSensitive() { return false; }
private:
	StringSearch::List stringSearchList;
	string pattern;
};

}

#endif /* STRINGMATCHER_H */