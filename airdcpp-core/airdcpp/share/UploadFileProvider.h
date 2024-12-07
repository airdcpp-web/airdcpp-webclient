/*
* Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_HASHED_FILE_PROVIDER_H
#define DCPLUSPLUS_DCPP_HASHED_FILE_PROVIDER_H

#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/hash/value/HashBloom.h>
#include <airdcpp/hash/value/MerkleTree.h>
#include <airdcpp/share/ShareSearchInfo.h>

namespace dcpp {

class Segment;
struct UploadFileQuery {
	UploadFileQuery(const TTHValue& aTTH) : tth(aTTH) {}
	UploadFileQuery(const TTHValue& aTTH, const UserPtr& aUser, const ProfileTokenSet* aProfiles, const Segment* aSegment) : tth(aTTH), user(aUser), profiles(aProfiles), segment(aSegment) {}

	const TTHValue& tth;
	const UserPtr user = nullptr;

	const ProfileTokenSet* profiles = nullptr;
	const Segment* segment = nullptr;

	bool enableAccessChecks() const noexcept {
		return !!profiles;
	}
};

// Common interface for a basic store that provides uploading (and search) functionality 
// Should be registered in ShareManager
class UploadFileProvider {
public:
	virtual bool toRealWithSize(const UploadFileQuery& aQuery, string& path_, int64_t& size_, bool& noAccess_) const noexcept = 0;
	virtual void getRealPaths(const TTHValue& root, StringList& aPaths) const noexcept = 0;
	virtual void getBloom(ProfileToken aToken, HashBloom& bloom_) const noexcept = 0;
	virtual void getBloomFileCount(ProfileToken aToken, size_t& fileCount_) const noexcept = 0;
	virtual void search(SearchResultList&, const TTHValue&, const ShareSearch&) const noexcept {}

	virtual const string& getProviderName() const noexcept = 0;
};

}

#endif