/*
 * Copyright (C) 2011 AirDC++ Project
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

#ifndef TARGET_UTIL_H
#define TARGET_UTIL_H

#include "typedefs.h"
#include "Util.h"

#include <string>

namespace dcpp {
using std::string;

class TargetUtil {

public:
	struct TargetInfo {
		explicit TargetInfo() : targetDir(Util::emptyString), diskSpace(0), queued(0) { }
		explicit TargetInfo(const string& aPath, int64_t aFreeSpace) : targetDir(aPath), diskSpace(aFreeSpace), queued(0) { }

		int64_t queued, diskSpace;
		string targetDir;
		int64_t getFreeSpace() { return diskSpace-queued; }
	};

	enum TargetType {
		TARGET_PATH,
		TARGET_FAVORITE,
		TARGET_SHARE,
	};

	typedef map<string, TargetInfo> TargetInfoMap;

	static string getMountPath(const string& aPath);
	static string getMountPath(const string& aPath, const StringSet& aVolumes);

	static void getTarget(int aID, TargetInfo& ti_);
	static void getTarget(StringList& targets, TargetInfo& ti_);

	static void getVirtualTarget(const string& aTarget, TargetType targetType, TargetInfo& ti_);
	static void getVirtualName(int ID, string& vTarget, TargetType& targetType);

	static void getVolumes(StringSet& volumes);
	static bool getDiskInfo(TargetInfo& ti_);

	static int countDownloadDirItems();
	static int countShareFavDirs();
};

}

#endif