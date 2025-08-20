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

#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/util/text/DualString.h>
#include <airdcpp/core/types/DupeType.h>
#include <airdcpp/hash/value/HashBloom.h>
#include <airdcpp/hash/HashedFile.h>
#include <airdcpp/hash/value/MerkleTree.h>
#include <airdcpp/share/ShareDirectory.h>
#include <airdcpp/share/ShareDirectoryInfo.h>
#include <airdcpp/share/ShareStats.h>
#include <airdcpp/core/classes/SortedVector.h>
#include <airdcpp/share/UploadFileProvider.h>
#include <airdcpp/connection/UserConnection.h>

namespace dcpp {

class OutputStream;
class MemoryInputStream;
class SearchQuery;
class SharePathValidator;
class ShareRefreshInfo;

#define SHARE_CACHE_VERSION "3"

class ShareTree : public UploadFileProvider, private ShareTreeMaps {
public:
	// Mostly for dupe check with size comparison (partial/exact dupe)
	// You may also give a path in NMDC format and the relevant 
	// directory (+ possible subdirectories) are detected automatically
	DupeType getAdcDirectoryDupe(const string& aAdcPath, int64_t aSize) const noexcept;

	// Returns the dupe paths by directory name/NMDC path
	StringList getAdcDirectoryDupePaths(const string& aAdcPath) const noexcept;

	bool isFileShared(const TTHValue& aTTH) const noexcept;
	bool isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept;

	void toTTHList(OutputStream& os_, const string& aVirtualPath, bool aRecursive, ProfileToken aProfile) const noexcept;

	void toFilelist(OutputStream& os_, const string& aVirtualPath, const OptionalProfileToken& aProfile, bool aRecursive, const FilelistDirectory::DuplicateFileHandler& aDuplicateFileHandler) const;
	void toCache(OutputStream& os_, const ShareDirectory::Ptr& aDirectory) const;

	// Throws ShareException
	AdcCommand getFileInfo(const TTHValue& aTTH) const;

	// Removes path characters from virtual name
	string validateVirtualName(const string& aName) const noexcept;

	// Get real paths for an ADC virtual path
	// Throws ShareException
	void getRealPaths(const string& aVirtualPath, StringList& realPaths_, const OptionalProfileToken& aProfile = nullopt) const;


	// UploadFileProvider
	const string providerName = "share";
	const string& getProviderName() const noexcept override {
		return providerName;
	}

	void getRealPaths(const TTHValue& root, StringList& aPaths) const noexcept override;

	// Get real path and size for a virtual path
	// noAccess_ will be set to true if the file is availabe but not in the supplied profiles
	// Throws ShareException
	bool toRealWithSize(const UploadFileQuery& aQuery, string& path_, int64_t& size_, bool& noAccess_) const noexcept override;

	// Adds all shared TTHs (permanent and temp) to the filter

	void getBloom(ProfileToken aToken, HashBloom& bloom_) const noexcept override;
	void getBloomFileCount(ProfileToken aToken, size_t& fileCount_) const noexcept override;

	void search(SearchResultList& results, const TTHValue& aTTH, const ShareSearch& aSearchInfo) const noexcept override;

	// Throws ShareException in case an invalid path is provided
	void searchText(SearchResultList& l, ShareSearch& aSearchInfo, ShareSearchCounters& counters_) const;

	IGETSET(int64_t, sharedSize, SharedSize, 0);

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

	using HashFileMap = ShareDirectory::File::TTHMap;

	using ProfileFormatter = std::function<string (const ProfileTokenSet &)>;
	void validateRootPath(const string& aRealPath, const ProfileFormatter& aProfileFormatter) const;

#ifdef _DEBUG
	// Go through the whole tree and check that the global maps have been filled properly
	void validateDirectoryTreeDebug() const noexcept;
	void validateDirectoryRecursiveDebugUnsafe(const ShareDirectory::Ptr& dir, OrderedStringSet& directoryPaths_, OrderedStringSet& filePaths_) const noexcept;
#endif

	void countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles, size_t& uniqueFiles, size_t& lowerCaseFiles, size_t& totalStrLen_, size_t& roots_) const noexcept;

	// Returns the dupe directories by directory name/ADC path
	void getDirectoriesByAdcNameUnsafe(const string& aAdcPath, ShareDirectory::List& dirs_) const noexcept;

