/*
 * Copyright (C) 2011-2013 AirDC++ Project
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

#ifndef AIR_UTIL_H
#define AIR_UTIL_H

#include "compiler.h"

#include "Text.h"
#include "SettingsManager.h"

namespace dcpp {

//Away modes
enum AwayMode {
	AWAY_OFF,
	AWAY_IDLE,
	AWAY_MINIMIZE,
	AWAY_MANUAL //highest value
};

enum DupeType { 
	DUPE_NONE, 
	PARTIAL_SHARE_DUPE, 
	SHARE_DUPE, 
	PARTIAL_QUEUE_DUPE, 
	QUEUE_DUPE,
	FINISHED_DUPE, 
	SHARE_QUEUE_DUPE 
};

class AirUtil {
	
	public:
		static boost::regex releaseReg;
		static boost::regex subDirRegPlain;
		static boost::regex crcReg;

		/* Cache some things to lower case */
		static string privKeyFile;
		static string tempDLDir;

		static DupeType checkDirDupe(const string& aDir, int64_t aSize);
		static DupeType checkFileDupe(const TTHValue& aTTH);
		static DupeType checkFileDupe(const string& aFileName, int64_t aSize);

		static string getDirDupePath(DupeType aType, const string& aPath);
		static string getDupePath(DupeType aType, const TTHValue& aTTH);

		static TTHValue getTTH(const string& aFileName, int64_t aSize);

		static void init();
		static void updateCachedSettings();
		static string getLocalIp(bool v6, bool allowPrivate = true);


		struct AddressInfo {
			AddressInfo(const string& aName, const string& aIP, uint8_t aPrefix) : adapterName(aName), ip(aIP), prefix(aPrefix) { }
			string adapterName;
			string ip;
			uint8_t prefix;
		};
		typedef vector<AddressInfo> IpList;
		static void getIpAddresses(IpList& addresses, bool v6);

		static int getSlotsPerUser(bool download, double value=0, int aSlots=0, SettingsManager::SettingProfile aProfile = static_cast<SettingsManager::SettingProfile>(SETTING(SETTINGS_PROFILE)));
		static int getSlots(bool download, double value=0, SettingsManager::SettingProfile aProfile = static_cast<SettingsManager::SettingProfile>(SETTING(SETTINGS_PROFILE)));
		static int getSpeedLimit(bool download, double value=0);
		static int getMaxAutoOpened(double value = 0);

		static string getPrioText(int prio);

		static bool listRegexMatch(const StringList& l, const boost::regex& aReg);
		static int listRegexCount(const StringList& l, const boost::regex& aReg);
		static void listRegexSubtract(StringList& l, const boost::regex& aReg);
		static bool stringRegexMatch(const string& aReg, const string& aString);

		static void getRegexMatchesT(const tstring& aString, TStringList& l, const boost::wregex& aReg);
		static void getRegexMatches(const string& aString, StringList& l, const boost::regex& aReg);

		static string formatMatchResults(int matches, int newFiles, const BundleList& bundles, bool partial);

		static void fileEvent(const string& tgt, bool file=false);
		static bool isSub(const string& aDir, const string& aParent);
		static bool isParentOrExact(const string& aDir, const string& aSub);

		static const string getReleaseRegLong(bool chat);
		static const string getReleaseRegBasic();

		static const string getLinkUrl();

		static void removeIfEmpty(const string& tgt);
		static bool removeDirectoryIfEmpty(const string& tgt, int attempts = 0);

		static bool isAdcHub(const string& hubUrl);
		static bool isHubLink(const string& hubUrl);

		static string convertMovePath(const string& aPath, const string& aParent, const string& aTarget);
		static string regexEscape(const string& aStr, bool isWildcard);

		static bool getAway() { return away > 0; }
		static AwayMode getAwayMode() { return away; }
		static void setAway(AwayMode aAway);
		static string getAwayMessage(const string& aAwayMsg, ParamMap& params);

		/* Removes common dirs from the end of toSubtract */
		static string subtractCommonDirs(const string& toCompare, const string& toSubtract, char separator);

		/* Returns the name without subdirs and possible position from where the subdir starts */
		static pair<string, string::size_type> getDirName(const string& aName, char separator);
		static string getTitle(const string& searchTerm);
	private:

		static AwayMode away;
		static time_t awayTime;

	};
}
#endif