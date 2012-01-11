
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
		static string getLocale();
		static void setProfile(int profile, bool setSkiplist=false);
		static int getSlotsPerUser(bool download, double value=0, int aSlots=0);
		static int getSlots(bool download, double value=0, bool rarLimits=false);
		static int getSpeedLimit(bool download, double value=0);
		static int getMaxAutoOpened(double value = 0);
		static string getPrioText(int prio);
		static string getReleaseDir(const string& aName);
		static string getMountPath(const string& aPath);
		static string getMountPath(const string& aPath, const StringSet& aVolumes);
		static bool checkSharedName(const string& fullPath, bool dir, bool report = true, const int64_t& size = 0);
		static void getTarget(StringList& targets, string& target, int64_t& size);
		static void getVolumes(StringSet& volumes);
		static bool getDiskInfo(const string& aPath, int64_t& freeSpace);

		static uint32_t getLastWrite(const string& path) {
							
			FileFindIter ff = FileFindIter(path);

			if (ff != FileFindIter()) {
				return ff->getLastWriteTime();
			}
		return 0;
		}

		static bool listRegexMatch(const StringList& l, const boost::regex& aReg);
		static int listRegexCount(const StringList& l, const boost::regex& aReg);
		static bool stringRegexMatch(const string& aReg, const string& aString);
		static string formatMatchResults(int matches, int newFiles, const BundleList& bundles, bool partial);
		static string convertMovePath(const string& aSourceCur, const string& aSourceRoot, const string& aTarget);
		static void fileEvent(const string& tgt, bool file=false);
		static bool isSub(const string& aDir, const string& aParent);
		static bool isParent(const string& aDir, const string& aSub);
		static const string getReleaseRegLong(bool chat);
		static const string getReleaseRegBasic();
	private:

	};
}
#endif