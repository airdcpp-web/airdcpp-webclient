/*
 * Copyright (C) 2001-2021 Jacek Sieka, arnetheduck on gmail point com
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
#include "constants.h"

#ifndef _WIN32

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

/** 
 * Compares two values
 * @return -1 if v1 < v2, 0 if v1 == v2 and 1 if v1 > v2
 */
template<typename T1>
inline int compare(const T1& v1, const T1& v2) noexcept { return (v1 < v2) ? -1 : ((v1 == v2) ? 0 : 1); }

typedef std::function<void (const string&)> StepFunction;
typedef std::function<bool (const string& /*Message*/, bool /*isQuestion*/, bool /*isError*/)> MessageFunction;
typedef std::function<void (float)> ProgressFunction;

// Recursively collected information about directory content
struct DirectoryContentInfo {
	DirectoryContentInfo() : directories(-1), files(-1) {  }
	DirectoryContentInfo(int aDirectories, int aFiles) : directories(aDirectories), files(aFiles) {  }

	int directories;
	int files;
};

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

class Util  
{
public:
	static bool hasContentInfo(const DirectoryContentInfo& aContentInfo) {
		return aContentInfo.directories >= 0 && aContentInfo.files >= 0;
	}

	static bool directoryEmpty(const DirectoryContentInfo& aContentInfo) {
		return aContentInfo.directories == 0 && aContentInfo.files == 0;
	}

	static int directoryContentSort(const DirectoryContentInfo& a, const DirectoryContentInfo& b) noexcept;
	static string formatDirectoryContent(const DirectoryContentInfo& aInfo) noexcept;
	static string formatFileType(const string& aPath) noexcept;

	struct PathSortOrderInt {
		int operator()(const string& a, const string& b) const noexcept {
			return pathSort(a, b);
		}
	};

	struct PathSortOrderBool {
		bool operator()(const string& a, const string& b) const noexcept {
			return pathSort(a, b) < 0;
		}
	};

	static tstring emptyStringT;
	static string emptyString;
	static wstring emptyStringW;
	static string getOsVersion(bool http = false) noexcept;
	static bool IsOSVersionOrGreater(int major, int minor) noexcept;

	// Execute a background process and get exit code
	static int runSystemCommand(const string& aCommand) noexcept;

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
		/** XML files for each bundle */
		PATH_BUNDLES,
		/** XML files for cached share structure */
		PATH_SHARECACHE,
		/** Temp files (viewed files, temp shared items...) */
		PATH_TEMP,

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


	static int64_t convertSize(int64_t aValue, SizeUnits valueType, SizeUnits to = B) noexcept;

	// The client uses regular config directories or boot config file to determine the config path
	// if a custom path isn't provided
	static void initialize(const string& aConfigPath = Util::emptyString);

	static string getAppFilePath() noexcept;
	static string getAppFileName() noexcept;
	static string getAppPath() noexcept;

	static string getSystemUsername() noexcept;
#ifndef _WIN32 
	static std::string appPath;
	static void setApp(const string& app) noexcept;
#endif

	/** Path of temporary storage */
	static string getOpenPath() noexcept;

	/** Path of configuration files */
	static const string& getPath(Paths path) noexcept { return paths[path]; }

	/** Migrate from pre-localmode config location */
	static void migrate(const string& file) noexcept;
	static void migrate(const string& aDir, const string& aPattern) noexcept;

	/** Path of file lists */
	static string getListPath() noexcept { return getPath(PATH_FILE_LISTS); }
	/** Path of bundles */
	static string getBundlePath() noexcept { return getPath(PATH_BUNDLES); }

	static string translateError(int aError) noexcept;
	static string formatLastError() noexcept;

	static string getFilePath(const string& aPath, const char aSeparator = PATH_SEPARATOR) noexcept;
	inline static string getAdcFilePath(const string& aPath) noexcept { return getFilePath(aPath, ADC_SEPARATOR); }

	static string getFileName(const string& aPath, const char aSeparator = PATH_SEPARATOR) noexcept;
	inline static string getAdcFileName(const string& aPath) noexcept { return getFileName(aPath, ADC_SEPARATOR); };

	static string getLastDir(const string& aPath, const char aSeparator = PATH_SEPARATOR) noexcept;
	inline static string getAdcLastDir(const string& aPath) noexcept { return getLastDir(aPath, ADC_SEPARATOR); };

	static string getParentDir(const string& aPath, const char aSeparator = PATH_SEPARATOR, bool allowEmpty = false) noexcept;
	inline static string getAdcParentDir(const string& aPath) noexcept { return getParentDir(aPath, ADC_SEPARATOR, false); };

	template<typename string_t>
	inline static bool isDirectoryPath(const string_t& aPath, const char aSeparator = PATH_SEPARATOR) noexcept { return !aPath.empty() && aPath.back() == aSeparator; }
	static string ensureTrailingSlash(const string& aPath, const char aSeparator = PATH_SEPARATOR) noexcept;

