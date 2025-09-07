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

#ifndef DCPLUSPLUS_DCPP_SHAREREFRESH_INFO_H
#define DCPLUSPLUS_DCPP_SHAREREFRESH_INFO_H

#include <string>

#include <airdcpp/forward.h>
#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/share/ShareDirectory.h>

namespace dcpp {

struct ShareRefreshStats {
	int64_t hashSize = 0;
	int64_t addedSize = 0;

	size_t existingDirectoryCount = 0;
	size_t existingFileCount = 0;
	size_t newDirectoryCount = 0;
	size_t newFileCount = 0;
	size_t skippedDirectoryCount = 0;
	size_t skippedFileCount = 0;

	bool isEmpty() const noexcept;
	void merge(const ShareRefreshStats& aOther) noexcept;
};

class ShareRefreshInfo : public ShareTreeMaps {
public:
	ShareRefreshInfo(const string& aPath, const ShareDirectory::Ptr& aOptionalOldRoot, time_t aLastWrite, ShareBloom& bloom_);
	~ShareRefreshInfo();

	ShareDirectory::Ptr optionalOldDirectory;
	ShareDirectory::Ptr newDirectory;

	ShareRefreshStats stats;

	string path;

	bool checkContent(const ShareDirectory::Ptr& aDirectory) noexcept;
	void applyRefreshChanges(ShareDirectory::MultiMap& lowerDirNameMap_, ShareDirectory::Map& rootPaths_, ShareDirectory::File::TTHMap& tthIndex_, int64_t& sharedBytes_, ProfileTokenSet* dirtyProfiles) noexcept;

	ShareRefreshInfo(ShareRefreshInfo&) = delete;
	ShareRefreshInfo& operator=(ShareRefreshInfo&) = delete;
};

}

#endif