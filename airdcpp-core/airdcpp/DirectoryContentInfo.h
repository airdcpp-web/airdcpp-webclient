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

#ifndef DCPLUSPLUS_DCPP_DIRECTORY_CONTENT_INFO_H
#define DCPLUSPLUS_DCPP_DIRECTORY_CONTENT_INFO_H

#include "Util.h"

namespace dcpp {

// Recursively collected information about directory content
struct DirectoryContentInfo {
	static DirectoryContentInfo uninitialized() {
		return DirectoryContentInfo(-1, -1);
	}

	static DirectoryContentInfo empty() {
		return DirectoryContentInfo(0, 0);
	}

	DirectoryContentInfo(int aDirectories, int aFiles) : directories(aDirectories), files(aFiles) {  }

	int directories = 0;
	int files = 0;

	bool isInitialized() const noexcept {
		return directories >= 0 && files >= 0;
	}

	bool isEmpty() const noexcept {
		return directories == 0 && files == 0;
	}

	static int Sort(const DirectoryContentInfo& a, const DirectoryContentInfo& b) noexcept {
		if (a.directories != b.directories) {
			return compare(a.directories, b.directories);
		}

		return compare(a.files, b.files);
	}
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_DIRECTORY_CONTENT_INFO_H)
