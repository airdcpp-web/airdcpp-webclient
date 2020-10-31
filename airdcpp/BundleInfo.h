/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_BUNDLEINFO_H_
#define DCPLUSPLUS_DCPP_BUNDLEINFO_H_

#include "typedefs.h"

#include "HintedUser.h"
#include "MerkleTree.h"
#include "Priority.h"

namespace dcpp {


struct BundleAddInfo {
	BundleAddInfo(const BundlePtr& aBundle, bool aMerged) : bundle(aBundle), merged(aMerged) {}
	BundleAddInfo() {}

	BundlePtr bundle = nullptr;
	bool merged = false;
};

struct DirectoryBundleAddInfo {
	int filesAdded = 0; // New files
	int filesUpdated = 0; // Source added
	int filesFailed = 0; // Adding failed
	//int filesExist = 0; // Files existing on disk already

	BundleAddInfo bundleInfo;

	typedef vector<DirectoryBundleAddInfo> List;
};

struct BundleDirectoryItemInfo {
	BundleDirectoryItemInfo(BundleDirectoryItemInfo&& rhs) = default;
	BundleDirectoryItemInfo& operator=(BundleDirectoryItemInfo&& rhs) = default;
	BundleDirectoryItemInfo(BundleDirectoryItemInfo&) = delete;
	BundleDirectoryItemInfo& operator=(BundleDirectoryItemInfo&) = delete;

	BundleDirectoryItemInfo(string aFile, const TTHValue& aTTH, int64_t aSize, Priority aPrio = Priority::DEFAULT) noexcept :
		file(move(aFile)), tth(aTTH), size(aSize), prio(aPrio) { }

	string file;
	TTHValue tth;
	int64_t size;
	Priority prio;

	typedef vector<BundleDirectoryItemInfo> List;
};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLEINFO_H_ */
