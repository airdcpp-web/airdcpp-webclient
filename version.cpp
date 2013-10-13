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

#include "version-revno.inc" // should #define BUILD_NUMBER_INT


#define xstrver(s) strver(s)
#define strver(s) #s


namespace dcpp {
#ifdef BETAVER
	const std::string shortVersionString(VERSIONSTRING "r" xstrver(BUILD_NUMBER_INT));
	const std::string fullVersionString(APPNAME " " VERSIONSTRING " " CONFIGURATION_TYPE " r" xstrver(BUILD_NUMBER_INT) " / " DCVERSIONSTRING);
#else
	const std::string shortVersionString(VERSIONSTRING);
	const std::string fullVersionString(APPNAME " " VERSIONSTRING " " CONFIGURATION_TYPE " / " DCVERSIONSTRING);
#endif

	int getBuildNumber() { return BUILD_NUMBER_INT; }
	string getBuildNumberStr() { return xstrver(BUILD_NUMBER_INT); }
}
