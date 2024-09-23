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

#ifndef DCPLUSPLUS_DCPP_LISTLOADER_H
#define DCPLUSPLUS_DCPP_LISTLOADER_H

#include "typedefs.h"

#include "DirectoryListingDirectory.h"
#include "SimpleXMLReader.h"

namespace dcpp {

class ListLoader : public SimpleXMLReader::CallBack {
public:
	ListLoader(DirectoryListing* aList, const string& aBase,
		bool aUpdating, time_t aListDownloadDate);

	~ListLoader() override = default;

	void startTag(const string& name, StringPairList& attribs, bool simple) override;
	void endTag(const string& name) override;

	void loadFile(StringPairList& attribs, bool simple);
	void loadDirectory(StringPairList& attribs, bool simple);
	void loadListing(StringPairList& attribs, bool simple);

	int getLoadedDirs() const noexcept { return dirsLoaded; }
private:
	void runHooksRecursive(const DirectoryListing::DirectoryPtr& aDir) noexcept;

	static DirectoryListing::Directory::DirType parseDirectoryType(bool aIncomplete, const DirectoryContentInfo& aContentInfo) noexcept;
	static void validateName(const string_view& aName);

	DirectoryListing* list;
	DirectoryListing::Directory* cur;

	bool inListing = false;
	int dirsLoaded = 0;

	const string base;
	const bool updating;
	const bool checkDupe;
	const bool partialList;
	const time_t listDownloadDate;
};

}
#endif