/*
 * Copyright (C) 2013-2014 AirDC++ Project
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

DualString::DualString(const string& aStr) {
	reserve(aStr.size());

	//auto tmp = dcpp::Text::toLower(aStr);
	int arrayPos = 0, bitPos = 0;
	const char* end = &aStr[0] + aStr.size();
	for (const char* p = &aStr[0]; p < end;) {
		wchar_t c = 0;
		int n = dcpp::Text::utf8ToWc(p, c);
		if (n < 0) {
			append("_");
		} else {
			auto lc = toLower(c);
			if (lc != c) {
				if (!charSizes) {
					initSizeArray(aStr.size());
				}
				charSizes[arrayPos] |= (1 << bitPos);
			}

			dcpp::Text::wcToUtf8(lc, *this);
		}

		p += n;
		bitPos += n;

		// move to the next array?
		if (bitPos >= static_cast<int>(ARRAY_BITS)) {
			bitPos = 0 + bitPos-ARRAY_BITS;
			arrayPos++;
		}
	}
}

// Create an array with minumum possible length that will store the character sizes (unset=lowercase, set=uppercase)
size_t DualString::initSizeArray(size_t strLen) {
	size_t arrSize = strLen % ARRAY_BITS == 0 ? strLen / ARRAY_BITS : (strLen / ARRAY_BITS) + 1;
	charSizes = new MaskType[arrSize];
	for (int s = 0; s < arrSize; ++s) {
		charSizes[s] = 0;
	}

	return arrSize;
}

DualString& DualString::operator=(DualString&& rhs) {
	assign(rhs.begin(), rhs.end());
	charSizes = rhs.charSizes;
	rhs.charSizes = nullptr;
	return *this; 
}

DualString::DualString(DualString&& rhs) : charSizes(rhs.charSizes) {
	assign(rhs.begin(), rhs.end());
	rhs.charSizes = nullptr;
}

DualString::DualString(const DualString& rhs) {
	assign(rhs.begin(), rhs.end());
	if (rhs.charSizes) {
		auto size = initSizeArray(rhs.size());
		for (int s = 0; s < size; ++s) {
			charSizes[s] = rhs.charSizes[s];
		}
	}
	//dcassert(0);
}

DualString& DualString::operator= (const DualString& rhs) {
	if (charSizes) {
		delete charSizes;
		charSizes = nullptr;
	}

	assign(rhs.begin(), rhs.end());
	if (rhs.charSizes) {
		auto size = initSizeArray(rhs.size());
		for (int s = 0; s < size; ++s) {
			charSizes[s] = rhs.charSizes[s];
		}
	}
	return *this;
}

DualString::~DualString() { 
	if (charSizes)
		delete charSizes; 
}

string DualString::getNormal() const {
	if (!charSizes)
		return *this;

	string ret;
	ret.reserve(size());

	int bitPos = 0, arrayPos = 0;
	const char* end = &c_str()[0] + string::size();
	for (const char* p = &c_str()[0]; p < end;) {
		if (charSizes[arrayPos] & (1 << bitPos)) {
			wchar_t c = 0;
			int n = dcpp::Text::utf8ToWc(p, c);

			dcpp::Text::wcToUtf8(toUpper(c), ret);

			bitPos += n;
			p += n;
		} else {
			ret += p[0];
			bitPos++;
			p++;
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