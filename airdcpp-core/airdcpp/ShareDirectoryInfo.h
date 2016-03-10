/*
* Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SHAREDIR_INFO_H
#define DCPLUSPLUS_DCPP_SHAREDIR_INFO_H

#include <string>

#include "forward.h"
#include "typedefs.h"
#include "Util.h"

namespace dcpp {
	class ShareDirectoryInfo;
	typedef std::shared_ptr<ShareDirectoryInfo> ShareDirectoryInfoPtr;
	typedef vector<ShareDirectoryInfoPtr> ShareDirectoryInfoList;

	class ShareDirectoryInfo {
	public:

		ShareDirectoryInfo(const string& aPath, const string& aVname = Util::emptyString, bool aIncoming = false, const ProfileTokenSet& aProfiles = ProfileTokenSet()) :
			virtualName(aVname), path(aPath), incoming(aIncoming), profiles(aProfiles) {
		
			if (virtualName.empty()) {
				virtualName = Util::getLastDir(aPath);
			}
		}

		~ShareDirectoryInfo() {}

		const string& getToken() const noexcept {
			return path;
		}

		void merge(const ShareDirectoryInfoPtr& aInfo) noexcept {
			virtualName = aInfo->virtualName;
			profiles = aInfo->profiles;
			incoming = aInfo->incoming;
			size = aInfo->size;
			lastRefreshTime = aInfo->lastRefreshTime;
			refreshState = aInfo->refreshState;
			fileCount = aInfo->fileCount;
			folderCount = aInfo->folderCount;
		}

		string virtualName;

		ProfileTokenSet profiles;

		const string path;
		bool incoming = false;

		int64_t size = 0;
		size_t fileCount = 0;
		size_t folderCount = 0;

		uint8_t refreshState = 0;
		time_t lastRefreshTime = 0;

		class PathCompare {
		public:
			PathCompare(const string& compareTo) : a(compareTo) { }
			bool operator()(const ShareDirectoryInfoPtr& p) { return Util::stricmp(p->path.c_str(), a.c_str()) == 0; }
			PathCompare& operator=(const PathCompare&) = delete;
		private:
			const string& a;
		};
	};

}

#endif