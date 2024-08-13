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

#ifndef DCPLUSPLUS_DCPP_SHARE_TREE_H
#define DCPLUSPLUS_DCPP_SHARE_TREE_H

#include "DualString.h"
#include "DupeType.h"
#include "HashBloom.h"
#include "HashedFile.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "ShareDirectory.h"
#include "ShareDirectoryInfo.h"
#include "ShareStats.h"
#include "SortedVector.h"
#include "TempShareManager.h"
#include "TimerManager.h"
#include "UserConnection.h"

namespace dcpp {

class OutputStream;
class MemoryInputStream;
class SearchQuery;
class SharePathValidator;
class ShareRefreshInfo;

class ShareTree {
public:
	// Returns virtual path of a TTH
	// Throws ShareException
	string toVirtual(const TTHValue& aTTH) const;

	// Throws ShareException in case an invalid path is provided
	void search(SearchResultList& l, SearchQuery& aSearch, const OptionalProfileToken& aProfile, const UserPtr& aUser, const string& aDir, bool aIsAutoSearch = false);

	// Mostly for dupe check with size comparison (partial/exact dupe)
	// You may also give a path in NMDC format and the relevant 
	// directory (+ possible subdirectories) are detected automatically
	DupeType getAdcDirectoryDupe(const string& aAdcPath, int64_t aSize) const noexcept;

	// Returns the dupe paths by directory name/NMDC path
	StringList getAdcDirectoryDupePaths(const string& aAdcPath) const noexcept;

	bool isFileShared(const TTHValue& aTTH) const noexcept;
	bool isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept;


	GroupedDirectoryMap getGroupedDirectories() const noexcept;
	MemoryInputStream* generatePartialList(const string& aVirtualPath, bool aRecursive, const OptionalProfileToken& aProfile, const FilelistDirectory::DuplicateFileHandler& aDuplicateFileHandler) const noexcept;
	MemoryInputStream* generateTTHList(const string& aVirtualPath, bool aRecursive, ProfileToken aProfile) const noexcept;
	void toFilelist(OutputStream& os_, const string& aVirtualPath, const OptionalProfileToken& aProfile, bool aRecursive, const FilelistDirectory::DuplicateFileHandler& aDuplicateFileHandler) const;

	// Throws ShareException
	AdcCommand getFileInfo(const TTHValue& aTTH) const;

	int64_t getTotalShareSize(ProfileToken aProfile) const noexcept;
	
	// Adds all shared TTHs (permanent and temp) to the filter
	void getBloom(HashBloom& bloom) const noexcept;

	// Removes path characters from virtual name
	string validateVirtualName(const string& aName) const noexcept;

	bool isTTHShared(const TTHValue& tth) const noexcept;

	// Get real paths for an ADC virtual path
	// Throws ShareException
	void getRealPaths(const string& aVirtualPath, StringList& realPaths_, const OptionalProfileToken& aProfile = nullopt) const;

	StringList getRealPaths(const TTHValue& root) const noexcept;

	IGETSET(int64_t, sharedSize, SharedSize, 0);

	// Get real paths of all shared root directories
	StringList getRootPaths() const noexcept;

	ShareSearchStats getSearchMatchingStats() const noexcept;

	// Convert real path to virtual path. Returns an empty string if not shared.
	string realToVirtualAdc(const string& aPath, const OptionalProfileToken& aToken = nullopt) const noexcept;

	// Get information of all shared directories grouped by profile tokens
	ShareDirectoryInfoList getRootInfos() const noexcept;
	ShareDirectoryInfoPtr getRootInfo(const string& aPath) const noexcept;

	ShareTree();

