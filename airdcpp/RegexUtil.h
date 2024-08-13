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

#ifndef DCPLUSPLUS_DCPP_REGEX_UTIL_H
#define DCPLUSPLUS_DCPP_REGEX_UTIL_H

#include <airdcpp/typedefs.h>

namespace dcpp {

class RegexUtil {
public:
	static bool listRegexMatch(const StringList& l, const boost::regex& aReg);
	static int listRegexCount(const StringList& l, const boost::regex& aReg);
	static void listRegexSubtract(StringList& l, const boost::regex& aReg);
	static bool stringRegexMatch(const string& aReg, const string& aString);

	static void getRegexMatchesT(const tstring& aString, TStringList& l, const boost::wregex& aReg);
	static void getRegexMatches(const string& aString, StringList& l, const boost::regex& aReg);

	static string regexEscape(const string& aStr, bool isWildcard) noexcept;

	static const string getPathReg() noexcept;
};

}

#endif // !defined(DCPLUSPLUS_DCPP_REGEX_UTIL_H)