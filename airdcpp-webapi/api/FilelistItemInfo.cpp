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

#include "stdinc.h"

#include <api/FilelistItemInfo.h>


namespace webserver {
	DirectoryListingItemToken FilelistItemInfo::getToken() const noexcept {
		return type == DIRECTORY ? dir->getToken() : file->getToken();
	}

	FilelistItemInfo::FilelistItemInfo(const DirectoryListing::File::Ptr& f, const OptionalProfileToken aShareProfileToken) : file(f), shareProfileToken(aShareProfileToken), type(FILE) {
		//dcdebug("FilelistItemInfo (file) %s was created\n", f->getName().c_str());
	}

	FilelistItemInfo::FilelistItemInfo(const DirectoryListing::Directory::Ptr& d, const OptionalProfileToken aShareProfileToken) : dir(d), shareProfileToken(aShareProfileToken), type(DIRECTORY) {
		//dcdebug("FilelistItemInfo (directory) %s was created\n", d->getName().c_str());
	}

	FilelistItemInfo::~FilelistItemInfo() {
		//dcdebug("FilelistItemInfo %s was deleted\n", getName().c_str());

		// The member destructor is not called automatically in an union
		if (type == FILE) {
			file.~shared_ptr();
		} else {
			dir.~shared_ptr();
		}
	}

	void FilelistItemInfo::getLocalPathsThrow(StringList& paths_) const {
		// TODO
		if (type == DIRECTORY) {
			dir->getLocalPathsUnsafe(paths_, shareProfileToken);
		} else {
			file->getLocalPathsUnsafe(paths_, shareProfileToken);
		}
	}
}