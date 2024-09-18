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

#include "stdinc.h"
#include "StringMatch.h"

#include "LogManager.h"
#include "RegexUtil.h"
#include "ResourceManager.h"
#include "StringTokenizer.h"

namespace dcpp {


StringMatch StringMatch::getSearch(const string& aPattern, Method aMethod) {
	StringMatch m;
	m.pattern = aPattern;
	m.setMethod(aMethod);
	m.prepare();
	return m;
}

StringMatch::Method StringMatch::getMethod() const noexcept {
	return boost::get<StringSearch>(&search) ? PARTIAL : boost::get<string>(&search) ? EXACT : isWildCard ? WILDCARD : REGEX;
}

void StringMatch::setMethod(Method method) {
	isWildCard = false;
	switch(method) {
		case PARTIAL: search = StringSearch(); break;
		case EXACT: search = string(); break;
		case REGEX: search = boost::regex(); break;
		case WILDCARD: search = boost::regex(); isWildCard=true; break;
		//case TTH: search = TTHValue(); break;
		//case TTH: search = string(); break;
		case METHOD_LAST: break;
	}
	//m = method;
}

bool StringMatch::operator==(const StringMatch& rhs) const noexcept {
	return pattern == rhs.pattern && getMethod() == rhs.getMethod();
}

struct Prepare : boost::static_visitor<bool> {
	Prepare(const string& aPattern, bool aWildCard, bool aVerbosePatternErrors) : pattern(aPattern), wildCard(aWildCard), verbosePatternErrors(aVerbosePatternErrors) {}
	Prepare& operator=(const Prepare&) = delete;

	bool operator()(StringSearch& s) const {
		s.clear();

		StringTokenizer<string> st(pattern, ' ');
		for(auto const& i: st.getTokens()) {
			s.addString(i);
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
				r.assign(RegexUtil::regexEscape(pattern, true), boost::regex::icase);
			} else {
				r.assign(pattern);
			}
			return true;
		} catch(const std::runtime_error&) {
			if (verbosePatternErrors) {
				LogManager::getInstance()->message(STRING_F(INVALID_PATTERN, pattern), LogMessage::SEV_ERROR, STRING(APPLICATION));
			}

			return false;
		}
	}

private:
	bool wildCard;
	const string& pattern;
	bool verbosePatternErrors = true;
};

bool StringMatch::prepare() {
	return !pattern.empty() && boost::apply_visitor(Prepare(pattern, isWildCard /*m == WILDCARD*/, verbosePatternErrors), search);
}

struct Match : boost::static_visitor<bool> {
	explicit Match(const string& aStr) : str(aStr) { }
	Match& operator=(const Match&) = delete;

	bool operator()(const StringSearch& s) const {
		return s.match_all(str);
	}

	bool operator()(const string& s) const {
		return Util::stricmp(str, s) == 0;
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
