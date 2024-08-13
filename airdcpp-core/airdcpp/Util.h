/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_UTIL_H
#define DCPLUSPLUS_DCPP_UTIL_H

#include "compiler.h"
#include "constants.h"

#include "Text.h"

namespace dcpp {

#define LIT(x) x, (sizeof(x)-1)

/** Evaluates op(pair<T1, T2>.first, compareTo) */
template<class T1, class T2, class op = equal_to<T1> >
class CompareFirst {
public:
	CompareFirst(const T1& compareTo) : a(compareTo) { }
	bool operator()(const pair<T1, T2>& p) const noexcept { 
		return op()(p.first, a); 
	}
private:
	CompareFirst& operator=(const CompareFirst&) = delete;
	const T1& a;
};

/** Evaluates op(pair<T1, T2>.second, compareTo) */
template<class T1, class T2, class op = equal_to<T2> >
class CompareSecond {
public:
	CompareSecond(const T2& compareTo) : a(compareTo) { }
	bool operator()(const pair<T1, T2>& p) const noexcept { 
		return op()(p.second, a); 
	}
private:
	CompareSecond& operator=(const CompareSecond&) = delete;
	const T2& a;
};

struct pair_to_range_t {
	template <typename I>
	friend constexpr auto operator|(std::pair<I, I> const& pr, pair_to_range_t) {
		return std::ranges::subrange(pr.first, pr.second);
	}
};

inline constexpr pair_to_range_t pair_to_range{};

/** 
 * Compares two values
 * @return -1 if v1 < v2, 0 if v1 == v2 and 1 if v1 > v2
 */
template<typename T1>
inline int compare(const T1& v1, const T1& v2) noexcept { return (v1 < v2) ? -1 : ((v1 == v2) ? 0 : 1); }

/** Uses SFINAE to determine whether a type provides a function; stores the result in "value".
Inspired by <http://stackoverflow.com/a/8752988>. */
#define HAS_FUNC(name, funcRet, funcTest) \
	template<typename HAS_FUNC_T> struct name { \
		typedef char yes[1]; \
		typedef char no[2]; \
		template<typename HAS_FUNC_U> static yes& check( \
		typename std::enable_if<std::is_same<funcRet, decltype(std::declval<HAS_FUNC_U>().funcTest)>::value>::type*); \
		template<typename> static no& check(...); \
		static const bool value = sizeof(check<HAS_FUNC_T>(nullptr)) == sizeof(yes); \
	}

struct DirectoryContentInfo;

class Util  
{
public:
	static tstring emptyStringT;
	static string emptyString;
	static wstring emptyStringW;

	enum SizeUnits {
		B,
		KB,
		MB,
		GB,
		TB,
		PB,
		EB,
		SIZE_LAST
	};


	static int64_t convertSize(int64_t aValue, SizeUnits valueType, SizeUnits to = B) noexcept;

	static string truncate(const string& aStr, int aMaxLength) noexcept;

	template<typename string_t>
	static void replace(const string_t& search, const string_t& replacement, string_t& str) noexcept {
		typename string_t::size_type i = 0;
		while((i = str.find(search, i)) != string_t::npos) {
			str.replace(i, search.size(), replacement);
			i += replacement.size();
		}
	}
	template<typename string_t>
	static inline void replace(const typename string_t::value_type* search, const typename string_t::value_type* replacement, string_t& str) noexcept {
		replace(string_t(search), string_t(replacement), str);
	}

	template<typename T1, typename T2>
	static double countAverage(T1 aFrom, T2 aTotal) {
		return aTotal == 0 ? 0 : (static_cast<double>(aFrom) / static_cast<double>(aTotal));
	}

	template<typename T1, typename T2>
	static int64_t countAverageInt64(T1 aFrom, T2 aTotal) {
		return aTotal == 0 ? 0 : (aFrom / aTotal);
	}

	template<typename T1, typename T2>
	static double countPercentage(T1 aFrom, T2 aTotal) {
		return countAverage<T1, T2>(aFrom, aTotal) * 100.00;
	}

	// Used to parse NMDC-style ip:port combination
	static void parseIpPort(const string& aIpPort, string& ip, string& port) noexcept;

	static string addBrackets(const string& s) noexcept;

	static string formatDirectoryContent(const DirectoryContentInfo& aInfo) noexcept;
	static string formatFileType(const string& aPath) noexcept;

	static string formatBytes(const string& aString) noexcept { return formatBytes(toInt64(aString)); }
	static string formatConnectionSpeed(const string& aString) noexcept { return formatConnectionSpeed(toInt64(aString)); }

	static string getShortTimeString(time_t t = time(NULL) ) noexcept;
	static string getTimeStamp(time_t t = time(NULL) ) noexcept;

