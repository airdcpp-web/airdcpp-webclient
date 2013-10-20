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

#include "stdinc.h"
#include "version.h"

#include "version-revno.inc" // should #define GIT_TAG, GIT_COMMIT, GIT_HASH


#define xstrver(s) strver(s)
#define strver(s) #s


namespace dcpp {
#ifdef BETAVER
	const std::string shortVersionString(xstrver(GIT_TAG) "-" xstrver(GIT_COMMIT) "-" GIT_HASH);
	const std::string fullVersionString(APPNAME " " xstrver(GIT_TAG) "-" xstrver(GIT_COMMIT) "-" GIT_HASH " " CONFIGURATION_TYPE " / " DCVERSIONSTRING);
#else
	const std::string shortVersionString(xstrver(GIT_TAG));
	const std::string fullVersionString(APPNAME " " xstrver(GIT_TAG) " " CONFIGURATION_TYPE " / " DCVERSIONSTRING);
#endif

	int getBuildNumber() { return GIT_COMMIT_COUNT; }
	string getBuildNumberStr() { return xstrver(GIT_COMMIT_COUNT); }
	int getCommitNumber() { return GIT_COMMIT; }
	string getVersionString() { return xstrver(GIT_TAG); }
}
