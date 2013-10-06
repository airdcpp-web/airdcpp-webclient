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

#include "stdinc.h"
#include "StringMatch.h"

#include "AirUtil.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "StringTokenizer.h"

namespace dcpp {

StringMatch::Method StringMatch::getMethod() const {
	return boost::get<StringSearch::List>(&search) ? PARTIAL : boost::get<string>(&search) ? EXACT : isWildCard ? WILDCARD : REGEX;
}

void StringMatch::setMethod(Method method) {
	isWildCard = false;
	switch(method) {
		case PARTIAL: search = StringSearch::List(); break;
		case EXACT: search = string(); break;
		case REGEX: search = boost::regex(); break;
		case WILDCARD: search = boost::regex(); isWildCard=true; break;
		//case TTH: search = TTHValue(); break;
		//case TTH: search = string(); break;
		case METHOD_LAST: break;
	}
	//m = method;
}

bool StringMatch::operator==(const StringMatch& rhs) const {
	return pattern == rhs.pattern && getMethod() == rhs.getMethod();
}

struct Prepare : boost::static_visitor<bool> {
	Prepare(const string& aPattern, bool aWildCard) : pattern(aPattern), wildCard(aWildCard) {}
	Prepare& operator=(const Prepare&) = delete;

	bool operator()(StringSearch::List& s) const {
		s.clear();
		StringTokenizer<string> st(pattern, ' ');
		for(auto& i: st.getTokens()) {
			if(!i.empty()) {
				s.emplace_back(i);
			}
		}
		return true;
	}

	bool operator()(string& s) const {
		s = pattern;
		return true;
	}

	bool operator()(boost::regex& r) const {
		try {
			if (wildCard) {
				r.assign(AirUtil::regexEscape(pattern, true), boost::regex::icase);
			} else {
				r.assign(pattern);
			}
			return true;
		} catch(const std::runtime_error&) {
			LogManager::getInstance()->message(STRING_F(INVALID_PATTERN, pattern), LogManager::LOG_ERROR);
			return false;
		}
	}

private:
	bool wildCard;
	const string& pattern;
};

bool StringMatch::prepare() {
	return !pattern.empty() && boost::apply_visitor(Prepare(pattern, isWildCard /*m == WILDCARD*/), search);
}

struct Match : boost::static_visitor<bool> {
	Match(const string& aStr) : str(aStr) { }
	Match& operator=(const Match&) = delete;

	bool operator()(const StringSearch::List& s) const {
		for(auto& i: s) {
			if(!i.match(str)) {
				return false;
			}
		}
		return !s.empty();
	}

	bool operator()(const string& s) const {
		return str == s;
	}

	bool operator()(const boost::regex& r) const {
		try {
			return !r.empty() && boost::regex_search(str, r);
		} catch(const std::runtime_error&) {
			// most likely a stack overflow, ignore...
			return false;
		}
	}

	/*bool operator()(const TTHValue& t) const {
		return t.toBase32() == str;
	}*/

private:
	const string& str;
};

bool StringMatch::match(const string& str) const {
	return !str.empty() && boost::apply_visitor(Match(str), search);
}

} // namespace dcpp
