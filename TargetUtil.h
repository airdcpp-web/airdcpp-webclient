/*
 * Copyright (C) 2011-2014 AirDC++ Project
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

		string targetDir;
		int64_t diskSpace, queued;
		int64_t getFreeSpace() const { return diskSpace-queued; }
		int64_t getDiff(int64_t aSize) const { return getFreeSpace() - aSize; }
		bool isInitialized() { return diskSpace != 0 || queued != 0 || !targetDir.empty(); }

		bool operator<(const TargetInfo& ti) const {
			return (diskSpace-queued) < (ti.diskSpace-ti.queued);
		}
	};

	enum TargetType {
		TARGET_PATH,
		TARGET_FAVORITE,
		TARGET_SHARE
	};

	typedef unordered_map<string, TargetInfo, noCaseStringHash, noCaseStringEq> TargetInfoMap;
	typedef unordered_set<string, noCaseStringHash, noCaseStringEq> VolumeSet;

	static string getMountPath(const string& aPath, const VolumeSet& aVolumes);

	static bool getTarget(const StringList& targets, TargetInfo& ti_, const int64_t& size);

	static bool getVirtualTarget(const string& aTarget, TargetType targetType, TargetInfo& ti_, const int64_t& size);

	static void getVolumes(VolumeSet& volumes);
	static bool getDiskInfo(TargetInfo& ti_);

	static void compareMap(const TargetInfoMap& targets, TargetInfo& retTi_, const int64_t& aSize, int8_t aMethod);
	static void reportInsufficientSize(const TargetInfo& ti, int64_t aSize);
	static string getInsufficientSizeMessage(const TargetInfo& ti, int64_t aSize);
};

}

#endif