/*
 * Copyright (C) 2011-2012 AirDC++ Project
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

namespace dcpp {

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

		/* Cache some things to lower case */
		static string privKeyFile;
		static string tempDLDir;

		static DupeType checkDirDupe(const string& aDir, int64_t aSize);
		static DupeType checkFileDupe(const TTHValue& aDir, const string& aFileName);
		static DupeType checkFileDupe(const string& aFileName, int64_t aSize);

		static void init();
		static void updateCachedSettings();
		static string getLocalIp();

		static void setProfile(int profile, bool setSkiplist=false);
		static int getSlotsPerUser(bool download, double value=0, int aSlots=0);
		static int getSlots(bool download, double value=0, bool rarLimits=false);
		static int getSpeedLimit(bool download, double value=0);
		static int getMaxAutoOpened(double value = 0);

		static string getPrioText(int prio);

		static uint32_t getLastWrite(const string& path);

		static bool listRegexMatch(const StringList& l, const boost::regex& aReg);
		static int listRegexCount(const StringList& l, const boost::regex& aReg);
		static void listRegexSubtract(StringList& l, const boost::regex& aReg);
		static bool stringRegexMatch(const string& aReg, const string& aString);

		static string formatMatchResults(int matches, int newFiles, const BundleList& bundles, bool partial);

		static void fileEvent(const string& tgt, bool file=false);
		static bool isSub(const string& aDir, const string& aParent);
		static bool isParentOrExact(const string& aDir, const string& aSub);

		static const string getReleaseRegLong(bool chat);
		static const string getReleaseRegBasic();

		static void removeIfEmpty(const string& tgt);
		static bool removeDirectoryIfEmpty(const string& tgt);

		static bool isAdcHub(const string& hubUrl);
		static bool isHubLink(const string& hubUrl);

		static string stripHubUrl(const string& url);

		static string convertMovePath(const string& aPath, const string& aParent, const string& aTarget);
		static string regexEscape(const string& aStr, bool isWildcard);
	private:

	};
}
#endif