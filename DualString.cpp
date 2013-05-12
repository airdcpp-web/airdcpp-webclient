/*
 * Copyright (C) 2013 AirDC++ Project
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

#include "DualString.h"
#include "Text.h"

using std::string;

wchar_t toLower(wchar_t c) noexcept {
#ifdef _WIN32
	return LOWORD(CharLowerW(reinterpret_cast<LPWSTR>(MAKELONG(c, 0))));
#else
	return (wchar_t)towlower(c);
#endif
}

wchar_t toUpper(wchar_t c) noexcept {
#ifdef _WIN32
	return LOWORD(CharUpperW(reinterpret_cast<LPWSTR>(MAKELONG(c, 0))));
#else
	return (wchar_t)towupper(c);
#endif
}

#define ARRAY_BITS (sizeof(MaskType)*8)

DualString::DualString(const string& aStr) : charSizes(nullptr) {
	reserve(aStr.size());

	int pos = 0;
	for (auto i = 0; i != aStr.size(); ) {
		wchar_t c = 0;
		int n = dcpp::Text::utf8ToWc(&aStr[i], c);
		if (n < 0) {
			append("_");
		} else {
			auto lc = toLower(c);
			if (lc != c) {
				if (!charSizes) {
					// Create an array with minumum possible length that will store the character sizes (unset=lowercase, set=uppercase)
					auto arrSize = aStr.size() % ARRAY_BITS == 0 ? aStr.size() / ARRAY_BITS : (aStr.size() / ARRAY_BITS) +1;
					charSizes = new MaskType[arrSize];
					for (uint8_t i = 0; i < arrSize; ++i) {
						charSizes[i] = 0;
					}
				}
				charSizes[pos] |= (1 << i);
			}

			dcpp::Text::wcToUtf8(lc, *this);
		}

		i++;
		if (i % ARRAY_BITS == 0) {
			pos++;
		}
	}
}

DualString::~DualString() { 
	if (charSizes)
		delete[] charSizes; 
}

string DualString::getNormal() const {
	if (!charSizes)
		return *this;

	string ret;
	ret.reserve(size());

	int pos = 0;
	for (auto i = 0; i != size(); ) {
		if (charSizes[pos] & (1 << i)) {
			wchar_t c = 0;
			dcpp::Text::utf8ToWc(&c_str()[i], c);

			dcpp::Text::wcToUtf8(toUpper(c), ret);
		} else {
			ret += c_str()[i];
		}

		i++;
		if (i % ARRAY_BITS == 0)
			pos++;
	}

	return ret;
}

bool DualString::lowerCaseOnly() const { 
	return !charSizes; 
}