	static string getTimeString() noexcept;

	static string getDateTime(time_t t) noexcept;
#ifdef _WIN32
	static wstring getDateTimeW(time_t t) noexcept;
#endif
	static string formatBytes(int64_t aBytes) noexcept;
	static wstring formatBytesW(int64_t aBytes) noexcept;

	static string formatConnectionSpeed(int64_t aBytes) noexcept;
	static wstring formatConnectionSpeedW(int64_t aBytes) noexcept;

	static string formatExactSize(int64_t aBytes) noexcept;
	static wstring formatExactSizeW(int64_t aBytes) noexcept;

	static string formatAbbreviated(int aNum) noexcept;
	static wstring formatAbbreviatedW(int aNum) noexcept;

	static wstring formatSecondsW(int64_t aSec, bool supressHours = false) noexcept;
	static string formatSeconds(int64_t aSec, bool supressHours = false) noexcept;

	typedef string (*FilterF)(const string&);

	// Set aTime to 0 to avoid formating of time variable
	static string formatParams(const string& msg, const ParamMap& params, FilterF filter = nullptr, time_t aTime = time(NULL)) noexcept;

	static string formatTime(const string &msg, const time_t t) noexcept;

	static inline int64_t roundDown(int64_t size, int64_t blockSize) noexcept {
		return ((size + blockSize / 2) / blockSize) * blockSize;
	}

	static inline int64_t roundUp(int64_t size, int64_t blockSize) noexcept {
		return ((size + blockSize - 1) / blockSize) * blockSize;
	}

	static inline int roundDown(int size, int blockSize) noexcept {
		return ((size + blockSize / 2) / blockSize) * blockSize;
	}

	static inline int roundUp(int size, int blockSize) noexcept {
		return ((size + blockSize - 1) / blockSize) * blockSize;
	}

	static string formatTime(uint64_t aSec, bool aTranslate, bool aPerMinute = false) noexcept;

	static int DefaultSort(const char* a, const char* b) noexcept;
	static int DefaultSort(const wchar_t* a, const wchar_t* b) noexcept;
	inline static int DefaultSort(const string& a, const string& b) noexcept {
		return DefaultSort(a.c_str(), b.c_str());
	}

	static int64_t toInt64(const string& aString) noexcept {
#ifdef _WIN32
		return _atoi64(aString.c_str());
#else
		return strtoll(aString.c_str(), (char **)NULL, 10);
#endif
	}

	static time_t toTimeT(const string& aString) noexcept {
		return static_cast<time_t>(toInt64(aString));
	}
	static time_t parseRemoteFileItemDate(const string& aString) noexcept;

	static int toInt(const string& aString) noexcept {
		return atoi(aString.c_str());
	}
	static uint32_t toUInt32(const string& str) noexcept {
		return toUInt32(str.c_str());
	}
	static uint32_t toUInt32(const char* c) noexcept {
#ifdef _MSC_VER
		/*
		* MSVC's atoi returns INT_MIN/INT_MAX if out-of-range; hence, a number
		* between INT_MAX and UINT_MAX can't be converted back to uint32_t.
		*/
		uint32_t ret = atoi(c);
		if(errno == ERANGE)
			return (uint32_t)_atoi64(c);
		return ret;
#else
		return (uint32_t)atoi(c);
#endif
	}
	
	static unsigned toUInt(const string& s) noexcept {
		if(s.empty())
			return 0;
		int ret = toInt(s);
		if(ret < 0)
			return 0;
		return ret;
	}

	static double toDouble(const string& aString) noexcept {
		// Work-around for atof and locales...
		lconv* lv = localeconv();
		string::size_type i = aString.find_last_of(".,");
		if(i != string::npos && aString[i] != lv->decimal_point[0]) {
			string tmp(aString);
			tmp[i] = lv->decimal_point[0];
			return atof(tmp.c_str());
		}
		return atof(aString.c_str());
	}

	static float toFloat(const string& aString) noexcept {
		return (float)toDouble(aString);
	}

	static string toString(short val) noexcept {
		char buf[8];
		snprintf(buf, sizeof(buf), "%d", (int)val);
		return buf;
	}
	static string toString(unsigned short val) noexcept {
		char buf[8];
		snprintf(buf, sizeof(buf), "%u", (unsigned int)val);
		return buf;
	}
	static string toString(int val) noexcept {
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", val);
		return buf;
	}
	static string toString(unsigned int val) noexcept {
		char buf[16];
		snprintf(buf, sizeof(buf), "%u", val);
		return buf;
	}
	static string toString(long val) noexcept {
		char buf[32];
		snprintf(buf, sizeof(buf), "%ld", val);
		return buf;
	}
	static string toString(unsigned long val) noexcept {
		char buf[32];
		snprintf(buf, sizeof(buf), "%lu", val);
		return buf;
	}
	static string toString(long long val) noexcept {
		char buf[32];
		snprintf(buf, sizeof(buf), "%lld", val);
		return buf;
	}
	static string toString(unsigned long long val) noexcept {
		char buf[32];
		snprintf(buf, sizeof(buf), "%llu", val);
		return buf;
	}
	static string toString(double val) noexcept {
		char buf[16];
		snprintf(buf, sizeof(buf), "%0.2f", val);
		return buf;
	}

