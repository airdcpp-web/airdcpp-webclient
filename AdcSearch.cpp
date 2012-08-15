/*
 * Copyright (C) 2011-2012 AirDC++ Project
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
#include "AdcSearch.h"
#include "Util.h"
#include "AdcHub.h"
#include "StringTokenizer.h"

namespace {
	inline uint16_t toCode(char a, char b) { return (uint16_t)a | ((uint16_t)b)<<8; }
}

namespace dcpp {

AdcSearch::AdcSearch(const TTHValue& aRoot) : root(aRoot), include(&includeX), gt(0), 
	lt(numeric_limits<int64_t>::max()), hasRoot(true), isDirectory(false) {
}

AdcSearch::AdcSearch(const string& aSearch, const StringList& aExt) : ext(aExt), include(&includeX), gt(0), 
	lt(numeric_limits<int64_t>::max()), hasRoot(false), isDirectory(false) {

	//add included
	StringTokenizer<string> st(aSearch, ' ');
	for(auto i = st.getTokens().begin(); i != st.getTokens().end(); ++i) {
		if(i->size() > 0) {
			includeX.push_back(StringSearch(*i));
		}
	}
}

AdcSearch::AdcSearch(const StringList& params) : include(&includeX), gt(0), 
	lt(numeric_limits<int64_t>::max()), hasRoot(false), isDirectory(false)
{
	for(auto i = params.begin(); i != params.end(); ++i) {
		const string& p = *i;
		if(p.length() <= 2)
			continue;

		uint16_t cmd = toCode(p[0], p[1]);
		if(toCode('T', 'R') == cmd) {
			hasRoot = true;
			root = TTHValue(p.substr(2));
			return;
		} else if(toCode('A', 'N') == cmd) {
			includeX.push_back(StringSearch(p.substr(2)));		
		} else if(toCode('N', 'O') == cmd) {
			exclude.push_back(StringSearch(p.substr(2)));
		} else if(toCode('E', 'X') == cmd) {
			ext.push_back(p.substr(2));
		} else if(toCode('G', 'R') == cmd) {
			auto exts = AdcHub::parseSearchExts(Util::toInt(p.substr(2)));
			ext.insert(ext.begin(), exts.begin(), exts.end());
		} else if(toCode('R', 'X') == cmd) {
			noExt.push_back(p.substr(2));
		} else if(toCode('G', 'E') == cmd) {
			gt = Util::toInt64(p.substr(2));
		} else if(toCode('L', 'E') == cmd) {
			lt = Util::toInt64(p.substr(2));
		} else if(toCode('E', 'Q') == cmd) {
			lt = gt = Util::toInt64(p.substr(2));
		} else if(toCode('T', 'Y') == cmd) {
			isDirectory = (p[2] == '2');
		}
	}
}

bool AdcSearch::isExcluded(const string& str) {
	for(auto i = exclude.begin(); i != exclude.end(); ++i) {
		if(i->match(str))
			return true;
	}
	return false;
}

bool AdcSearch::hasExt(const string& name) {
	if(ext.empty())
		return true;
	if(!noExt.empty()) {
		ext = StringList(ext.begin(), set_difference(ext.begin(), ext.end(), noExt.begin(), noExt.end(), ext.begin()));
		noExt.clear();
	}
	for(auto i = ext.cbegin(), iend = ext.cend(); i != iend; ++i) {
		if(name.length() >= i->length() && stricmp(name.c_str() + name.length() - i->length(), i->c_str()) == 0)
			return true;
	}
	return false;
}

bool AdcSearch::matchesDirectFile(const string& aName, int64_t aSize) {
	if(!(aSize >= gt)) {
		return false;
	} else if(!(aSize <= lt)) {
		return false;
	}	

	if(isExcluded(aName))
		return false;

	auto j = include->begin();
	for(; j != include->end() && j->match(aName); ++j) 
		;	// Empty

	if(j != include->end())
		return false;

	// Check file type...
	return hasExt(aName);
}

bool AdcSearch::matchesDirectDirectoryName(const string& aName) {
	bool hasMatch = false;
	for(auto k = include->begin(); k != include->end(); ++k) {
		if(k->match(aName) && !isExcluded(aName))
			hasMatch = true;
		else {
			hasMatch = false;
			break;
		}
	}

	//bool sizeOk = (aStrings.gt == 0);
	if(hasMatch && ext.empty()) {
		return true;
	}

	return false;
}

bool AdcSearch::matchesSize(int64_t aSize) {
	return aSize >= gt && aSize <= lt;
}

} //dcpp