
#ifndef AIR_UTIL_H
#define AIR_UTIL_H

#include "compiler.h"

#include "Text.h"
#include "pme.h"

namespace dcpp {

static PME releaseReg;
static PME subDirReg;
static boost::regex skiplistReg; //boost is faster on this??

class AirUtil {
	
	public:
		static void init();
		static void setSkiplist();
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
		static bool checkSharedName(const string& aName, bool dir, bool report = true);

	private:

	};
}
#endif