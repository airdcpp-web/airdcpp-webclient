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

#ifndef DCPLUSPLUS_DCPP_UTIL_H
#define DCPLUSPLUS_DCPP_UTIL_H

#include "compiler.h"

# define SP_HIDDEN 1

#ifdef _WIN32

# define PATH_SEPARATOR '\\'
# define PATH_SEPARATOR_STR "\\"

#else

# define PATH_SEPARATOR '/'
# define PATH_SEPARATOR_STR "/"

#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#endif

#include "Text.h"

namespace dcpp {

#define LIT(x) x, (sizeof(x)-1)

/** Evaluates op(pair<T1, T2>.first, compareTo) */
template<class T1, class T2, class op = equal_to<T1> >
class CompareFirst {
public:
	CompareFirst(const T1& compareTo) : a(compareTo) { }
	bool operator()(const pair<T1, T2>& p) { return op()(p.first, a); }
private:
	CompareFirst& operator=(const CompareFirst&);
	const T1& a;
};

/** Evaluates op(pair<T1, T2>.second, compareTo) */
template<class T1, class T2, class op = equal_to<T2> >
class CompareSecond {
public:
	CompareSecond(const T2& compareTo) : a(compareTo) { }
	bool operator()(const pair<T1, T2>& p) { return op()(p.second, a); }
private:
	CompareSecond& operator=(const CompareSecond&);
	const T2& a;
};

/** 
 * Compares two values
 * @return -1 if v1 < v2, 0 if v1 == v2 and 1 if v1 > v2
 */
template<typename T1>
inline int compare(const T1& v1, const T1& v2) { return (v1 < v2) ? -1 : ((v1 == v2) ? 0 : 1); }

typedef std::function<void (const string&)> StepFunction;
typedef std::function<bool (const string& /*Message*/, bool /*isQuestion*/, bool /*isError*/)> MessageFunction;
typedef std::function<void (float)> ProgressFunction;

class Util  
{
public:
	struct PathSortOrderInt {
		int operator()(const string& left, const string& right) const {
			auto comp = compare(Util::getFilePath(left), Util::getFilePath(right));
			if (comp == 0) {
				return compare(left, right);
			}
			return comp;
		}
	};

	struct PathSortOrderBool {
		bool operator()(const string& left, const string& right) const {
			auto comp = compare(Util::getFilePath(left), Util::getFilePath(right));
			if (comp == 0) {
				return compare(left, right) < 0;
			}
			return comp < 0;
		}
	};

	static tstring emptyStringT;
	static string emptyString;
	static wstring emptyStringW;
	static string getOsVersion(bool http = false);
	static bool IsOSVersionOrGreater(int major, int minor);

	enum Paths {
		/** Global configuration */
		PATH_GLOBAL_CONFIG,
		/** Per-user configuration (queue, favorites, ...) */
		PATH_USER_CONFIG,
		/** Language files */
		PATH_USER_LOCAL,
		/** Various resources (help files etc) */
		PATH_RESOURCES,
		/** Translations */
		PATH_LOCALE,
		/** Default download location */
		PATH_DOWNLOADS,
		/** Default file list location */
		PATH_FILE_LISTS,
		/** Default hub list cache */
		PATH_HUB_LISTS,
		/** Where the notepad file is stored */
		PATH_NOTEPAD,
		/** Folder with emoticons packs*/
		PATH_EMOPACKS,
		/** XML files for each bundle*/
		PATH_BUNDLES,
		/** XML files for each bundle*/
		PATH_SHARECACHE,
		/** Path to Theme Files*/
		PATH_THEMES,
		PATH_LAST
	};

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


	static int64_t convertSize(int64_t aValue, SizeUnits valueType, SizeUnits to = B);

	static void initialize();
	static string getAppName();

	/** Path of temporary storage */
	static string getTempPath();

	static string getOpenPath();
	static string getOpenPath(const string& aFileName);

	/** Path of configuration files */
	static const string& getPath(Paths path) { return paths[path]; }

