/*
* Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"
#include "version.h"

#include "version.inc"

#ifndef _WIN32
#include <sys/utsname.h>
#endif

#define xstrver(s) strver(s)
#define strver(s) #s


namespace dcpp {
	const std::string shortVersionString(APPNAME_INC " " GIT_TAG);
	const std::string fullVersionString(APPNAME_INC " " GIT_TAG " " + getConfigurationType() + " / " DCVERSIONSTRING);
	const char* getAppName() { return APPNAME_INC; }
	int getBuildNumber() { return GIT_COMMIT_COUNT; }
	string getBuildNumberStr() { return xstrver(GIT_COMMIT_COUNT); }
	string getVersionTag() { return GIT_TAG; }

	time_t getVersionDate() { return VERSION_DATE; }

	string getConfigurationType() {
#ifdef _WIN64
		return "x86-64";
#elif _WIN32
		return "x86-32";
#else
		utsname n;
		if (uname(&n) != 0) {
			return "(unknown architecture)";
		}

		return string(n.machine);
#endif
	}

	VersionType getVersionType() {
		string v = GIT_TAG;
		if (v.length() > 4 && v[4] == 'a') {
			return VERSION_NIGHTLY;
		}

		if (v.length() > 4 && v[4] == 'b') {
			return VERSION_BETA;
		}
		
		return VERSION_STABLE;
	}
}
