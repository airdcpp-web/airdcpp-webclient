/*
 * Copyright (C) 2011-2013 AirDC++ Project
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
#include "SearchManager.h"
#include "AirUtil.h"

namespace {
	inline uint16_t toCode(char a, char b) { return (uint16_t)a | ((uint16_t)b)<<8; }
}

namespace dcpp {

AdcSearch* AdcSearch::getSearch(const string& aSearchString, const string& aExcluded, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, MatchType aMatchType, bool returnParents) {
	AdcSearch* s = nullptr;

	if(aTypeMode == SearchManager::TYPE_TTH) {
		s = new AdcSearch(TTHValue(aSearchString));
	} else {
		s = new AdcSearch(aSearchString, aExcluded, aExtList, aMatchType);
		if(aSizeMode == SearchManager::SIZE_ATLEAST) {
			s->gt = aSize;
		} else if(aSizeMode == SearchManager::SIZE_ATMOST) {
			s->lt = aSize;
		}

		s->itemType = (aTypeMode == SearchManager::TYPE_DIRECTORY) ? AdcSearch::TYPE_DIRECTORY : (aTypeMode == SearchManager::TYPE_FILE) ? AdcSearch::TYPE_FILE : AdcSearch::TYPE_ANY;
		s->addParents = returnParents;
	}

	return s;
}

StringList AdcSearch::parseSearchString(const string& aString) {
	// similar to StringTokenizer but handles quotation marks (and doesn't create empty tokens)

	StringList ret;
	string::size_type i = 0, prev=0;
	auto addString = [&] {
		if (prev != i) {
			ret.push_back(aString.substr(prev, i-prev));
		}
		prev = i+1;
	};

	bool quote = false;
	while( (i = aString.find_first_of(" \"", i)) != string::npos) {
		switch(aString[i]) {
			case ' ': {
				if (!quote) addString();
				break;
			}
			case '\"': {
				quote = !quote;
				addString();
				break;
			}
		}
		i++;
	}

	if(prev < aString.size()) {
		i = aString.size();
		addString();
	}
		
	return ret;
}

AdcSearch::AdcSearch(const TTHValue& aRoot) : root(aRoot) {

}

AdcSearch::AdcSearch(const string& aSearch, const string& aExcluded, const StringList& aExt, MatchType aMatchType) : matchType(aMatchType) {

	//add included
	if (matchType == MATCH_EXACT) {
		includeX.emplace_back(aSearch);
	} else {
		auto inc = move(parseSearchString(aSearch));
		for(auto& i: inc)
			includeX.emplace_back(i);
	}


	//add excluded
	auto ex = move(parseSearchString(aExcluded));
	for(auto& i: ex)
		exclude.emplace_back(i);

	for (auto& i : aExt)
		ext.push_back(Text::toLower(i));
}

AdcSearch::AdcSearch(const StringList& params) {
	for(const auto& p: params) {
		if(p.length() <= 2)
			continue;

		uint16_t cmd = toCode(p[0], p[1]);
		if(toCode('T', 'R') == cmd) {
			root = TTHValue(p.substr(2));
			return;
		} else if(toCode('A', 'N') == cmd) {
			includeX.emplace_back(p.substr(2));		
		} else if(toCode('N', 'O') == cmd) {
			exclude.emplace_back(p.substr(2));
		} else if(toCode('E', 'X') == cmd) {
			ext.push_back(Text::toLower(p.substr(2)));
		} else if(toCode('G', 'R') == cmd) {
			auto exts = AdcHub::parseSearchExts(Util::toInt(p.substr(2)));
			ext.insert(ext.begin(), exts.begin(), exts.end());
		} else if(toCode('R', 'X') == cmd) {
			noExt.push_back(Text::toLower(p.substr(2)));
		} else if(toCode('G', 'E') == cmd) {
			gt = Util::toInt64(p.substr(2));
		} else if(toCode('L', 'E') == cmd) {
			lt = Util::toInt64(p.substr(2));
		} else if(toCode('E', 'Q') == cmd) {
			lt = gt = Util::toInt64(p.substr(2));
		} else if(toCode('T', 'Y') == cmd) {
			itemType = static_cast<ItemType>(Util::toInt(p.substr(2)));
		} else if(toCode('M', 'T') == cmd) {
			matchType = static_cast<MatchType>(Util::toInt(p.substr(2)));
		} else if(toCode('O', 'T') == cmd) {
			maxDate = Util::toUInt32(p.substr(2));
		} else if(toCode('N', 'T') == cmd) {
			minDate = Util::toUInt32(p.substr(2));
		} else if(toCode('P', 'P') == cmd) {
			addParents = (p[2] == '1');
		}
	}
}

bool AdcSearch::isIndirectExclude(const string& aName) const {
	if (root)
		return false;

	for (auto& i : includeX) {
		if (i.match(aName))
			return false;
	}

	return true;
}

bool AdcSearch::isExcluded(const string& str) const {
	for(auto& i: exclude) {
		if(i.match(str))
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

	for(const auto& i: ext) {
		if (name.length() >= i.length() && name.compare(name.length() - i.length(), i.length(), i.c_str()) == 0)
			return true;
	}
	return false;
}

bool AdcSearch::matchesFileLower(const string& aName, int64_t aSize, uint64_t aDate) {
	if(!(aSize >= gt)) {
		return false;
	} else if(!(aSize <= lt)) {
		return false;
	} else if (!(aDate == 0 || (aDate >= minDate && aDate <= maxDate))) {
		return false;
	}

	if (matchType == MATCH_EXACT) {
		if (compare((*include->begin()).getPattern(), aName) != 0)
			return false;
	} else {
		auto j = include->begin();
		for(; j != include->end() && j->matchLower(aName); ++j) 
			;	// Empty

		if(j != include->end())
			return false;
	}

	// Check file type...
	if (!hasExt(aName))
		return false;


	if(isExcluded(aName))
		return false;

	return true;
}

bool AdcSearch::matchesDirectory(const string& aName) {
	if (itemType == TYPE_FILE)
		return false;

	bool hasMatch = false;
	for(const auto& k: *include) {
		if(k.match(aName) && !isExcluded(aName))
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

StringSearch::List* AdcSearch::matchesDirectoryReLower(const string& aName) {
	StringSearch::List* newStr = nullptr;
	for(const auto& k: *include) {
		if(k.matchLower(aName)) {
			if(!newStr) {
				newStr = new StringSearch::List(*include);
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), k), newStr->end());
		}
	}

	return newStr;
}

bool AdcSearch::matchesSize(int64_t aSize) {
	return aSize >= gt && aSize <= lt;
}

bool AdcSearch::matchesDate(uint32_t aDate) {
	return aDate == 0 || (aDate >= minDate && aDate <= maxDate);
}

} //dcpp