	/** Migrate from pre-localmode config location */
	static void migrate(const string& file);
	static void migrate(const string& aDir, const string& aPattern);

	/** Path of file lists */
	static string getListPath() { return getPath(PATH_FILE_LISTS); }
	/** Path of hub lists */
	static string getHubListsPath() { return getPath(PATH_HUB_LISTS); }
	/** Notepad filename */
	static string getNotepadFile() { return getPath(PATH_NOTEPAD); }
	/** Path of bundles */
	static string getBundlePath() { return getPath(PATH_BUNDLES); }

	static string translateError(int aError);

	static time_t getStartTime() { return startTime; }
	static long getUptime() { return mUptimeSeconds; }
	static void increaseUptime() { mUptimeSeconds++; }

	static string getFilePath(const string& path, const char separator = PATH_SEPARATOR);
	inline static string getNmdcFilePath(const string& path) { return getFilePath(path, '\\'); }
	inline static string getAdcFilePath(const string& path) { return getFilePath(path, '/'); }

	static string getFileName(const string& path, const char separator = PATH_SEPARATOR);
	inline static string getNmdcFileName(const string& path) { return getFileName(path, '\\'); };
	inline static string getAdcFileName(const string& path) { return getFileName(path, '/'); };

	static string getLastDir(const string& path, const char separator = PATH_SEPARATOR);
	inline static string getNmdcLastDir(const string& path) { return getLastDir(path, '\\'); };
	inline static string getAdcLastDir(const string& path) { return getLastDir(path, '/'); };

	static string getParentDir(const string& path, const char separator = PATH_SEPARATOR, bool allowEmpty = false);
	inline static string getNmdcParentDir(const string& path) { return getParentDir(path, '\\', true); };
	inline static string getAdcParentDir(const string& path) { return getParentDir(path, '/', false); };

	static string getFileExt(const string& path);

	static wstring getFilePath(const wstring& path);
	static wstring getFileName(const wstring& path);
	static wstring getFileExt(const wstring& path);
	static wstring getLastDir(const wstring& path);

	template<typename string_t>
	static void replace(const string_t& search, const string_t& replacement, string_t& str) {
		typename string_t::size_type i = 0;
		while((i = str.find(search, i)) != string_t::npos) {
			str.replace(i, search.size(), replacement);
			i += replacement.size();
		}
	}
	template<typename string_t>
	static inline void replace(const typename string_t::value_type* search, const typename string_t::value_type* replacement, string_t& str) {
		replace(string_t(search), string_t(replacement), str);
	}

	static void sanitizeUrl(string& url);
	static void decodeUrl(const string& aUrl, string& protocol, string& host, string& port, string& path, string& query, string& fragment);
	static map<string, string> decodeQuery(const string& query);

	static bool isPathValid(const string& sPath);
	static inline string validatePath(const string& aPath, bool requireEndSeparator = false) {
		auto path = cleanPathChars(aPath, false);
		if (requireEndSeparator && !path.empty() && path.back() != PATH_SEPARATOR) {
			path += PATH_SEPARATOR;
		}
		return path; 
	}
	static inline string validateFileName(const string& aFileName) { return cleanPathChars(aFileName, true); }
	static bool checkExtension(const string& tmp);

	static string addBrackets(const string& s);

	static string formatBytes(const string& aString) { return formatBytes(toInt64(aString)); }
	static string formatConnectionSpeed(const string& aString) { return formatConnectionSpeed(toInt64(aString)); }

	static string getShortTimeString(time_t t = time(NULL) );
	static string getTimeStamp(time_t t = time(NULL) );

	static string getTimeString();

	static string getDateTime(time_t t);
#ifdef _WIN32
	static wstring getDateTimeW(time_t t);
#endif
	static string toAdcFile(const string& file);
	static string toNmdcFile(const string& file);
	
	static string formatBytes(int64_t aBytes);
	static wstring formatBytesW(int64_t aBytes);

