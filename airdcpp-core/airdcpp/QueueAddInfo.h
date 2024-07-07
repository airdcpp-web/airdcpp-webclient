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

#ifndef DCPLUSPLUS_DCPP_BUNDLEINFO_H_
#define DCPLUSPLUS_DCPP_BUNDLEINFO_H_

#include "typedefs.h"

#include "HintedUser.h"
#include "MerkleTree.h"
#include "Priority.h"

namespace dcpp {


// Queue results
struct BundleAddInfo {
	BundleAddInfo(const BundlePtr& aBundle, bool aMerged) : bundle(aBundle), merged(aMerged) {}
	BundleAddInfo() {}

	BundlePtr bundle = nullptr;
	bool merged = false;
};

struct DirectoryBundleAddResult {
	int filesAdded = 0; // New files
	int filesUpdated = 0; // Source added
	int filesFailed = 0; // Adding failed
	//int filesExist = 0; // Files existing on disk already

	BundleAddInfo bundleInfo;
};


// Adding bundles
struct BundleAddOptions {
	BundleAddOptions(string aTarget, const HintedUser& aOptionalUser, const void* aCaller) noexcept :
		target(std::move(aTarget)), optionalUser(aOptionalUser), caller(aCaller) { }

	string target;
	HintedUser optionalUser;
	const void* caller;
};

struct BundleAddData {
	BundleAddData(string aName, Priority aPrio, time_t aDate) noexcept :
		name(std::move(aName)), prio(aPrio), date(aDate) { }

	string name;
	Priority prio;
	time_t date;
};

struct BundleFileAddData : public BundleAddData {
	BundleFileAddData(string aFile, const TTHValue& aTTH, int64_t aSize, Priority aPrio, time_t aDate) noexcept :
		BundleAddData(std::move(aFile), aPrio, aDate), tth(aTTH), size(aSize) { }

	TTHValue tth;
	int64_t size;

	typedef vector<BundleFileAddData> List;
};

// Filelist
struct FilelistAddData {
	FilelistAddData(const HintedUser& aUser, const void* aCaller, const string& aListPath) noexcept :
		user(aUser), caller(aCaller), listPath(aListPath) { }

	HintedUser user;
	const void* caller;
	string listPath;
};

// Viewed files
struct ViewedFileAddData {
	ViewedFileAddData(string aFile, const TTHValue& aTTH, int64_t aSize, const void* aCaller, const HintedUser& aUser, bool aIsText) noexcept :
		file(std::move(aFile)), tth(aTTH), size(aSize), caller(aCaller), user(aUser), isText(aIsText) { }

	string file;
	TTHValue tth;
	int64_t size;
	const void* caller;
	HintedUser user;
	bool isText;
};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLEINFO_H_ */
