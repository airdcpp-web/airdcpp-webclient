/*
 * Copyright (C) 2011-2016 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_TARGET_UTIL_H
#define DCPLUSPLUS_DCPP_TARGET_UTIL_H

#include <airdcpp/typedefs.h>

#include <airdcpp/GetSet.h>
#include <airdcpp/Util.h>

#include <string>

namespace dcpp {
using std::string;

class TargetUtil {

// DEPRECEATED
public:
	class TargetInfo {
	public:
		explicit TargetInfo() { }
		explicit TargetInfo(const string& aPath, int64_t aFreeSpace = 0) : target(aPath), freeDiskSpace(aFreeSpace) { }

		bool isInitialized() { return freeDiskSpace != 0 || !target.empty(); }

		bool hasTarget() const noexcept {
			return !target.empty();
		}

		bool hasFreeSpace(int64_t aRequiredBytes) const noexcept {
			return freeDiskSpace >= aRequiredBytes;
		}

		GETSET(string, target, Target);
		IGETSET(int64_t, freeDiskSpace, FreeDiskSpace, 0);
	};

	typedef map<string, TargetInfo, noCaseStringLess> TargetInfoMap;

	enum TargetType {
		TARGET_PATH,
		TARGET_FAVORITE,
		TARGET_SHARE,
		TARGET_LAST
	};

	static void getVirtualTarget(const string& aTarget, TargetType targetType, TargetInfo& ti_);
private:
	static void getTarget(const OrderedStringSet& aTargets, TargetInfo& ti_);
	static void compareMap(const TargetInfoMap& targets, TargetInfo& retTi_);
};

}

#endif