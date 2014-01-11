/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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
#include "StringSearch.h"

#include "Text.h"

namespace dcpp {

StringSearch::Pattern::Pattern(const string& aPattern) noexcept : pattern(Text::toLower(aPattern)), plen(aPattern.length()) {
	initDelta1();
}

StringSearch::Pattern::Pattern(const Pattern& rhs) noexcept : pattern(rhs.pattern), plen(rhs.plen) {
	memcpy(delta1, rhs.delta1, sizeof(delta1));
}

const StringSearch::Pattern& StringSearch::Pattern::operator=(const Pattern& rhs) {
	memcpy(delta1, rhs.delta1, sizeof(delta1));
	pattern = rhs.pattern;
	plen = rhs.plen;
	return *this;
}

const StringSearch::Pattern& StringSearch::Pattern::operator=(const string& rhs) {
	pattern = Text::toLower(rhs);
	plen = rhs.size();
	initDelta1();
	return *this;
}

void StringSearch::Pattern::initDelta1() {
	uint16_t x = (uint16_t) (pattern.length() + 1);
	uint16_t i;
	for (i = 0; i < ASIZE; ++i) {
		delta1[i] = x;
	}
	// x = pattern.length();
	x--;
	uint8_t* p = (uint8_t*) pattern.data();
	for (i = 0; i < x; ++i) {
		delta1[p[i]] = (uint16_t) (x - i);
	}
}

size_t StringSearch::Pattern::matchLower(const string& aText, int aStartPos) const noexcept{
	dcassert(Text::isLower(aText));
	dcassert(pattern.length() == plen);
	const auto tlen = aText.length() - aStartPos;

	if (tlen < plen)
		return string::npos;

	// uint8_t to avoid problems with signed char pointer arithmetic
	uint8_t *tx = (uint8_t*) aText.c_str() + aStartPos;
	uint8_t *px = (uint8_t*) pattern.c_str();

	uint8_t *end = tx + tlen - plen + 1;
	while (tx < end) {
		size_t i = 0;
		for (; px[i] && (px[i] == tx[i]); ++i)
			;       // Empty!

		if (px[i] == 0) {
			return distance((uint8_t*)aText.c_str(), tx);
		}

		tx += delta1[tx[plen]];
	}

	return string::npos;
}


void StringSearch::addString(const string& aStr) {
	if (!aStr.empty())
		patterns.emplace_back(Text::toLower(aStr));
}

bool StringSearch::match_all(const string& aText) const {
	auto text = Text::toLower(aText);
	for (const auto& p : patterns) {
		if (p.matchLower(text) == string::npos) {
			return false;
		}
	}

	return true;
}

bool StringSearch::match_any_lower(const string& aText) const {
	for (const auto& p : patterns) {
		if (p.matchLower(aText) != string::npos) {
			return true;
		}
	}

	return false;
}

bool StringSearch::match_any(const string& aText) const {
	return match_any_lower(Text::toLower(aText));
}

int StringSearch::matchLower(const string& aText, bool aResumeOnNoMatch, ResultList* results_) const {
	int matches = 0, listPos = 0;
	for (const auto& p: patterns) {
		size_t addPos = string::npos;
		for (;;) {
			size_t curPos = p.matchLower(aText, addPos == string::npos ? 0 : addPos + 1);
			if (curPos != string::npos) {
				if (results_ && listPos > 0) {
					// prefer sequential match order if this isn't the first pattern
					if ((*results_)[listPos - 1] != string::npos && (*results_)[listPos - 1] > curPos) {
						addPos = curPos;
						continue; // keep on searching
					}
				}

				// use this match
				addPos = curPos;
			}

			if (addPos != string::npos) {
				matches++;
				if (results_) {
					(*results_)[listPos] = addPos;
				}
			} else if (!aResumeOnNoMatch) {
				if (results_) {
					fill_n((*results_).begin(), listPos, string::npos);
				}
				return 0;
			}

			break;
		}
		listPos++;
	}

	return matches;
}

void StringSearch::clear() {
	patterns.clear();
}

}