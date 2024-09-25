/*
 * Copyright (C) 2011-2024 AirDC++ Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#ifndef DCPLUSPLUS_DCPP_DUPE_UTIL_H
#define DCPLUSPLUS_DCPP_DUPE_UTIL_H

#include <airdcpp/compiler.h>
#include <airdcpp/constants.h>
#include <airdcpp/typedefs.h>

#include <airdcpp/DupeType.h>
#include <airdcpp/MerkleTree.h>

namespace dcpp {

class DupeUtil {
	
public:
	static boost::regex releaseRegBasic;
	static boost::regex releaseRegChat;
	static boost::regex subDirRegPlain;

	// Check directory dupe status by name or ADC path
	static DupeType checkAdcDirectoryDupe(const string& aAdcPath, int64_t aSize);
	static DupeType checkFileDupe(const TTHValue& aTTH);

	static StringList getAdcDirectoryDupePaths(DupeType aType, const string& aAdcPath);
	static StringList getFileDupePaths(DupeType aType, const TTHValue& aTTH);

	static bool isShareDupe(DupeType aType) noexcept;
	static bool isQueueDupe(DupeType aType) noexcept;
	static bool isFinishedDupe(DupeType aType) noexcept;
	static bool allowOpenDupe(DupeType aType) noexcept;

	static void init();
	
	static bool isRelease(const string& aString);

	static const string getReleaseRegLong(bool chat) noexcept;
	static const string getReleaseRegBasic() noexcept;
	static const string getSubDirReg() noexcept;

	inline static string getReleaseDirLocal(const string& aDir, bool aCut) noexcept { return getReleaseDir(aDir, aCut, PATH_SEPARATOR); };
	inline static string getAdcReleaseDir(const string& aDir, bool aCut) noexcept { return getReleaseDir(aDir, aCut, ADC_SEPARATOR); };
	static string getReleaseDir(const string& dir, bool cut, const char separator) noexcept;

	// Returns the name without subdirs and possible position from where the subdir starts
	static pair<string, string::size_type> getAdcDirectoryName(const string& aName) noexcept {
		return getDirectoryName(aName, ADC_SEPARATOR);
	}

	// Returns the name without subdirs and possible position from where the subdir starts
	static pair<string, string::size_type> getLocalDirectoryName(const string& aName) noexcept {
		return getDirectoryName(aName, PATH_SEPARATOR);
	}

	static string getTitle(const string& searchTerm) noexcept;

	using DupeSet = set<DupeType>;
	static DupeType parseDirectoryContentDupe(const DupeSet& aDupeSet) noexcept;
private:
	static pair<string, string::size_type> getDirectoryName(const string& aName, char aSeparator) noexcept;
};

}
#endif