	static string toString(const string& sep, const StringList& lst) noexcept;

	template<typename T, class NameOperator>
	static string listToStringT(const T& lst, bool forceBrackets, bool squareBrackets) noexcept {
		if(lst.size() == 1 && !forceBrackets)
			return NameOperator()(*lst.begin());

		string tmp;
		tmp.push_back(squareBrackets ? '[' : '(');
		for(auto i = lst.begin(), iend = lst.end(); i != iend; ++i) {
			tmp += NameOperator()(*i);
			tmp += ", ";
		}

		if(tmp.length() == 1) {
			tmp.push_back(squareBrackets ? ']' : ')');
		} else {
			tmp.pop_back();
			tmp[tmp.length()-1] = squareBrackets ? ']' : ')';
		}

		return tmp;
	}

	struct StrChar {
		const char* operator()(const string& u) noexcept { return u.c_str(); }
	};

	template<typename ListT>
	static string listToString(const ListT& lst) noexcept { return listToStringT<ListT, StrChar>(lst, false, true); }

#ifdef WIN32
	static wstring toStringW( int32_t val ) noexcept {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), L"%ld", val);
		return buf;
	}

	static wstring toStringW( uint32_t val ) noexcept {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), L"%d", val);
		return buf;
	}
	
	static wstring toStringW( DWORD val ) noexcept {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), L"%d", val);
		return buf;
	}
	
	static wstring toStringW( int64_t val ) noexcept {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), _T(I64_FMT), val);
		return buf;
	}

	static wstring toStringW( uint64_t val ) noexcept {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), _T(I64_FMT), val);
		return buf;
	}

	static wstring toStringW( double val ) noexcept {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), L"%0.2f", val);
		return buf;
	}
#endif
	static string toHexEscape(char val) noexcept {
		char buf[sizeof(int)*2+1+1];
		snprintf(buf, sizeof(buf), "%%%X", val&0x0FF);
		return buf;
	}
	static char fromHexEscape(const string& aString) noexcept {
		unsigned int res = 0;
		sscanf(aString.c_str(), "%X", &res);
		return static_cast<char>(res);
	}

	template<typename T>
	static T& intersect(T& t1, const T& t2) noexcept {
		for(typename T::iterator i = t1.begin(); i != t1.end();) {
			if(find_if(t2.begin(), t2.end(), bind1st(equal_to<typename T::value_type>(), *i)) == t2.end())
				i = t1.erase(i);
			else
				++i;
		}
		return t1;
	}

	template<typename T>
	static bool hasCommonElements(const T& a, const T& b) noexcept {
		return ranges::find_if(a, [&b](auto value) {
			return ranges::find(b, value) != b.end();
		}) != a.end();
	}

	template<typename T>
	static void concatenate(T& a, const T& toAdd) noexcept {
		std::copy(toAdd.begin(), toAdd.end(), std::back_inserter(a));
	}

	/**
	 * Case insensitive substring search.
	 * @return First position found or string::npos
	 */
	static string::size_type findSubString(const string& aString, const string& aSubString, string::size_type start = 0) noexcept;
	static wstring::size_type findSubString(const wstring& aString, const wstring& aSubString, wstring::size_type start = 0) noexcept;

	/* Utf-8 versions of strnicmp and stricmp, unicode char code order (!) */
	static int stricmp(const char* a, const char* b) noexcept;
	static int strnicmp(const char* a, const char* b, size_t n) noexcept;

	static int stricmp(const wchar_t* a, const wchar_t* b) noexcept {
		while(*a && Text::toLower(*a) == Text::toLower(*b))
			++a, ++b;
		return ((int)Text::toLower(*a)) - ((int)Text::toLower(*b));
	}
	static int strnicmp(const wchar_t* a, const wchar_t* b, size_t n) noexcept {
		while(n && *a && Text::toLower(*a) == Text::toLower(*b))
			--n, ++a, ++b;

		return n == 0 ? 0 : ((int)Text::toLower(*a)) - ((int)Text::toLower(*b));
	}

	static int stricmp(const string& a, const string& b) noexcept { return stricmp(a.c_str(), b.c_str()); }
	static int strnicmp(const string& a, const string& b, size_t n) noexcept { return strnicmp(a.c_str(), b.c_str(), n); }
	static int stricmp(const wstring& a, const wstring& b) noexcept { return stricmp(a.c_str(), b.c_str()); }
	static int strnicmp(const wstring& a, const wstring& b, size_t n) noexcept { return strnicmp(a.c_str(), b.c_str(), n); }
	
	static void replace(string& aString, const string& findStr, const string& replaceStr) noexcept;
	static tstring replaceT(const tstring& aString, const tstring& fStr, const tstring& rStr) noexcept;

	static bool toBool(const int aNumber) noexcept {
		return (aNumber > 0 ? true : false);
	}
	
	static string base64_encode(unsigned char const*, unsigned int len) noexcept;
    static string base64_decode(string const& s) noexcept;

	static bool isChatCommand(const string& aText) noexcept;
};
	
