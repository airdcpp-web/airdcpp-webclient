/*
 * Copyright (C) 2001-2022 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_TEXT_H
#define DCPLUSPLUS_DCPP_TEXT_H

#include "debug.h"
#include "typedefs.h"

namespace dcpp {

/**
 * Text handling routines for DC++. DC++ internally uses UTF-8 for
 * (almost) all string:s, hence all foreign text must be converted
 * appropriately...
 * acp - ANSI code page used by the system
 * wide - wide unicode string
 * utf8 - UTF-8 representation of the string
 * t - current GUI text format
 * string - UTF-8 string (most of the time)
 * wstring - Wide string
 * tstring - GUI type string (acp string or wide string depending on build type)
 */
namespace Text {
	extern const string utf8;
	extern string systemCharset;

	void initialize();

	// Size of wchar_t is usually 16 bits on Windows and 32 bits on other platforms
	// For 16 bit wchars (UTF-16), we may have surrogate pairs and per-character conversion 
	// will produce invalid results for them
	// Use of these function should thus be avoided in platform-independent (or Windows-specific) code
	int utf8ToWc(const char* str, wchar_t& c);
	void wcToUtf8(wchar_t c, string& str);
#ifdef WIN32
	wstring acpToWide(const string& str, const string& fromCharset = "") noexcept;

	wstring utf8ToWide(const string& str) noexcept;

	string wideToAcp(const wstring& str, const string& toCharset = "") noexcept;
	string wideToUtf8(const wstring& str) noexcept;

	inline string acpToUtf8(const string& str, const string& fromCharset = "") noexcept {
		return wideToUtf8(acpToWide(str, fromCharset));
	}
	inline string utf8ToAcp(const string& str, const string& toCharset = "") noexcept {
		return wideToAcp(utf8ToWide(str), toCharset);
	}

#ifdef UNICODE
	inline tstring toT(const string& str) noexcept { return utf8ToWide(str); }
	inline string fromT(const tstring& str) noexcept { return wideToUtf8(str); }
#else
	inline tstring toT(const string& str) noexcept { return utf8ToAcp(str); }
	inline string fromT(const tstring& str) noexcept { return acpToUtf8(str); }
#endif

	inline const TStringList& toT(const StringList& lst, TStringList& tmp) noexcept {
		for (auto& i : lst)
			tmp.push_back(toT(i));
		return tmp;
	}

	inline const StringList& fromT(const TStringList& lst, StringList& tmp) noexcept {
		for (auto& i : lst)
			tmp.push_back(fromT(i));
		return tmp;
	}

	// Modifies the original string
	const wstring& toLowerReplace(wstring& tgt) noexcept;

	inline wstring toLower(const wstring& str) noexcept {
		wstring tmp(str);
		return toLowerReplace(tmp);
	}

	string toDOS(string tmp) noexcept;
	wstring toDOS(wstring tmp) noexcept;
#else
	string convert(const string& str, const string& fromCharset, const string& toCharset = "") noexcept;
#endif

	inline bool isAscii(const string& str) noexcept { return isAscii(str.c_str()); }
	bool isAscii(const char* str) noexcept;
	inline char asciiToLower(char c) { dcassert((((uint8_t)c) & 0x80) == 0); return (char)tolower(c); }

	string sanitizeUtf8(const string& str) noexcept;
	bool validateUtf8(const string& str) noexcept;

	wchar_t toLower(wchar_t c) noexcept;
	wchar_t toUpper(wchar_t c) noexcept;

	bool isLower(const string& str) noexcept;
	bool isLower(wchar_t c) noexcept;
	string toLower(const string& str) noexcept;

	string toUtf8(const string& str, const string& fromCharset = "") noexcept;
	string fromUtf8(const string& str, const string& toCharset = "") noexcept;

	inline bool isSeparator(char c) noexcept {
		return (c >= 32 && c <= 47) ||
			(c >= 58 && c <= 64) ||
			(c >= 91 && c <= 96) ||
			(c >= 123 && c <= 127);
	};

	bool isSeparator(wchar_t c) noexcept;
}

#ifdef _WIN32
# define NATIVE_NL "\r\n"
#else
# define NATIVE_NL "\n"
#endif

} // namespace dcpp

#endif