	static string formatConnectionSpeed(int64_t aBytes);
	static wstring formatConnectionSpeedW(int64_t aBytes);

	static string formatExactSize(int64_t aBytes);
	static wstring formatExactSizeW(int64_t aBytes);

	static wstring formatSecondsW(int64_t aSec, bool supressHours = false);
	static string formatSeconds(int64_t aSec, bool supressHours = false);

	typedef string (*FilterF)(const string&);
	static string formatParams(const string& msg, const ParamMap& params, FilterF filter = 0);

	static string formatTime(const string &msg, const time_t t);

	static inline int64_t roundDown(int64_t size, int64_t blockSize) {
		return ((size + blockSize / 2) / blockSize) * blockSize;
	}

	static inline int64_t roundUp(int64_t size, int64_t blockSize) {
		return ((size + blockSize - 1) / blockSize) * blockSize;
	}

	static inline int roundDown(int size, int blockSize) {
		return ((size + blockSize / 2) / blockSize) * blockSize;
	}

	static inline int roundUp(int size, int blockSize) {
		return ((size + blockSize - 1) / blockSize) * blockSize;
	}

	inline static string FormatPath(const string& path) {
#ifdef _WIN32
		//dont format unless its needed, xp works slower with these so.
		//also we want to limit the unc path lower, no point on endless paths.
		if(path.size() < 250 || path.size() > UNC_MAX_PATH) 
			return path;

		string temp;
		if ((path[0] == '\\') & (path[1] == '\\'))
			temp = "\\\\?\\UNC\\" + path.substr(2);
		else
			temp = "\\\\?\\" + path;
		return temp;
#else
		return path;
#endif
	}
		
	inline static tstring FormatPathT(const tstring& path) {
#ifdef _WIN32
		//dont format unless its needed, xp works slower with these so.
		//also we want to limit the unc path lower, no point on endless paths. 
		if(path.size() < 250 || path.size() > UNC_MAX_PATH) 
			return path;

		tstring temp;
		if ((path[0] == '\\') & (path[1] == '\\'))
			temp = _T("\\\\?\\UNC\\") + path.substr(2);
		else
			temp = _T("\\\\?\\") + path;
		return temp;
#else
		return path;
#endif
	}

	static string formatTime(int64_t aSec, bool translate, bool perMinute = false);

	static int DefaultSort(const wchar_t* a, const wchar_t* b, bool noCase = true);

	static int64_t toInt64(const string& aString) {
#ifdef _WIN32
		return _atoi64(aString.c_str());
#else
		return strtoll(aString.c_str(), (char **)NULL, 10);
#endif
	}

	static int toInt(const string& aString) {
		return atoi(aString.c_str());
	}
	static uint32_t toUInt32(const string& str) {
		return toUInt32(str.c_str());
	}
	static uint32_t toUInt32(const char* c) {
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
	
	static unsigned toUInt(const string& s) {
		if(s.empty())
			return 0;
		int ret = toInt(s);
		if(ret < 0)
			return 0;
		return ret;
	}

	static double toDouble(const string& aString) {
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

	static float toFloat(const string& aString) {
		return (float)toDouble(aString.c_str());
	}

	static string toString(short val) {
		char buf[8];
		snprintf(buf, sizeof(buf), "%d", (int)val);
		return buf;
	}
	static string toString(unsigned short val) {
		char buf[8];
		snprintf(buf, sizeof(buf), "%u", (unsigned int)val);
		return buf;
	}
	static string toString(int val) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", val);
		return buf;
	}
	static string toString(unsigned int val) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%u", val);
		return buf;
	}
	static string toString(long val) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%ld", val);
		return buf;
	}
	static string toString(unsigned long val) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%lu", val);
		return buf;
	}
	static string toString(long long val) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%lld", val);
		return buf;
	}
	static string toString(unsigned long long val) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%llu", val);
		return buf;
	}
	static string toString(double val) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%0.2f", val);
		return buf;
	}

	static string toString(const string& sep, const StringList& lst);

	template<typename T, class NameOperator>
	static string listToStringT(const T& lst, bool forceBrackets, bool squareBrackets) {
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
		const char* operator()(const string& u) { return u.c_str(); }
	};

	template<typename ListT>
	static string listToString(const ListT& lst) { return listToStringT<ListT, StrChar>(lst, false, true); }