	// Attempts to find directory from share and returns the last existing directory
	// If the exact directory can't be found, the missing directory names are added in remainingTokens_
	ShareDirectory::Ptr findDirectoryUnsafe(const string& aRealPath, StringList& remainingTokens_) const noexcept;

	// Find an existing directory by real path
	ShareDirectory::Ptr findDirectoryUnsafe(const string& aRealPath) const noexcept;

	bool findDirectoryByRealPath(const string& aPath, const ShareDirectoryCallback& aCallback = nullptr) const noexcept;
	bool findFileByRealPath(const string& aPath, const ShareFileCallback& aCallback = nullptr) const noexcept;

	void addHashedFile(const string& aRealPath, const HashedFile& aFileInfo, ProfileTokenSet* dirtyProfiles) noexcept;

	ShareBloom* getBloom() const noexcept {
		return bloom.get();
	}

	void setBloom(ShareBloom* aBloom) noexcept;

	bool applyRefreshChanges(ShareRefreshInfo& ri, ProfileTokenSet* aDirtyProfiles);

	ShareDirectory::File::ConstSet findFiles(const TTHValue& aTTH) const noexcept;

	void removeProfile(ProfileToken aProfile, StringList& rootsToRemove_) noexcept;
	void getProfileInfo(ProfileToken aProfile, int64_t& totalSize_, size_t& filesCount_) const noexcept;

	// Get root directories by profile
	ShareDirectory::List getRoots(const OptionalProfileToken& aProfile) const noexcept;

	ShareDirectory::Map getRootPaths() const noexcept;

	const ShareDirectory::Map& getRootPathsUnsafe() const noexcept {
		return rootPaths;
	}

	// Get real paths of all shared root directories
	StringList getRootPathList() const noexcept;
	ShareRootList getShareRoots() const noexcept;

	// Returns true if the path is inside a shared root (doesn't necessarily need to exist in share)
	string parseRoot(const string& aPath) const noexcept;

	SharedMutex& getCS() const noexcept { return cs; }
private:
	mutable SharedMutex cs;

	bool matchBloom(const SearchQuery& aSearch) const noexcept;

	unique_ptr<ShareBloom> bloom;

	ShareDirectoryInfoPtr getRootInfoUnsafe(const ShareDirectory::Ptr& aDir) const noexcept;

	bool addDirectoryResultUnsafe(const ShareDirectory* aDir, SearchResultList& aResults, const OptionalProfileToken& aProfile, const SearchQuery& srch) const noexcept;

	// Get root directories matching the provided token
	// Unsafe
	void getRootsByVirtualUnsafe(const string& aVirtualName, const OptionalProfileToken& aProfile, ShareDirectory::List& dirs_) const noexcept;

	// Get root directories matching any of the provided tokens
	// Unsafe
	void getRootsByVirtualUnsafe(const string& aVirtualName, const ProfileTokenSet& aProfiles, ShareDirectory::List& dirs_) const noexcept;

	// Attempt to add the path in share
	ShareDirectory::Ptr ensureDirectoryUnsafe(const string& aRealPath) noexcept;
	ShareDirectory::File* findFileUnsafe(const string& aPath) const noexcept;

	void getRootsUnsafe(const OptionalProfileToken& aProfile, ShareDirectory::List& dirs_) const noexcept;

	// Get root directory by exact path
	ShareDirectory::Ptr findRootUnsafe(const string& aRootPath) const noexcept;

	// Parse root from the given file/directory path
	ShareDirectory::Ptr parseRootUnsafe(const string& aRealPath) const noexcept;
	
	// Get directories matching the virtual path (root path is not accepted here)
	// Can be used with a single profile token or a set of them
	// Throws ShareException
	// Unsafe
	template<class T>
	void getDirectoriesByVirtualUnsafe(const string& aVirtualPath, const T& aProfile, ShareDirectory::List& dirs_) const {
		if (aVirtualPath.empty() || aVirtualPath[0] != ADC_SEPARATOR) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		auto start = aVirtualPath.find(ADC_SEPARATOR, 1);
		if (start == string::npos || start == 1) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		ShareDirectory::List virtuals; //since we are mapping by realpath, we can have more than 1 same virtual names
		{
			getRootsByVirtualUnsafe(aVirtualPath.substr(1, start - 1), aProfile, virtuals);
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
		}

		if (dirs_.empty()) {
			//if we are here it means we didnt find anything, throw.
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}
	}
#if defined(_DEBUG) && defined(_WIN32)
	static void testDualString();
#endif

};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SHARE_TREE_H)
