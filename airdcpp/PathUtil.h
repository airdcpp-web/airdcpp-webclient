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

#ifndef DCPLUSPLUS_DCPP_PATH_UTIL_H
#define DCPLUSPLUS_DCPP_PATH_UTIL_H

#include "compiler.h"
#include "constants.h"
#include "typedefs.h"

namespace dcpp {

class PathUtil  
{
public:
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

	static bool isAdcDirectoryPath(const string& aPath) noexcept;
	static bool isAdcRoot(const string& aPath) noexcept;

	static string validatePath(const string& aPath, bool aRequireEndSeparator = false) noexcept;
	static inline string validateFileName(const string& aFileName) noexcept { return cleanPathChars(aFileName, true); }
	static string cleanPathSeparators(const string& str) noexcept;
	static bool checkExtension(const string& tmp) noexcept;

	static string toAdcFile(const string& file) noexcept;
	static string toNmdcFile(const string& file) noexcept;
	
	static int pathSort(const string& a, const string& b) noexcept;


	// Returns true if aDir is a sub directory of aParent
	// Note: matching is always case insensitive. This will also handle directory paths in aParent without the trailing slash to work with Windows limitations (share monitoring)
	inline static bool isSubAdc(const string& aDir, const string& aParent) noexcept { return isSub(aDir, aParent, ADC_SEPARATOR); }
	inline static bool isSubLocal(const string& aDir, const string& aParent) noexcept { return isSub(aDir, aParent, PATH_SEPARATOR); }
	static bool isSub(const string& aDir, const string& aParent, const char separator) noexcept;

	// Returns true if aSub is a subdir of aDir OR both are the same directory
	// Note: matching is always case insensitive. This will also handle directory paths in aSub without the trailing slash to work with Windows limitations (share monitoring)
	inline static bool isParentOrExactAdc(const string& aDir, const string& aSub) noexcept { return isParentOrExact(aDir, aSub, ADC_SEPARATOR); }
	inline static bool isParentOrExactLocal(const string& aDir, const string& aSub) noexcept { return isParentOrExact(aDir, aSub, PATH_SEPARATOR); }
	static bool isParentOrExact(const string& aDir, const string& aSub, const char aSeparator) noexcept;
	static bool isParentOrExactLower(const string& aParentLower, const string& aSubLower, const char aSeparator) noexcept;


	// Removes common dirs from the end of toSubtract
	static string subtractCommonAdcDirectories(const string& toCompare, const string& toSubtract) noexcept {
		return subtractCommonDirs(toCompare, toSubtract, ADC_SEPARATOR);
	}

	static string subtractCommonDirectories(const string& toCompare, const string& toSubtract) noexcept {
		return subtractCommonDirs(toCompare, toSubtract, PATH_SEPARATOR);
	}

	// Removes common path section from the beginning of toSubtract
	// Path separators are ignored when comparing
	static string subtractCommonParents(const string& toCompare, const StringList& toSubtract) noexcept;

	// Returns the position from the end of aSubPath from where the paths start to differ
	// Path separators are ignored when comparing
	static size_t compareFromEndAdc(const string& aMainPath, const string& aSubAdcPath) noexcept {
		return compareFromEnd(aMainPath, aSubAdcPath, ADC_SEPARATOR);
	}

	// Get the path for matching a file list (remote file must be in ADC format)
	// Returns the local path for NMDC and the remote path for ADC (or empty for NMDC results that have nothing in common)
	static string getAdcMatchPath(const string& aRemoteFile, const string& aLocalFile, const string& aLocalBundlePath, bool aNmdc) noexcept;

	// Remove common subdirectories (except the last one) from the end of aSubPath
	// Non-subtractable length of aMainPath may also be specified
	// Path separators are ignored when comparing
	static string getLastCommonAdcDirectoryPathFromSub(const string& aMainPath, const string& aSubPath, size_t aMainBaseLength = 0) noexcept {
		return getLastCommonDirectoryPathFromSub(aMainPath, aSubPath, ADC_SEPARATOR, aMainBaseLength);
	}

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
#endif

	static bool fileExists(const string &aFile) noexcept;

#if defined _WIN32 && defined _DEBUG
	static void test() noexcept;
#endif

	static bool removeDirectoryIfEmpty(const string& aPath, int aMaxAttempts);
private:
	static string cleanPathChars(string aPath, bool isFileName) noexcept;

	static string getLastCommonDirectoryPathFromSub(const string& aMainPath, const string& aSubPath, char aSubSeparator, size_t aMainBaseLength) noexcept;

	static string subtractCommonDirs(const string& toCompare, const string& toSubtract, char aSeparator) noexcept;
	static size_t compareFromEnd(const string& aMainPath, const string& aSubPath, char aSubSeparator) noexcept;

	static bool removeDirectoryIfEmptyRecursive(const string& aTarget, int aMaxAttempts, int aCurAttempts);
};


class IsParentOrExact {
public:
	// Returns true for items matching the predicate that are parent directories of compareTo (or exact matches)
	IsParentOrExact(const string& aCompareTo, const char aSeparator) : compareTo(aCompareTo), separator(aSeparator) {}
	bool operator()(const string& p) noexcept { return PathUtil::isParentOrExact(p, compareTo, separator); }

	IsParentOrExact& operator=(const IsParentOrExact&) = delete;
private:
	const string& compareTo;
	const char separator;
};

class IsParentOrExactOrSub {
public:
	IsParentOrExactOrSub(const string& aCompareTo, const char aSeparator) : compareTo(aCompareTo), separator(aSeparator) {}
	bool operator()(const string& p) noexcept { return PathUtil::isParentOrExact(p, compareTo, separator) || PathUtil::isSub(p, compareTo, separator); }

	IsParentOrExactOrSub& operator=(const IsParentOrExactOrSub&) = delete;
private:
	const string& compareTo;
	const char separator;
};

class IsSub {
public:
	// Returns true for items matching the predicate that are subdirectories of compareTo
	IsSub(const string& aCompareTo, const char aSeparator) : compareTo(aCompareTo), separator(aSeparator) {}
	bool operator()(const string& p) noexcept { return PathUtil::isSub(p, compareTo, separator); }

	IsSub& operator=(const IsSub&) = delete;
private:
	const string& compareTo;
	const char separator;
};


} // namespace dcpp

#endif // !defined(UTIL_H)