class StringPtrHash {
public:
	size_t operator() (const string* s) const noexcept {
		return std::hash<std::string>()(*s);
	}
};

class StringPtrEq {
public:
	bool operator()(const string* a, const string* b) const noexcept {
		return *a == *b;
	}
};

class StringPtrLess {
public:
	bool operator()(const string* a, const string* b) const noexcept {
		return compare(*a, *b) < 0;
	}
};

/** Case insensitive hash function for strings */
struct noCaseStringHash {
	size_t operator()(const string* s) const noexcept {
		return operator()(*s);
	}

	size_t operator()(const string& s) const noexcept {
		size_t x = 0;
		const char* end = s.data() + s.size();
		for(const char* str = s.data(); str < end; ) {
			wchar_t c = 0;
			int n = Text::utf8ToWc(str, c);
			if(n < 0) {
				x = x*32 - x + '_';
				str += abs(n);
			} else {
				x = x*32 - x + (size_t)Text::toLower(c);
				str += n;
			}
		}
		return x;
	}

	size_t operator()(const wstring* s) const noexcept {
		return operator()(*s);
	}
	size_t operator()(const wstring& s) const noexcept {
		size_t x = 0;
		const wchar_t* y = s.data();
		wstring::size_type j = s.size();
		for(wstring::size_type i = 0; i < j; ++i) {
			x = x*31 + (size_t)Text::toLower(y[i]);
		}
		return x;
	}

	bool operator()(const string* a, const string* b) const noexcept {
		return Util::stricmp(*a, *b) < 0;
	}
	bool operator()(const string& a, const string& b) const noexcept {
		return Util::stricmp(a, b) < 0;
	}
	bool operator()(const wstring* a, const wstring* b) const noexcept {
		return Util::stricmp(*a, *b) < 0;
	}
	bool operator()(const wstring& a, const wstring& b) const noexcept {
		return Util::stricmp(a, b) < 0;
	}
};

/** Case insensitive string comparison */
struct noCaseStringEq {
	bool operator()(const string* a, const string* b) const noexcept {
		return a == b || Util::stricmp(*a, *b) == 0;
	}
	bool operator()(const string& a, const string& b) const noexcept {
		return Util::stricmp(a, b) == 0;
	}
	bool operator()(const wstring* a, const wstring* b) const noexcept {
		return a == b || Util::stricmp(*a, *b) == 0;
	}
	bool operator()(const wstring& a, const wstring& b) const noexcept {
		return Util::stricmp(a, b) == 0;
	}
};

/** Case insensitive string ordering */
struct noCaseStringLess {
	bool operator()(const string* a, const string* b) const noexcept {
		return Util::stricmp(*a, *b) < 0;
	}
	bool operator()(const string& a, const string& b) const noexcept {
		return Util::stricmp(a, b) < 0;
	}
	bool operator()(const wstring* a, const wstring* b) const noexcept {
		return Util::stricmp(*a, *b) < 0;
	}
	bool operator()(const wstring& a, const wstring& b) const noexcept {
		return Util::stricmp(a, b) < 0;
	}
};

/* Case insensitive string comparison classes */
class Stricmp {
public:
	Stricmp(const string& compareTo) : a(compareTo) { }
	bool operator()(const string& p) const noexcept {
		return Util::stricmp(p.c_str(), a.c_str()) == 0; 
	}
private:
	Stricmp& operator=(const Stricmp&) = delete;
	const string& a;
};

class StricmpT {
public:
	StricmpT(const wstring& compareTo) : a(compareTo) { }
	bool operator()(const wstring& p) const noexcept { 
		return Util::stricmp(p.c_str(), a.c_str()) == 0; 
	}
private:
	StricmpT& operator=(const StricmpT&) = delete;
	const wstring& a;
};

struct Compare {
	int operator()(const string& a, const string& b) const noexcept {
		return a.compare(b);
	}
};

} // namespace dcpp

#endif // !defined(UTIL_H)
