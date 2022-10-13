/*
 * Copyright (C) 2011-2022 AirDC++ Project
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

#include "AdcHub.h"
#include "SearchQuery.h"
#include "StringTokenizer.h"
#include "Util.h"


namespace {
	inline uint16_t toCode(char a, char b) { return (uint16_t)a | ((uint16_t)b)<<8; }
}

namespace dcpp {

double SearchQuery::getRelevanceScore(const SearchQuery& aSearch, int aLevel, bool aIsDirectory, const string& aName) noexcept {
	// get the level scores first
	double scores = aLevel > 0 ? 9 / static_cast<double>(aLevel) : 10;
	double maxPoints = 10;

	auto positions = aSearch.getResultPositions(aName);
	if (positions.empty()) {
		// "Find and view NFO" in own list is performed without include terms, but we still want to prefer lower-level items
		return scores / maxPoints;
	}

	dcassert(boost::find_if(positions, CompareFirst<size_t, int>(string::npos)) == positions.end());

	// check the recursion level (ignore recursions if the last item was fully matched)
	int recursionLevel = 0;
	if (aSearch.recursion && aSearch.getLastIncludeMatches() != static_cast<int>(aSearch.include.count())) {
		recursionLevel = aSearch.recursion->recursionLevel;
	}

	// Prefer sequential matches
	bool isSorted = is_sorted(positions.begin(), positions.end());
	if (isSorted) {
		scores += 120;
	}
	maxPoints += 120;


	// maximum points from SearchQuery::toPointList based on the include count
	double maxPosPoints = (aSearch.include.count() * 20.0) + (20.0 * (recursionLevel+1));

	// separators
	double curPosPoints = 0;
	for (auto i : positions) {
		curPosPoints += i.second;
	}

	if (isSorted) {
		scores += curPosPoints;
	} else {
		scores += (curPosPoints / maxPosPoints) * 10;
	}
	maxPoints += maxPosPoints;


	// distance of the matched words (ignores missing separators)
	if (isSorted) {
		int minDistance = 0;
		for (const auto& p : aSearch.include.getPatterns()) {
			minDistance += p.size();
		}
		minDistance = minDistance + aSearch.include.count() - aSearch.include.getPatterns().back().size() - 1;

		int extraDistance = (positions.back().first - positions.front().first) - minDistance;
		scores += extraDistance > 0 ? max((1 / static_cast<double>(extraDistance)) * 20, 0.0) : 30;
	}
	maxPoints += 30;


	// positions of the first pattern (prefer the beginning)
	if (isSorted) {
		auto startPos = positions[0].first;
		scores += startPos > 0 ? (1 / static_cast<double>(startPos)) * 20 : 30;
	}
	maxPoints += 30;


	// prefer directories
	if (aIsDirectory) {
		scores += 5;
	}
	maxPoints += 5;


	// scale the points
	scores = scores / maxPoints;

	// drop results with no direct matches 
	if (recursionLevel > 0 && all_of(aSearch.getLastPositions().begin(), aSearch.getLastPositions().end(), [](size_t pos) { return pos == string::npos; })) {
		scores = scores / (recursionLevel + 1);
	}

	return scores;
}

SearchQuery::ResultPointsList SearchQuery::toPointList(const string& aName) const noexcept {
	ResultPointsList ret(lastIncludePositions.size());
	for (size_t j = 0; j < lastIncludePositions.size(); ++j) {
		int points = 0;
		auto pos = lastIncludePositions[j];
		if (pos != string::npos) {
			if (pos == 0) {
				points += 20;
			} else if (!Text::isSeparator(include.getPatterns()[j].str().front()) && Text::isSeparator(aName[pos - 1])) {
				points += 10;
			}

			auto endPos = pos + include.getPatterns()[j].size();
			if (endPos == aName.size()) {
				points += 20;
			} else if (!Text::isSeparator(include.getPatterns()[j].str().back()) && Text::isSeparator(aName[endPos])) {
				points += 10;
			}
		}

		ret[j] = { pos, points };
	}

	return ret;
}

SearchQuery* SearchQuery::getSearch(const SearchPtr& aSearch) noexcept {
	SearchQuery* s = nullptr;

	if(aSearch->fileType == Search::TYPE_TTH) {
		s = new SearchQuery(TTHValue(aSearch->query));
	} else {
		s = new SearchQuery(aSearch->query, aSearch->excluded, aSearch->exts, aSearch->matchType);
		if(aSearch->sizeType == Search::SIZE_ATLEAST) {
			s->gt = aSearch->size;
		} else if(aSearch->sizeType == Search::SIZE_ATMOST) {
			s->lt = aSearch->size;
		}

		s->itemType = (aSearch->fileType == Search::TYPE_DIRECTORY) ? SearchQuery::TYPE_DIRECTORY : (aSearch->fileType == Search::TYPE_FILE) ? SearchQuery::TYPE_FILE : SearchQuery::TYPE_ANY;
	}

	s->addParents = aSearch->returnParents;
	s->maxResults = aSearch->maxResults;
	return s;
}

StringList SearchQuery::parseSearchString(const string& aString) noexcept {
	return CommandTokenizer<string, vector>(aString).getTokens();
}

SearchQuery::SearchQuery(const string& nmdcString, Search::SizeModes aSizeMode, int64_t size, Search::TypeModes aFileType, size_t aMaxResults) noexcept : maxResults(aMaxResults) {
	if (aFileType == Search::TYPE_TTH && nmdcString.compare(0, 4, "TTH:") == 0) {
		root = TTHValue(nmdcString.substr(4));

	} else {
		StringTokenizer<string> tok(Text::toLower(nmdcString), '$');
		for (auto& term : tok.getTokens()) {
			if (!term.empty()) {
				include.addString(term);
			}
		}

		if (aSizeMode == Search::SIZE_ATLEAST) {
			gt = size;
		} else if (aSizeMode == Search::SIZE_ATMOST) {
			lt = size;
		}

		switch (aFileType) {
		case Search::TYPE_AUDIO: ext = AdcHub::parseSearchExts(1 << 0); break;
		case Search::TYPE_COMPRESSED: ext = AdcHub::parseSearchExts(1 << 1); break;
		case Search::TYPE_DOCUMENT: ext = AdcHub::parseSearchExts(1 << 2); break;
		case Search::TYPE_EXECUTABLE: ext = AdcHub::parseSearchExts(1 << 3); break;
		case Search::TYPE_PICTURE: ext = AdcHub::parseSearchExts(1 << 4); break;
		case Search::TYPE_VIDEO: ext = AdcHub::parseSearchExts(1 << 5); break;
		case Search::TYPE_DIRECTORY: itemType = SearchQuery::TYPE_DIRECTORY; break;
		default: break;
		}
	}

	prepare();
}

SearchQuery::SearchQuery(const TTHValue& aRoot) noexcept : root(aRoot) {

}

SearchQuery::SearchQuery(const string& aSearch, const StringList& aExcluded, const StringList& aExt, Search::MatchType aMatchType) noexcept : matchType(aMatchType) {

	//add included
	auto inc = move(parseSearchString(aSearch));
	for(auto& i: inc)
		include.addString(i);


	//add excluded
	for(auto& i: aExcluded)
		exclude.addString(i);

	for (auto& i : aExt)
		ext.push_back(Text::toLower(i));

	prepare();
}

SearchQuery::SearchQuery(const StringList& params, size_t aMaxResults) noexcept : maxResults(aMaxResults) {
	for(const auto& p: params) {
		if(p.length() <= 2)
			continue;

		uint16_t cmd = toCode(p[0], p[1]);
		if(toCode('T', 'R') == cmd) {
			root = TTHValue(p.substr(2));
			return;
		} else if(toCode('A', 'N') == cmd) {
			include.addString(p.substr(2));
		} else if(toCode('N', 'O') == cmd) {
			exclude.addString(p.substr(2));
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
			matchType = static_cast<Search::MatchType>(Util::toInt(p.substr(2)));
		} else if(toCode('O', 'T') == cmd) {
			maxDate = Util::toTimeT(p.substr(2));
		} else if(toCode('N', 'T') == cmd) {
			minDate = Util::toTimeT(p.substr(2));
		} else if(toCode('P', 'P') == cmd) {
			addParents = (p[2] == '1');
		}
	}

	prepare();
}

void SearchQuery::prepare() noexcept {
	lastIncludePositions.resize(include.count());
	fill(lastIncludePositions.begin(), lastIncludePositions.end(), string::npos);

	if (!ext.empty()) {
		itemType = TYPE_FILE;
	}
}

bool SearchQuery::hasExt(const string& name) noexcept {
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

bool SearchQuery::matchesFile(const string& aName, int64_t aSize, uint64_t aDate, const TTHValue& aTTH) noexcept {
	if (itemType == SearchQuery::TYPE_DIRECTORY) {
		return false;
	}

	if (root) {
		return aTTH == *root;
	}

	return matchesFileLower(Text::toLower(aName), aSize, aDate);
}

bool SearchQuery::matchesStr(const string& aStr) noexcept {
	return matchesFileLower(Text::toLower(aStr), 0, 0);
}

bool SearchQuery::matchesFileLower(const string& aName, int64_t aSize, uint64_t aDate) noexcept {
	if (!matchesDate(aDate) || !matchesSize(aSize)) {
		return false;
	}

	// Validate exact matches first
	if (matchType == Search::MATCH_NAME_EXACT && compare(include.getPatterns().front().str(), aName) != 0) {
		return false;
	}

	// Matching and positions
	resetPositions();
	lastIncludeMatches = include.matchLower(aName, recursion ? true : false, &lastIncludePositions);
	dcassert(count(lastIncludePositions.begin(), lastIncludePositions.end(), string::npos) == (int)include.count() - lastIncludeMatches);
	if (!positionsComplete())
		return false;

	// Check file type...
	if (!hasExt(aName))
		return false;


	if (isExcludedLower(aName))
		return false;

	return true;
}

bool SearchQuery::matchesAdcPath(const string& aPath, Recursion& recursion_) noexcept {
	auto sl = StringTokenizer<string>(aPath, ADC_SEPARATOR).getTokens();
	if (sl.empty()) {
		// Invalid path
		return false;
	}

	size_t level = 0;
	for (;;) {
		const auto& s = sl[level];
		resetPositions();
		lastIncludeMatches = include.matchLower(Text::toLower(s), true, &lastIncludePositions);

		level++;
		if (lastIncludeMatches > 0 && (level < sl.size())) { // no recursion if this is the last one
			// we got something worth of saving
			recursion_ = Recursion(*this, s);
			recursion = &recursion_;
		}

		if (level == sl.size())
			break;

		// moving to an upper level
		if (recursion) {
			recursion->increase(s.size());
		}
	}

	return positionsComplete();
}

SearchQuery::ResultPointsList SearchQuery::getResultPositions(const string& aName) const noexcept {
	// Do we need to use matches from a lower level?
	auto ret = toPointList(aName);
	if (recursion && find(lastIncludePositions, string::npos) != lastIncludePositions.end()) {
		Recursion::merge(ret, recursion);
		return ret;
	}

	return ret;
}

void SearchQuery::resetPositions() noexcept {
	if (lastIncludeMatches > 0) {
		fill(lastIncludePositions.begin(), lastIncludePositions.end(), string::npos);
		lastIncludeMatches = 0;
	}
	dcassert(count(lastIncludePositions.begin(), lastIncludePositions.end(), string::npos) == (int)lastIncludePositions.size());
}

bool SearchQuery::matchesDirectory(const string& aName) noexcept {
	if (itemType == TYPE_FILE)
		return false;

	//bool sizeOk = (aStrings.gt == 0);
	return include.match_all(aName);
}

bool SearchQuery::matchesAnyDirectoryLower(const string& aName) noexcept {
	if (matchType != Search::MATCH_PATH_PARTIAL && itemType == TYPE_FILE)
		return false;

	// no additional checks at this point to allow recursion to work

	resetPositions();
	lastIncludeMatches = include.matchLower(aName, true, &lastIncludePositions);
	dcassert(count(lastIncludePositions.begin(), lastIncludePositions.end(), string::npos) == (int)include.count() - lastIncludeMatches);
	return lastIncludeMatches > 0;
}

SearchQuery::Recursion::Recursion(const SearchQuery& aSearch, const string& aName) noexcept : positions(aSearch.toPointList(aName)) {
	if (aSearch.recursion && merge(positions, aSearch.recursion)) {
		depthLen = aSearch.recursion->depthLen;
		recursionLevel = aSearch.recursion->recursionLevel;
	}
}

bool SearchQuery::Recursion::completes(const StringSearch::ResultList& compareTo) const noexcept {
	for (size_t j = 0; j < positions.size(); ++j) {
		if (positions[j].first == string::npos && compareTo[j] == string::npos)
			return false;
	}
	return true;
}

bool SearchQuery::Recursion::isComplete() const noexcept {
	return none_of(positions.begin(), positions.end(), CompareFirst<size_t, int>(string::npos));
}

bool SearchQuery::Recursion::merge(ResultPointsList& mergeTo, const Recursion* parent) noexcept {
	auto& old = parent->positions;
	optional<size_t> startPos;

	// do we have anything that needs to be merged?
	for (size_t j = 0; j < old.size(); ++j) {
		if (mergeTo[j].first == string::npos && old[j].first != string::npos) {
			startPos = j;
			break;
		}
	}

	if (startPos) {
		// set the missing positions
		for (size_t j = *startPos; j < old.size(); ++j) {
			if (mergeTo[j].first == string::npos)
				mergeTo[j] = old[j];
			else
				mergeTo[j].first += parent->depthLen;
		}

		return true;
	}

	return false;
}

bool SearchQuery::positionsComplete() const noexcept {
	if (lastIncludeMatches == static_cast<int>(include.count()))
		return true;

	return recursion && recursion->completes(lastIncludePositions);
}

} //dcpp