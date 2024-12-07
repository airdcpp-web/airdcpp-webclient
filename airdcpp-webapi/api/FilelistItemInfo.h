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

#ifndef DCPLUSPLUS_DCPP_FILELIST_ITEMINFO_H
#define DCPLUSPLUS_DCPP_FILELIST_ITEMINFO_H

#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/filelist/DirectoryListingDirectory.h>


namespace webserver {
	class FilelistItemInfo : public FastAlloc<FilelistItemInfo> {
	public:
		using Ptr = shared_ptr<FilelistItemInfo>;
		using List = vector<Ptr>;

		enum ItemType {
			FILE,
			DIRECTORY
		};

		union {
			const DirectoryListing::File::Ptr file;
			const DirectoryListing::Directory::Ptr dir;
		};

		FilelistItemInfo(const DirectoryListing::File::Ptr& f, const OptionalProfileToken aShareProfileToken);
		FilelistItemInfo(const DirectoryListing::Directory::Ptr& d, const OptionalProfileToken aShareProfileToken);
		~FilelistItemInfo();

		DupeType getDupe() const noexcept {
			if (shareProfileToken) {
				// Own filelist
				return DUPE_SHARE_FULL;
			}

			return type == DIRECTORY ? dir->getDupe() : file->getDupe(); 
		}
		const string& getName() const noexcept { return type == DIRECTORY ? dir->getName() : file->getName(); }
		string getAdcPath() const noexcept { return type == DIRECTORY ? dir->getAdcPathUnsafe() : file->getAdcPathUnsafe(); } // TODO
		bool isComplete() const noexcept { return type == DIRECTORY ? dir->isComplete() : true; }
		void getLocalPathsThrow(StringList& paths_) const;

		time_t getDate() const noexcept { return type == DIRECTORY ? dir->getRemoteDate() : file->getRemoteDate(); }
		int64_t getSize() const noexcept { return type == DIRECTORY ? dir->getTotalSize(false) : file->getSize(); }

		DirectoryListingItemToken getToken() const noexcept;

		ItemType getType() const noexcept {
			return type;
		}

		bool isDirectory() const noexcept {
			return type == DIRECTORY;
		}
	private:
		const OptionalProfileToken shareProfileToken;
		const ItemType type;
	};

	using FilelistItemInfoPtr = FilelistItemInfo::Ptr;
}

#endif