	ShareRoot::Ptr addShareRoot(const string& aPath, const string& aVirtualName, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastModified, time_t aLastRefreshed) noexcept;
	ShareRoot::Ptr addShareRoot(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept;
	ShareRoot::Ptr updateShareRoot(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept;

	// Removes the root path entirely share (from all profiles)
	ShareRoot::Ptr removeShareRoot(const string& aPath) noexcept;

	// Change the refresh status for a directory and its subroots
	// Safe to call with non-root directories
	ShareRoot::Ptr setRefreshState(const string& aPath, ShareRootRefreshState aState, bool aUpdateRefreshTime, const optional<ShareRefreshTaskToken>& aRefreshTaskToken) const noexcept;

	typedef ShareDirectory::File::TTHMap HashFileMap;


	// Get directories matching the virtual path (root path is not accepted here)
	// Can be used with a single profile token or a set of them
	// Throws ShareException
	// Unsafe
	template<class T>
	void findVirtuals(const string& aVirtualPath, const T& aProfile, ShareDirectory::List& dirs_) const {
		ShareDirectory::List virtuals; //since we are mapping by realpath, we can have more than 1 same virtualnames
		if (aVirtualPath.empty() || aVirtualPath[0] != ADC_SEPARATOR) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		auto start = aVirtualPath.find(ADC_SEPARATOR, 1);
		if (start == string::npos || start == 1) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		getRootsByVirtual(aVirtualPath.substr(1, start - 1), aProfile, virtuals);
		if (virtuals.empty()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		for (const auto& root : virtuals) {
			string::size_type i = start; // always start from the begin.
			string::size_type j = i + 1;

			auto d = root;
			while ((i = aVirtualPath.find(ADC_SEPARATOR, j)) != string::npos) {
				d = d->findDirectoryLower(Text::toLower(aVirtualPath.substr(j, i - j)));
				if (!d) {
					break;
				}

				j = i + 1;
			}

			if (d) {
				dirs_.push_back(d);
			}
		}

		if (dirs_.empty()) {
			//if we are here it means we didnt find anything, throw.
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}
	}

	const ShareDirectory::Map& getRoots() const noexcept {
		return rootPaths;
	}

#ifdef _DEBUG
	// Go through the whole tree and check that the global maps have been filled properly
	void validateDirectoryTreeDebug() const noexcept;
	void validateDirectoryRecursiveDebug(const ShareDirectory::Ptr& dir, OrderedStringSet& directoryPaths_, OrderedStringSet& filePaths_) const noexcept;
#endif

	void countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles, size_t uniqueFiles, size_t& lowerCaseFiles, size_t& totalStrLen_, size_t& roots_) const noexcept;

	// Returns the dupe directories by directory name/ADC path
	void getDirectoriesByAdcName(const string& aAdcPath, ShareDirectory::List& dirs_) const noexcept;

	// Get real path and size for a virtual path
	// noAccess_ will be set to true if the file is availabe but not in the supplied profiles
	// Throws ShareException
	void toRealWithSize(const string& aVirtualPath, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_);

	// Attempts to find directory from share and returns the last existing directory
	// If the exact directory can't be found, the missing directory names are added in remainingTokens_
	ShareDirectory::Ptr findDirectory(const string& aRealPath, StringList& remainingTokens_) const noexcept;

	// Find an existing directory by real path
	ShareDirectory::Ptr findDirectory(const string& aRealPath) const noexcept;

	void addHashedFile(const string& aRealPath, const HashedFile& aFileInfo, ProfileTokenSet* dirtyProfiles) noexcept;

	ShareBloom* getBloom() const noexcept {
		return bloom.get();
	}

	void setBloom(ShareBloom* aBloom) noexcept {
		bloom.reset(aBloom);
	}

	TempShareManager& getTempShareManager() noexcept {
		return tempShare;
	}

	const TempShareManager& getTempShareManager() const noexcept {
		return tempShare;
	}

	bool applyRefreshChanges(ShareRefreshInfo& ri, ProfileTokenSet* aDirtyProfiles);

	// ShareDirectory::Ptr getDirectoryByRealPath(const string& aPath) const noexcept;
	ShareDirectory::File* findFile(const string& aPath) const noexcept;
	ShareDirectory::File::ConstSet findFiles(const TTHValue& aTTH) const noexcept;
private:
	TempShareManager tempShare;

	uint64_t totalSearches = 0;
	uint64_t tthSearches = 0;
	uint64_t recursiveSearches = 0;
	uint64_t recursiveSearchTime = 0;
	uint64_t filteredSearches = 0;
	uint64_t recursiveSearchesResponded = 0;
	uint64_t searchTokenCount = 0;
	uint64_t searchTokenLength = 0;
	uint64_t autoSearches = 0;

	typedef vector<ShareRoot::Ptr> ShareRootList;
	unique_ptr<ShareBloom> bloom;

	ShareDirectoryInfoPtr getRootInfo(const ShareDirectory::Ptr& aDir) const noexcept;

	HashFileMap tthIndex;

	// Map real name to virtual name - multiple real names may be mapped to a single virtual one
	ShareDirectory::Map rootPaths;

	// All directory names cached for easy lookups
	ShareDirectory::MultiMap lowerDirNameMap;

	bool addDirectoryResult(const ShareDirectory* aDir, SearchResultList& aResults, const OptionalProfileToken& aProfile, SearchQuery& srch) const noexcept;

	// Get root directories matching the provided token
	// Unsafe
	void getRootsByVirtual(const string& aVirtualName, const OptionalProfileToken& aProfile, ShareDirectory::List& dirs_) const noexcept;

	// Get root directories matching any of the provided tokens
	// Unsafe
	void getRootsByVirtual(const string& aVirtualName, const ProfileTokenSet& aProfiles, ShareDirectory::List& dirs_) const noexcept;

	// Get root directories by profile
	// Unsafe
	void getRoots(const OptionalProfileToken& aProfile, ShareDirectory::List& dirs_) const noexcept;

	// Attempt to add the path in share
	ShareDirectory::Ptr getDirectory(const string& aRealPath) noexcept;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SHARE_TREE_H)
