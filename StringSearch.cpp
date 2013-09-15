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
#include "StringSearch.h"

#include "Text.h"

namespace dcpp {

StringSearch::StringSearch(const string& aPattern) noexcept : pattern(Text::toLower(aPattern)) {
	initDelta1();
}

StringSearch::StringSearch(const StringSearch& rhs) noexcept : pattern(rhs.pattern) { 
	memcpy(delta1, rhs.delta1, sizeof(delta1));
}

 const StringSearch& StringSearch::operator=(const StringSearch& rhs) {
	memcpy(delta1, rhs.delta1, sizeof(delta1));
	pattern = rhs.pattern;
	return *this;
}

const StringSearch& StringSearch::operator=(const string& rhs) {
	pattern = Text::toLower(rhs);
	initDelta1();
	return *this;
}

void StringSearch::initDelta1() {
	uint16_t x = (uint16_t)(pattern.length() + 1);
	uint16_t i;
	for(i = 0; i < ASIZE; ++i) {
		delta1[i] = x;
	}
	// x = pattern.length();
	x--;
	uint8_t* p = (uint8_t*)pattern.data();
	for(i = 0; i < x; ++i) {
		delta1[p[i]] = (uint16_t)(x - i);
	}
}

bool StringSearch::match(const string& aText) const noexcept{
	// Lower-case representation of UTF-8 string, since we no longer have that 1 char = 1 byte...
	string lower;
	Text::toLower(aText, lower);
	return matchLower(lower);
}

bool StringSearch::matchLower(const string& aText) const noexcept {
	const string::size_type plen = pattern.length();
	const string::size_type tlen = aText.length();

	if (tlen < plen)// fix UTF-8 support
		return false;

	// uint8_t to avoid problems with signed char pointer arithmetic
	uint8_t *tx = (uint8_t*)aText.c_str();
	uint8_t *px = (uint8_t*)pattern.c_str();

	uint8_t *end = tx + tlen - plen + 1;
	while (tx < end)
	{
		size_t i = 0;
		for (; px[i] && (px[i] == tx[i]); ++i)
			;       // Empty!

		if (px[i] == 0)
			return true;

		tx += delta1[tx[plen]];
	}

	return false;
}

}