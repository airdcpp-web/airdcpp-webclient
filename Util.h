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

#ifndef DCPLUSPLUS_DCPP_UTIL_H
#define DCPLUSPLUS_DCPP_UTIL_H

#include "compiler.h"

# define SP_DEFAULT 0
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

/** Evaluates op(pair<T1, T2>.second, compareTo) */
template<class T1, class T2, class T3, class op = equal_to<T2> >
class CompareSecondFirst {
public:
	CompareSecondFirst(const T2& compareTo) : a(compareTo) { }
	bool operator()(const pair<T1, pair<T2, T3>>& p) { return op()(p.second.first, a); }
private:
	CompareSecondFirst& operator=(const CompareSecondFirst&);
	const T2& a;
};


/* Case insensitive string comparison classes */
class Stricmp {
public:
	Stricmp(const string& compareTo) : a(compareTo) { }
	bool operator()(const string& p) { return stricmp(p.c_str(), a.c_str()) == 0; }
private:
	Stricmp& operator=(const Stricmp&);
	const string& a;
};

class StricmpT {
public:
	StricmpT(const wstring& compareTo) : a(compareTo) { }
	bool operator()(const wstring& p) { return stricmp(p.c_str(), a.c_str()) == 0; }
private:
	StricmpT& operator=(const StricmpT&);
	const wstring& a;
};

/** 
 * Compares two values
 * @return -1 if v1 < v2, 0 if v1 == v2 and 1 if v1 > v2
 */
template<typename T1>
inline int compare(const T1& v1, const T1& v2) { return (v1 < v2) ? -1 : ((v1 == v2) ? 0 : 1); }

class Util  
{
public:
	static tstring emptyStringT;
	static string emptyString;
	static wstring emptyStringW;
	static string getOsVersion(bool http = false, bool doubleStr = false);

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
		/** Path to Theme Files*/
		PATH_THEMES,
		PATH_LAST
	};

	static void initialize();
	static string getAppName();

	/** Path of temporary storage */
	static string getTempPath();

	static string getOpenPath();
	static string getOpenPath(const string& aFileName);
	static void openFile(const string& aPath);

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

	static string getFilePath(const string& path);
	static string getFileName(const string& path);
	static string getFileExt(const string& path);
	static string getLastDir(const string& path);

	static string getParentDir(const string& path, bool allowEmpty = false);

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

	static string validateFileName(string aFile);
	static bool checkExtension(const string& tmp);
	static string cleanPathChars(const string& str);
	static string addBrackets(const string& s);

	static string formatStatus(int iStatus);
	
	static string formatBytes(const string& aString) { return formatBytes(toInt64(aString)); }

static string getShortTimeString(time_t t = time(NULL) );
	static string getTimeStamp(time_t t = time(NULL) );

	static string getTimeString();
	static string getDateTime(time_t t);
	static string toAdcFile(const string& file);
	static string toNmdcFile(const string& file);
	
	static string formatBytes(int64_t aBytes);
	static wstring formatBytesW(int64_t aBytes);

	static wstring formatExactSize(int64_t aBytes);

	static wstring formatSecondsW(int64_t aSec, bool supressHours = false);
	static string formatSeconds(int64_t aSec, bool supressHours = false);

	typedef string (*FilterF)(const string&);
	static string formatParams(const string& msg, const ParamMap& params, FilterF filter = 0);

	static string formatTime(const string &msg, const time_t t);
	static string formatRegExp(const string& msg, const StringMap& params);

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
	}
		
	inline static tstring FormatPath(const tstring& path) {
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
		snprintf(buf, sizeof(buf), I64_FMT, val);
		return buf;
	}
	static string toString(unsigned long long val) {
		char buf[32];
		snprintf(buf, sizeof(buf), U64_FMT, val);
		return buf;
	}
	static string toString(double val) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%0.2f", val);
		return buf;
	}

	static string toString(const string& sep, const StringList& lst);

	template<typename T>
	static string listToString(const T& lst) {
		if(lst.size() == 1)
			return *lst.begin();
		string tmp("[");
		for(auto i = lst.begin(), iend = lst.end(); i != iend; ++i) {
			tmp += *i + ',';
		}
		if(tmp.length() == 1)
			tmp.push_back(']');
		else
			tmp[tmp.length()-1] = ']';
		return tmp;
	}

	static string toString(const StringSet& lst) { return listToString<StringSet>(lst); }
	static string toString(const StringList& lst) { return listToString<StringList>(lst); }
	static string toString(const OrderedStringSet& lst) { return listToString<OrderedStringSet>(lst); }

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

	static bool isNumeric(wchar_t c) {
		return (c >= '0' && c <= '9') ? true : false;
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
	
	static bool isPrivateIp(string const& ip);
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
	static TCHAR* strstr(const TCHAR *str1, const TCHAR *str2, int *pnIdxFound);
	static tstring replace(const tstring& aString, const tstring& fStr, const tstring& rStr);

	static bool toBool(const int aNumber) {
		return (aNumber > 0 ? true : false);
	}
	
	static string base64_encode(unsigned char const*, unsigned int len);
    static string base64_decode(string const& s);

	static uint64_t getDirSize(const string &sFullPath);
	static bool validatePath(const string &sPath);
	static bool fileExists(const string &aFile);
	static string getReleaseDir(const string& dir, bool cut);

	static int randInt(int min=0, int max=std::numeric_limits<int>::max());
	static uint32_t rand();
	static uint32_t rand(uint32_t high) { return rand() % high; }
	static uint32_t rand(uint32_t low, uint32_t high) { return rand(high-low) + low; }
	static double randd() { return ((double)rand()) / ((double)0xffffffff); }

	static bool hasParam(const string& aParam);
	static tstring getParams(bool isFirst);
	static void addParam(const string& aParam);
private:
	/** In local mode, all config and temp files are kept in the same dir as the executable */
	static bool localMode;

	static string paths[PATH_LAST];

	static StringList params;

	static time_t startTime;
	
	static void loadBootConfig();
	
	static long mUptimeSeconds;
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
		return stricmp(*a, *b) < 0;
	}
	bool operator()(const string& a, const string& b) const {
		return stricmp(a, b) < 0;
	}
	bool operator()(const wstring* a, const wstring* b) const {
		return stricmp(*a, *b) < 0;
	}
	bool operator()(const wstring& a, const wstring& b) const {
		return stricmp(a, b) < 0;
	}
};

/** Case insensitive string comparison */
struct noCaseStringEq {
	bool operator()(const string* a, const string* b) const {
		return a == b || stricmp(*a, *b) == 0;
	}
	bool operator()(const string& a, const string& b) const {
		return stricmp(a, b) == 0;
	}
	bool operator()(const wstring* a, const wstring* b) const {
		return a == b || stricmp(*a, *b) == 0;
	}
	bool operator()(const wstring& a, const wstring& b) const {
		return stricmp(a, b) == 0;
	}
};

/** Case insensitive string ordering */
struct noCaseStringLess {
	bool operator()(const string* a, const string* b) const {
		return stricmp(*a, *b) < 0;
	}
	bool operator()(const string& a, const string& b) const {
		return stricmp(a, b) < 0;
	}
	bool operator()(const wstring* a, const wstring* b) const {
		return stricmp(*a, *b) < 0;
	}
	bool operator()(const wstring& a, const wstring& b) const {
		return stricmp(a, b) < 0;
	}
};

} // namespace dcpp

#endif // !defined(UTIL_H)