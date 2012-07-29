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
#include "pme.h"
#include "File.h"

namespace dcpp {

static PME releaseReg;
static PME subDirReg;
static boost::regex skiplistReg; //boost is faster on this??

/* Cache some things to lower case */
static string privKeyFile;
static string tempDLDir;
static string winDir;

class AirUtil {
	
	public:
		static void init();
		static void updateCachedSettings();
		static bool matchSkiplist(const string& str);
		static string getLocalIp();

		static void setProfile(int profile, bool setSkiplist=false);
		static int getSlotsPerUser(bool download, double value=0, int aSlots=0);
		static int getSlots(bool download, double value=0, bool rarLimits=false);
		static int getSpeedLimit(bool download, double value=0);
		static int getMaxAutoOpened(double value = 0);

		static string getPrioText(int prio);
		static string getReleaseDir(const string& aName);
		static bool checkSharedName(const string& fullPath, bool dir, bool report = true, const int64_t& size = 0);

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
		static bool isEmpty(const string& tgt);

		static bool isAdcHub(const string& hubUrl);
		static bool isHubLink(const string& hubUrl);

		static string stripHubUrl(const string& url);

		static string convertMovePath(const string& aPath, const string& aParent, const string& aTarget);
		static string AirUtil::regexEscape(const string& aStr, bool isWildcard);
	private:

	};
}
#endif