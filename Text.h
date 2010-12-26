/*
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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

	const string& acpToUtf8(const string& str, string& tmp, const string& fromCharset = "") throw();
	inline string acpToUtf8(const string& str, const string& fromCharset = "") throw() {
		string tmp;
		return acpToUtf8(str, tmp, fromCharset);
	}

	const wstring& acpToWide(const string& str, wstring& tmp, const string& fromCharset = "") throw();
	inline wstring acpToWide(const string& str, const string& fromCharset = "") throw() {
		wstring tmp;
		return acpToWide(str, tmp, fromCharset);
	}

	const string& utf8ToAcp(const string& str, string& tmp, const string& toCharset = "") throw();
	inline string utf8ToAcp(const string& str, const string& toCharset = "") throw() {
		string tmp;
		return utf8ToAcp(str, tmp, toCharset);
	}

	const wstring& utf8ToWide(const string& str, wstring& tmp) throw();
	inline wstring utf8ToWide(const string& str) throw() {
		wstring tmp;
		return utf8ToWide(str, tmp);
	}

	const string& wideToAcp(const wstring& str, string& tmp, const string& toCharset = "") throw();
	inline string wideToAcp(const wstring& str, const string& toCharset = "") throw() {
		string tmp;
		return wideToAcp(str, tmp, toCharset);
	}
	
	const string& wideToUtf8(const wstring& str, string& tmp) throw();
	inline string wideToUtf8(const wstring& str) throw() {
		string tmp;
		return wideToUtf8(str, tmp);
	}

	int utf8ToWc(const char* str, wchar_t& c);
	void wcToUtf8(wchar_t c, string& str);

#ifdef UNICODE
	inline const tstring& toT(const string& str, tstring& tmp) throw() { return utf8ToWide(str, tmp); }
	inline tstring toT(const string& str) throw() { return utf8ToWide(str); }

	inline const string& fromT(const tstring& str, string& tmp) throw() { return wideToUtf8(str, tmp); }
	inline string fromT(const tstring& str) throw() { return wideToUtf8(str); }
#else
	inline const tstring& toT(const string& str, tstring& tmp) throw() { return utf8ToAcp(str, tmp); }
	inline tstring toT(const string& str) throw() { return utf8ToAcp(str); }

	inline const string& fromT(const tstring& str, string& tmp) throw() { return acpToUtf8(str, tmp); }
	inline string fromT(const tstring& str) throw() { return acpToUtf8(str); }
#endif

	inline bool isAscii(const string& str) throw() { return isAscii(str.c_str()); }
	bool isAscii(const char* str) throw();
	
	bool validateUtf8(const string& str) throw();

	inline char asciiToLower(char c) { dcassert((((uint8_t)c) & 0x80) == 0); return (char)tolower(c); }

	inline wchar_t toLower(wchar_t c) throw() {
#ifdef _WIN32
		return static_cast<wchar_t>(reinterpret_cast<ptrdiff_t>(CharLowerW((LPWSTR)c)));
#else
		return (wchar_t)towlower(c);
#endif
	}
	
	const wstring& toLower(const wstring& str, wstring& tmp) throw();
	inline wstring toLower(const wstring& str) throw() {
		wstring tmp;
		return toLower(str, tmp);
	}
	
	const string& toLower(const string& str, string& tmp) throw();
	inline string toLower(const string& str) throw() {
		string tmp;
		return toLower(str, tmp);
	}

	const string& convert(const string& str, string& tmp, const string& fromCharset, const string& toCharset) throw();
	inline string convert(const string& str, const string& fromCharset, const string& toCharset) throw() {
		string tmp;
		return convert(str, tmp, fromCharset, toCharset);
	}

	const string& toUtf8(const string& str, const string& fromCharset, string& tmp) throw();
	inline string toUtf8(const string& str, const string& fromCharset = systemCharset) {
		string tmp;
		return toUtf8(str, fromCharset, tmp);
	}
	
	const string& fromUtf8(const string& str, const string& toCharset, string& tmp) throw();
	inline string fromUtf8(const string& str, const string& toCharset = systemCharset) throw() {
		string tmp;
		return fromUtf8(str, toCharset, tmp);
	}
	
	string toDOS(string tmp);
	wstring toDOS(wstring tmp);
	
	template<typename T>
	tstring tformat(const tstring& src, T t) {
		tstring ret(src.size() + 64, _T('\0'));
		int n = _sntprintf(&ret[0], ret.size(), src.c_str(), t);
		if(n != -1 && n < static_cast<int>(ret.size())) {
			ret.resize(n);
		}
		return ret;
	}
	template<typename T, typename T2, typename T3>
	tstring tformat(const tstring& src, T t, T2 t2, T3 t3) {
		tstring ret(src.size() + 128, _T('\0'));
		int n = _sntprintf(&ret[0], ret.size(), src.c_str(), t, t2, t3);
		if(n != -1 && n < static_cast<int>(ret.size())) {
			ret.resize(n);
		}
		return ret;
	}
}

} // namespace dcpp

#endif
