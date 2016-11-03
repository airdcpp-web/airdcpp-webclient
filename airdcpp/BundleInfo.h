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

#ifndef DCPLUSPLUS_DCPP_BUNDLEINFO_H_
#define DCPLUSPLUS_DCPP_BUNDLEINFO_H_

#include "typedefs.h"

//#include "Bundle.h"
#include "HintedUser.h"
#include "MerkleTree.h"
#include "Priority.h"

namespace dcpp {


/*struct BundleDownloadInfo {
	string directoryName;
	string targetPath;

	Priority priority;
	void* owner;
};

struct BundleAddInfo {
	int filesAdded = 0;
	int filesFailed = 0;

	bool merged = false;
	BundlePtr bundle = nullptr;

	string errorMessage;
};*/

struct BundleFileInfo {
	BundleFileInfo(BundleFileInfo&& rhs) = default;
	BundleFileInfo& operator=(BundleFileInfo&& rhs) = default;
	BundleFileInfo(BundleFileInfo&) = delete;
	BundleFileInfo& operator=(BundleFileInfo&) = delete;

	BundleFileInfo(string aFile, const TTHValue& aTTH, int64_t aSize, time_t aDate = 0, Priority aPrio = Priority::DEFAULT) noexcept :
	file(move(aFile)), tth(aTTH), size(aSize), prio(aPrio), date(aDate) { }

	string file;
	TTHValue tth;
	int64_t size;
	Priority prio;
	time_t date;

	typedef vector<BundleFileInfo> List;
};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLEINFO_H_ */
