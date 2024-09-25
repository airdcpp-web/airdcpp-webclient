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

#ifndef DCPLUSPLUS_DCPP_HASHEDFILEINFO_H
#define DCPLUSPLUS_DCPP_HASHEDFILEINFO_H

#include <airdcpp/typedefs.h>
#include <airdcpp/GetSet.h>
#include <airdcpp/MerkleTree.h>

namespace dcpp {

class HashedFile {
public:
	HashedFile(uint64_t aTimeStamp = 0, int64_t aSize = -1) : timeStamp(aTimeStamp), size(aSize) {}
	HashedFile(const TTHValue& aRoot, uint64_t aTimeStamp, int64_t aSize) :
		root(aRoot), timeStamp(aTimeStamp), size(aSize) { }

	~HashedFile() = default;

	GETSET(TTHValue, root, Root);
	GETSET(uint64_t, timeStamp, TimeStamp);
	GETSET(int64_t, size, Size);
};

using RenameList = std::vector<pair<std::string, HashedFile>>;

}

#endif // !defined(DCPLUSPLUS_DCPP_HASHEDFILEINFO_H)