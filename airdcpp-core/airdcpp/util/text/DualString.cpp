/*
 * Copyright (C) 2013-2024 AirDC++ Project
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

#include <airdcpp/util/text/DualString.h>
#include <airdcpp/util/text/Text.h>

using std::string;

#define ARRAY_BITS (sizeof(MaskType)*8)

// Text::toLower should be used for initial conversion due to UTF-16 surrogate handling
// Text::utf8ToWc should be sufficient for equality checks
DualString::DualString(const string& aStr) : str(dcpp::Text::toLower(aStr)) {
	init(aStr);
}

// Set possible uppercase characters
void DualString::init(const string& aNormalStr) noexcept {
	int arrayPos = 0, bitPos = 0;
	auto iNormal = aNormalStr.c_str();
	auto iLower = str.c_str();
	while (*iLower) {
		wchar_t cNormal = 0, cLower = 0;
		int nNormal = dcpp::Text::utf8ToWc(iNormal, cNormal);
		int nLower = dcpp::Text::utf8ToWc(iLower, cLower);
		if (cNormal != cLower) {
			if (!charSizes) {
				initSizeArray(aNormalStr.size());
			}
			charSizes.get()[arrayPos] |= (1 << bitPos);
		}

		iNormal += abs(nNormal);
		iLower += abs(nLower);

		bitPos += abs(nLower);

		// move to the next array?
		if (bitPos >= static_cast<int>(ARRAY_BITS)) {
			bitPos = 0 + bitPos - ARRAY_BITS;
			arrayPos++;
		}
	}
}

size_t DualString::length() const noexcept {
	return str.length();
}

// Create an array with minimum possible length that will store the character sizes (unset=lowercase, set=uppercase)
size_t DualString::initSizeArray(size_t strLen) noexcept {
	size_t arrSize = strLen % ARRAY_BITS == 0 ? strLen / ARRAY_BITS : (strLen / ARRAY_BITS) + 1;
	charSizes = std::make_unique<MaskType[]>(arrSize);
	for (size_t s = 0; s < arrSize; ++s) {
		charSizes.get()[s] = 0;
	}

	return arrSize;
}

DualString::DualString(DualString&& rhs) noexcept : charSizes(std::move(rhs.charSizes)), str(std::move(rhs.str)) {
}

string DualString::getNormal() const noexcept {
	if (!charSizes)
		return str;

	string ret;
	ret.reserve(length());

	int bitPos = 0, arrayPos = 0;
	const char* end = &str.c_str()[0] + str.size();
	for (const char* iLower = &str.c_str()[0]; iLower < end;) {
		if (charSizes.get()[arrayPos] & (1 << bitPos)) {
			wchar_t cLower = 0;
			int nLower = dcpp::Text::utf8ToWc(iLower, cLower);

			auto cUpper = dcpp::Text::toUpper(cLower);
			dcpp::Text::wcToUtf8(cUpper, ret);

			bitPos += abs(nLower);
			iLower += abs(nLower);
		} else {
			ret += iLower[0];
			bitPos++;
			iLower++;
		}


		if (bitPos >= static_cast<int>(ARRAY_BITS)) {
			bitPos = 0 + bitPos-ARRAY_BITS;
			arrayPos++;
		}
	}

	return ret;
}

bool DualString::lowerCaseOnly() const noexcept {
	return !charSizes; 
}