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

#ifndef DCPLUSPLUS_DCPP_TEMPSHARE_ITEM_H
#define DCPLUSPLUS_DCPP_TEMPSHARE_ITEM_H

#include <airdcpp/typedefs.h>

#include <airdcpp/MerkleTree.h>

namespace dcpp {

typedef uint32_t TempShareToken;
struct TempShareInfo {
	TempShareInfo(const string& aName, const string& aRealPath, int64_t aSize, const TTHValue& aTTH, const UserPtr& aUser) noexcept;

	const TempShareToken id;
	const string name;
	const UserPtr user; //CID or hubUrl
	const string realPath; //filepath
	const int64_t size; //filesize
	const TTHValue tth;
	const time_t timeAdded;

	bool hasAccess(const UserPtr& aUser) const noexcept;

	string getVirtualPath() const noexcept {
		return "/tmp/" + name;
	}
};

typedef vector<TempShareInfo> TempShareInfoList;

}

#endif