	static string joinDirectory(const string& aPath, const string& aDirectoryName, const char separator = PATH_SEPARATOR) noexcept;

	static string getFileExt(const string& aPath) noexcept;

	static wstring getFilePath(const wstring& aPath) noexcept;
	static wstring getFileName(const wstring& aPath) noexcept;
	static wstring getFileExt(const wstring& aPath) noexcept;
	static wstring getLastDir(const wstring& aPath) noexcept;

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

	static void sanitizeUrl(string& url) noexcept;
	static void decodeUrl(const string& aUrl, string& protocol, string& host, string& port, string& path, string& query, string& fragment) noexcept;

	// Used to parse NMDC-style ip:port combination
	static void parseIpPort(const string& aIpPort, string& ip, string& port) noexcept;
	static map<string, string> decodeQuery(const string& query) noexcept;

	static bool isAdcDirectoryPath(const string& aPath) noexcept;
	static bool isAdcRoot(const string& aPath) noexcept;

	static string validatePath(const string& aPath, bool aRequireEndSeparator = false) noexcept;
	static inline string validateFileName(const string& aFileName) noexcept { return cleanPathChars(aFileName, true); }
	static string cleanPathSeparators(const string& str) noexcept;
	static bool checkExtension(const string& tmp) noexcept;

	static string addBrackets(const string& s) noexcept;

	static string formatBytes(const string& aString) noexcept { return formatBytes(toInt64(aString)); }
	static string formatConnectionSpeed(const string& aString) noexcept { return formatConnectionSpeed(toInt64(aString)); }

	static string getShortTimeString(time_t t = time(NULL) ) noexcept;
	static string getTimeStamp(time_t t = time(NULL) ) noexcept;

	static string getTimeString() noexcept;

	static string getDateTime(time_t t) noexcept;
#ifdef _WIN32
	static wstring getDateTimeW(time_t t) noexcept;
#endif
	static string toAdcFile(const string& file) noexcept;
	static string toNmdcFile(const string& file) noexcept;
	
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

	static int pathSort(const string& a, const string& b) noexcept;

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
	inline static string formatPath(const string& aPath) noexcept {
		//dont format unless its needed
		//also we want to limit the unc path lower, no point on endless paths.
		if (aPath.size() < 250 || aPath.size() > UNC_MAX_PATH) {
			return aPath;
		}

		if (aPath[0] == '\\' && aPath[1] == '\\') {
			return "\\\\?\\UNC\\" + aPath.substr(2);
		}

		return "\\\\?\\" + aPath;
	}

	inline static wstring formatPathW(const tstring& aPath) noexcept {
		//dont format unless its needed
		//also we want to limit the unc path lower, no point on endless paths. 
		if (aPath.size() < 250 || aPath.size() > UNC_MAX_PATH) {
			return aPath;
		}

		if (aPath[0] == '\\' && aPath[1] == '\\') {
			return _T("\\\\?\\UNC\\") + aPath.substr(2);
		}

		return _T("\\\\?\\") + aPath;
	}

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

	static string encodeURI(const string& /*aString*/, bool reverse = false) noexcept;
	
	// Return whether the IP is localhost or a link-local address (169.254.0.0/16 or fe80)
	static bool isLocalIp(const string& ip, bool v6) noexcept;

	// Returns whether the IP is a private one (non-local)
	//
	// Private ranges:
	// IPv4: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16
	// IPv6: fd prefix
	static bool isPrivateIp(const string& ip, bool v6) noexcept;

	static bool isPublicIp(const string& ip, bool v6) noexcept;

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

	static bool fileExists(const string &aFile) noexcept;

	static int randInt(int min=0, int max=std::numeric_limits<int>::max()) noexcept;
	static uint32_t rand() noexcept;
	static uint32_t rand(uint32_t high) noexcept { return high == 0 ? 0 : rand() % high; }
	static uint32_t rand(uint32_t low, uint32_t high) noexcept { return rand(high-low) + low; }
	static double randd() noexcept { return ((double)rand()) / ((double)0xffffffff); }

	static bool hasStartupParam(const string& aParam) noexcept;
	static string getStartupParams(bool isFirst) noexcept;
	static void addStartupParam(const string& aParam) noexcept;
	static optional<string> getStartupParam(const string& aKey) noexcept;

	static bool usingLocalMode() noexcept { return localMode; }
	static bool wasUncleanShutdown;

	static bool isChatCommand(const string& aText) noexcept;
private:
	static string cleanPathChars(string aPath, bool isFileName) noexcept;

	/** In local mode, all config and temp files are kept in the same dir as the executable */
	static bool localMode;

	static string paths[PATH_LAST];

	static StringList startupParams;
	
	static bool loadBootConfig(const string& aDirectoryPath) noexcept;

	static int osMinor;
	static int osMajor;
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
