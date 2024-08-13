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

#include "RegexUtil.h"

#include "Util.h"

#include <boost/algorithm/string/replace.hpp>

namespace dcpp {

bool RegexUtil::listRegexMatch(const StringList& l, const boost::regex& aReg) {
	return ranges::all_of(l, [&](const string& s) { return regex_match(s, aReg); });
}

int RegexUtil::listRegexCount(const StringList& l, const boost::regex& aReg) {
	return static_cast<int>(ranges::count_if(l, [&](const string& s) { return regex_match(s, aReg); }));
}

void RegexUtil::listRegexSubtract(StringList& l, const boost::regex& aReg) {
	l.erase(remove_if(l.begin(), l.end(), [&](const string& s) { return regex_match(s, aReg); }), l.end());
}

bool RegexUtil::stringRegexMatch(const string& aReg, const string& aString) {
	if (aReg.empty())
		return false;

	try {
		boost::regex reg(aReg);
		return boost::regex_match(aString, reg);
	}
	catch (...) {}
	return false;
}

void RegexUtil::getRegexMatchesT(const tstring& aString, TStringList& l, const boost::wregex& aReg) {
	auto start = aString.begin();
	auto end = aString.end();
	boost::match_results<tstring::const_iterator> result;
	try {
		while (boost::regex_search(start, end, result, aReg, boost::match_default)) {
			l.emplace_back(tstring(result[0].first, result[0].second));
			start = result[0].second;
		}
	} catch (...) {
		//...
	}
}

void RegexUtil::getRegexMatches(const string& aString, StringList& l, const boost::regex& aReg) {
	auto start = aString.begin();
	auto end = aString.end();
	boost::match_results<string::const_iterator> result;
	try {
		while (boost::regex_search(start, end, result, aReg, boost::match_default)) {
			l.emplace_back(string(result[0].first, result[0].second));
			start = result[0].second;
		}
	} catch (...) {
		//...
	}
}

const string RegexUtil::getPathReg() noexcept {
	return R"((?<=\s)(([A-Za-z0-9]:)|(\\))(\\[^\\:]+)(\\([^\s:])([^\\:])*)*((\.[a-z0-9]{2,10})|(\\))(?=(\s|$|:|,)))";
}

string RegexUtil::regexEscape(const string& aStr, bool aIsWildcard) noexcept {
	if (aStr.empty())
		return Util::emptyString;

	//don't replace | and ? if it's wildcard
	static const boost::regex re_boostRegexEscape(aIsWildcard ? R"([\^\.\$\(\)\[\]\*\+\?\/\\])" : R"([\^\.\$\|\(\)\[\]\*\+\?\/\\])");
	const string rep("\\\\\\1&");
	string result = regex_replace(aStr, re_boostRegexEscape, rep, boost::match_default | boost::format_sed);
	if (aIsWildcard) {
		//convert * and ?
		boost::replace_all(result, "\\*", ".*");
		boost::replace_all(result, "\\?", ".");
		result = "^(" + result + ")$";
	}

	return result;
}


}

