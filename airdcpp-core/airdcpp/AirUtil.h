/*
 * Copyright (C) 2011-2017 AirDC++ Project
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

#ifndef DCPP_AIRUTIL_H
#define DCPP_AIRUTIL_H

#include "compiler.h"

#include "DupeType.h"
#include "Priority.h"
#include "SettingsManager.h"

namespace dcpp {

class AirUtil {
	
public:
	class TimeCounter {
	public:
		TimeCounter(string aMsg);
		~TimeCounter();
	private:
		time_t start;
		string msg;
	};

	static boost::regex releaseReg;
	static boost::regex subDirRegPlain;
	static boost::regex crcReg;

	// Check directory dupe status by name or ADC path
	static DupeType checkAdcDirectoryDupe(const string& aAdcPath, int64_t aSize);
	static DupeType checkFileDupe(const TTHValue& aTTH);

	static StringList getAdcDirectoryDupePaths(DupeType aType, const string& aAdcPath);
	static StringList getFileDupePaths(DupeType aType, const TTHValue& aTTH);

	static bool isShareDupe(DupeType aType) noexcept;
	static bool isQueueDupe(DupeType aType) noexcept;
	static bool isFinishedDupe(DupeType aType) noexcept;
	static bool allowOpenDupe(DupeType aType) noexcept;

	// Calculates TTH value from the lowercase filename and size
	static TTHValue getTTH(const string& aFileName, int64_t aSize) noexcept;

	// Calculates TTH value from the lowercase path
	static TTHValue getPathId(const string& aPath) noexcept;

	static void init();

	static string toOpenFileName(const string& aFileName, const TTHValue& aTTH) noexcept;
	static string fromOpenFileName(const string& aFileName) noexcept;

	struct AdapterInfo {
		AdapterInfo(const string& aName, const string& aIP, uint8_t aPrefix) : adapterName(aName), ip(aIP), prefix(aPrefix) { }

		string adapterName;
		string ip;
		uint8_t prefix;
	};
	typedef vector<AdapterInfo> AdapterInfoList;

	// Get a list of network adapters for the wanted protocol
	static AdapterInfoList getNetworkAdapters(bool v6);

	// Get a sorted list of available bind adapters for the wanted protocol
	// Ensures that the current bind address is listed as well
	static AdapterInfoList getBindAdapters(bool v6);

	// Get current bind address
	// The best adapter address is returned if no bind address is configured
	// (public addresses are preferred over local/private ones)
	static string getLocalIp(bool v6) noexcept;

	static int getSlotsPerUser(bool download, double value=0, int aSlots=0, SettingsManager::SettingProfile aProfile = static_cast<SettingsManager::SettingProfile>(SETTING(SETTINGS_PROFILE)));
	static int getSlots(bool download, double value=0, SettingsManager::SettingProfile aProfile = static_cast<SettingsManager::SettingProfile>(SETTING(SETTINGS_PROFILE)));

	// Maximum wanted download/upload speed. Uses set connection values by default.
	static int getSpeedLimit(bool download, double value=0);
	static int getMaxAutoOpened(double value = 0);

	static string getPrioText(Priority aPriority) noexcept;

	static bool listRegexMatch(const StringList& l, const boost::regex& aReg);
	static int listRegexCount(const StringList& l, const boost::regex& aReg);
	static void listRegexSubtract(StringList& l, const boost::regex& aReg);
	static bool stringRegexMatch(const string& aReg, const string& aString);
	
	static bool isRelease(const string& aString);

	static void getRegexMatchesT(const tstring& aString, TStringList& l, const boost::wregex& aReg);
	static void getRegexMatches(const string& aString, StringList& l, const boost::regex& aReg);

	static string formatMatchResults(int aMatchingFiles, int aNewFiles, const BundleList& aBundles) noexcept;

	static void fileEvent(const string& tgt, bool file=false);

	// Returns true if aDir is a sub directory of aParent
	// Note: matching is always case insensitive. This will also handle directory paths in aParent without the trailing slash to work with Windows limitations (share monitoring)
	inline static bool isSubAdc(const string& aDir, const string& aParent) noexcept { return isSub(aDir, aParent, ADC_SEPARATOR);	}
	inline static bool isSubLocal(const string& aDir, const string& aParent) noexcept { return isSub(aDir, aParent, PATH_SEPARATOR); }
	static bool isSub(const string& aDir, const string& aParent, const char separator) noexcept;

	// Returns true if aSub is a subdir of aDir OR both are the same directory
	// Note: matching is always case insensitive. This will also handle directory paths in aSub without the trailing slash to work with Windows limitations (share monitoring)
	inline static bool isParentOrExactAdc(const string& aDir, const string& aSub) noexcept { return isParentOrExact(aDir, aSub, ADC_SEPARATOR); }
	inline static bool isParentOrExactLocal(const string& aDir, const string& aSub) noexcept { return isParentOrExact(aDir, aSub, PATH_SEPARATOR); }
	static bool isParentOrExact(const string& aDir, const string& aSub, const char separator) noexcept;

	static const string getReleaseRegLong(bool chat) noexcept;
	static const string getReleaseRegBasic() noexcept;
	static const string getSubDirReg() noexcept;

	inline static string getReleaseDirLocal(const string& aDir, bool aCut) noexcept { return getReleaseDir(aDir, aCut, PATH_SEPARATOR); };
	inline static string getAdcReleaseDir(const string& aDir, bool aCut) noexcept { return getReleaseDir(aDir, aCut, ADC_SEPARATOR); };
	static string getReleaseDir(const string& dir, bool cut, const char separator) noexcept;

	static const string getLinkUrl() noexcept;

	static void removeDirectoryIfEmpty(const string& tgt, int maxAttempts, bool silent);

	static bool isAdcHub(const string& aHubUrl) noexcept;
	static bool isSecure(const string& aHubUrl) noexcept;
	static bool isHubLink(const string& aHubUrl) noexcept;

	static string regexEscape(const string& aStr, bool isWildcard) noexcept;

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

	/* Returns the name without subdirs and possible position from where the subdir starts */
	static pair<string, string::size_type> getAdcDirectoryName(const string& aName) noexcept {
		return getDirName(aName, ADC_SEPARATOR);
	}

	static string getTitle(const string& searchTerm) noexcept;
