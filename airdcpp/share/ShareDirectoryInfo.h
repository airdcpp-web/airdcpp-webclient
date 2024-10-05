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

#ifndef DCPLUSPLUS_DCPP_SHAREDIR_INFO_H
#define DCPLUSPLUS_DCPP_SHAREDIR_INFO_H

#include <string>

#include <airdcpp/forward.h>
#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/core/types/DirectoryContentInfo.h>
#include <airdcpp/hash/value/MerkleTree.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/util/Util.h>
#include <airdcpp/util/ValueGenerator.h>

namespace dcpp {
	class ShareDirectoryInfo;
	typedef std::shared_ptr<ShareDirectoryInfo> ShareDirectoryInfoPtr;
	typedef vector<ShareDirectoryInfoPtr> ShareDirectoryInfoList;
	typedef std::set<ShareDirectoryInfoPtr, std::owner_less<ShareDirectoryInfoPtr>> ShareDirectoryInfoSet;
	typedef std::map<TTHValue, ShareDirectoryInfoPtr, std::owner_less<ShareDirectoryInfoPtr>> ShareDirectoryInfoMap;

	class ShareDirectoryInfo {
	public:

		ShareDirectoryInfo(const string& aPath, const string& aVname = Util::emptyString, bool aIncoming = false, const ProfileTokenSet& aProfiles = ProfileTokenSet()) :
			virtualName(aVname), path(aPath), incoming(aIncoming), profiles(aProfiles), id(ValueGenerator::generatePathId(aPath)) {
		
			if (virtualName.empty()) {
				virtualName = PathUtil::getLastDir(aPath);
			}

			if (profiles.empty()) {
				profiles.insert(SETTING(DEFAULT_SP));
			}
		}

		~ShareDirectoryInfo() {}

		string getToken() const noexcept {
			return id.toBase32();
		}

		void merge(const ShareDirectoryInfoPtr& aInfo) noexcept {
			virtualName = aInfo->virtualName;
			profiles = aInfo->profiles;
			incoming = aInfo->incoming;
			size = aInfo->size;
			lastRefreshTime = aInfo->lastRefreshTime;
			refreshState = aInfo->refreshState;
			refreshTaskToken = aInfo->refreshTaskToken;
			contentInfo = aInfo->contentInfo;
		}

		string virtualName;

		ProfileTokenSet profiles;

		const TTHValue id;
		const string path;
		bool incoming = false;

		int64_t size = 0;
		DirectoryContentInfo contentInfo = DirectoryContentInfo::empty();

		uint8_t refreshState = 0;
		time_t lastRefreshTime = 0;
		optional<ShareRefreshTaskToken> refreshTaskToken = nullopt;

		class PathCompare {
		public:
			PathCompare(const string& compareTo) : a(compareTo) { }
			bool operator()(const ShareDirectoryInfoPtr& p) { return Util::stricmp(p->path.c_str(), a.c_str()) == 0; }
			PathCompare& operator=(const PathCompare&) = delete;
		private:
			const string& a;
		};

		class IdCompare {
		public:
			IdCompare(const TTHValue& compareTo) : a(compareTo) { }
			bool operator()(const ShareDirectoryInfoPtr& p) { return p->id == a; }
			IdCompare& operator=(const IdCompare&) = delete;
		private:
			const TTHValue& a;
		};
	};

}

#endif