#ifdef WIN32
	static wstring toStringW( int32_t val ) {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), L"%ld", val);
		return buf;
	}

	static wstring toStringW( uint32_t val ) {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), L"%d", val);
		return buf;
	}
	
	static wstring toStringW( DWORD val ) {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), L"%d", val);
		return buf;
	}
	
	static wstring toStringW( int64_t val ) {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), _T(I64_FMT), val);
		return buf;
	}

	static wstring toStringW( uint64_t val ) {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), _T(I64_FMT), val);
		return buf;
	}

	static wstring toStringW( double val ) {
		wchar_t buf[32];
		snwprintf(buf, sizeof(buf), L"%0.2f", val);
		return buf;
	}
#endif

	static bool isNumeric(wchar_t c) {
		return (c >= '0' && c <= '9') ? true : false;
	}

	static string toHexEscape(char val) {
		char buf[sizeof(int)*2+1+1];
		snprintf(buf, sizeof(buf), "%%%X", val&0x0FF);
		return buf;
	}
	static char fromHexEscape(const string aString) {
		unsigned int res = 0;
		sscanf(aString.c_str(), "%X", &res);
		return static_cast<char>(res);
	}

	template<typename T>
	static T& intersect(T& t1, const T& t2) {
		for(typename T::iterator i = t1.begin(); i != t1.end();) {
			if(find_if(t2.begin(), t2.end(), bind1st(equal_to<typename T::value_type>(), *i)) == t2.end())
				i = t1.erase(i);
			else
				++i;
		}
		return t1;
	}

	static string encodeURI(const string& /*aString*/, bool reverse = false);
	
	static bool isPrivateIp(const string& ip, bool v6);
	/**
	 * Case insensitive substring search.
	 * @return First position found or string::npos
	 */
	static string::size_type findSubString(const string& aString, const string& aSubString, string::size_type start = 0) noexcept;
	static wstring::size_type findSubString(const wstring& aString, const wstring& aSubString, wstring::size_type start = 0) noexcept;

	/* Utf-8 versions of strnicmp and stricmp, unicode char code order (!) */
	static int stricmp(const char* a, const char* b);
	static int strnicmp(const char* a, const char* b, size_t n);

	static int stricmp(const wchar_t* a, const wchar_t* b) {
		while(*a && Text::toLower(*a) == Text::toLower(*b))
			++a, ++b;
		return ((int)Text::toLower(*a)) - ((int)Text::toLower(*b));
	}
	static int strnicmp(const wchar_t* a, const wchar_t* b, size_t n) {
		while(n && *a && Text::toLower(*a) == Text::toLower(*b))
			--n, ++a, ++b;

		return n == 0 ? 0 : ((int)Text::toLower(*a)) - ((int)Text::toLower(*b));
	}

	static int stricmp(const string& a, const string& b) { return stricmp(a.c_str(), b.c_str()); }
	static int strnicmp(const string& a, const string& b, size_t n) { return strnicmp(a.c_str(), b.c_str(), n); }
	static int stricmp(const wstring& a, const wstring& b) { return stricmp(a.c_str(), b.c_str()); }
	static int strnicmp(const wstring& a, const wstring& b, size_t n) { return strnicmp(a.c_str(), b.c_str(), n); }
	
	static void replace(string& aString, const string& findStr, const string& replaceStr);
	static tstring replaceT(const tstring& aString, const tstring& fStr, const tstring& rStr);

	static bool toBool(const int aNumber) {
		return (aNumber > 0 ? true : false);
	}
	
	static string base64_encode(unsigned char const*, unsigned int len);
    static string base64_decode(string const& s);

	static bool fileExists(const string &aFile);

	static int randInt(int min=0, int max=std::numeric_limits<int>::max());
	static uint32_t rand();
	static uint32_t rand(uint32_t high) { return rand() % high; }
	static uint32_t rand(uint32_t low, uint32_t high) { return rand(high-low) + low; }
	static double randd() { return ((double)rand()) / ((double)0xffffffff); }

	static bool hasParam(const string& aParam);
	static string getParams(bool isFirst);
	static void addParam(const string& aParam);
	static optional<string> getParam(const string& aKey);

	static bool usingLocalMode() { return localMode; }
	static bool wasUncleanShutdown;
