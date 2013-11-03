/* 
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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

#define DCVERSIONSTRING "0.830"

namespace dcpp {
	enum VersionType {
		VERSION_STABLE,
		VERSION_BETA,
		VERSION_NIGHTLY,
		VERSION_LAST
	};

	extern const std::string shortVersionString;
	extern const std::string fullVersionString;

	int getBuildNumber();
	string getBuildNumberStr();
	string getVersionString();
	time_t getVersionDate();
	const char* getAppName();
	string getConfigurationType();
	VersionType getVersionType();
}

#define APPNAME getAppName()
#define VERSIONSTRING getVersionString()
#define CONFIGURATION_TYPE getConfigurationType()

//Total git commit commit count
#define BUILD_NUMBER_STR getBuildNumberStr()
#define BUILD_NUMBER getBuildNumber()

//Git commit count for the current git tag
#define COMMIT_NUMBER getCommitNumber()

#ifdef NDEBUG
# define INST_NAME "{AIRDC-AEE8350A-B49A-4753-AB4B-E55479A48351}"
#else
# define INST_NAME "{AIRDC-AEE8350A-B49A-4753-AB4B-E55479A48350}"
#endif

/* Update the .rc file as well... */