private:
	static pair<string, string::size_type> getDirName(const string& aName, char separator) noexcept;
	static string getLastCommonDirectoryPathFromSub(const string& aMainPath, const string& aSubPath, char aSubSeparator, size_t aMainBaseLength) noexcept;

	static string subtractCommonDirs(const string& toCompare, const string& toSubtract, char separator) noexcept;
	static size_t compareFromEnd(const string& aMainPath, const string& aSubPath, char aSubSeparator) noexcept;

	static bool removeDirectoryIfEmptyRe(const string& tgt, int maxAttempts, int curAttempts);
};

class IsParentOrExact {
public:
	// Returns true for items matching the predicate that are parent directories of compareTo (or exact matches)
	IsParentOrExact(const string& aCompareTo, const char aSeparator) : compareTo(aCompareTo), separator(aSeparator) {}
	bool operator()(const string& p) noexcept { return AirUtil::isParentOrExact(p, compareTo, separator); }

	IsParentOrExact& operator=(const IsParentOrExact&) = delete;
private:
	const string& compareTo;
	const char separator;
};

class IsParentOrExactOrSub {
public:
	IsParentOrExactOrSub(const string& aCompareTo, const char aSeparator) : compareTo(aCompareTo), separator(aSeparator) {}
	bool operator()(const string& p) noexcept { return AirUtil::isParentOrExact(p, compareTo, separator) || AirUtil::isSub(p, compareTo, separator); }

	IsParentOrExactOrSub& operator=(const IsParentOrExactOrSub&) = delete;
private:
	const string& compareTo;
	const char separator;
};

class IsSub {
public:
	// Returns true for items matching the predicate that are subdirectories of compareTo
	IsSub(const string& aCompareTo, const char aSeparator) : compareTo(aCompareTo), separator(aSeparator) {}
	bool operator()(const string& p) noexcept { return AirUtil::isSub(p, compareTo, separator); }

	IsSub& operator=(const IsSub&) = delete;
private:
	const string& compareTo;
	const char separator;
};

}
#endif