private:
	static string cleanPathChars(string aPath, bool isFileName);

	/** In local mode, all config and temp files are kept in the same dir as the executable */
	static bool localMode;

	static string paths[PATH_LAST];

	static StringList params;

	static time_t startTime;
	
	static void loadBootConfig();
	
	static long mUptimeSeconds;
	static int osMinor;
	static int osMajor;
};
	
/** Case insensitive hash function for strings */
struct noCaseStringHash {
	size_t operator()(const string* s) const {
		return operator()(*s);
	}

	size_t operator()(const string& s) const {
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

	size_t operator()(const wstring* s) const {
		return operator()(*s);
	}
	size_t operator()(const wstring& s) const {
		size_t x = 0;
		const wchar_t* y = s.data();
		wstring::size_type j = s.size();
		for(wstring::size_type i = 0; i < j; ++i) {
			x = x*31 + (size_t)Text::toLower(y[i]);
		}
		return x;
	}

	bool operator()(const string* a, const string* b) const {
		return Util::stricmp(*a, *b) < 0;
	}
	bool operator()(const string& a, const string& b) const {
		return Util::stricmp(a, b) < 0;
	}
	bool operator()(const wstring* a, const wstring* b) const {
		return Util::stricmp(*a, *b) < 0;
	}
	bool operator()(const wstring& a, const wstring& b) const {
		return Util::stricmp(a, b) < 0;
	}
};

/** Case insensitive string comparison */
struct noCaseStringEq {
	bool operator()(const string* a, const string* b) const {
		return a == b || Util::stricmp(*a, *b) == 0;
	}
	bool operator()(const string& a, const string& b) const {
		return Util::stricmp(a, b) == 0;
	}
	bool operator()(const wstring* a, const wstring* b) const {
		return a == b || Util::stricmp(*a, *b) == 0;
	}
	bool operator()(const wstring& a, const wstring& b) const {
		return Util::stricmp(a, b) == 0;
	}
};

/** Case insensitive string ordering */
struct noCaseStringLess {
	bool operator()(const string* a, const string* b) const {
		return Util::stricmp(*a, *b) < 0;
	}
	bool operator()(const string& a, const string& b) const {
		return Util::stricmp(a, b) < 0;
	}
	bool operator()(const wstring* a, const wstring* b) const {
		return Util::stricmp(*a, *b) < 0;
	}
	bool operator()(const wstring& a, const wstring& b) const {
		return Util::stricmp(a, b) < 0;
	}
};

/* Case insensitive string comparison classes */
class Stricmp {
public:
	Stricmp(const string& compareTo) : a(compareTo) { }
	bool operator()(const string& p) { return Util::stricmp(p.c_str(), a.c_str()) == 0; }
private:
	Stricmp& operator=(const Stricmp&);
	const string& a;
};

class StricmpT {
public:
	StricmpT(const wstring& compareTo) : a(compareTo) { }
	bool operator()(const wstring& p) { return Util::stricmp(p.c_str(), a.c_str()) == 0; }
private:
	StricmpT& operator=(const StricmpT&);
	const wstring& a;
};

struct Compare {
	int operator()(const string& a, const string& b) const {
		return a.compare(b);
	}
};

} // namespace dcpp

#endif // !defined(UTIL_H)
