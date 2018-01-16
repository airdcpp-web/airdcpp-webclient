/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#include <api/FilelistItemInfo.h>


namespace webserver {
	DirectoryListingToken FilelistItemInfo::getToken() const noexcept {
		return hash<string>()(type == DIRECTORY ? dir->getName() : file->getName());
	}

	FilelistItemInfo::FilelistItemInfo(const DirectoryListing::File::Ptr& f) : type(FILE), file(f) {
		//dcdebug("FilelistItemInfo (file) %s was created\n", f->getName().c_str());
	}

	FilelistItemInfo::FilelistItemInfo(const DirectoryListing::Directory::Ptr& d) : type(DIRECTORY), dir(d) {
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
}