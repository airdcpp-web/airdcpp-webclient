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

#ifndef DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H
#define DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H

#include "forward.h"

#include "DirectoryListingListener.h"
#include "ClientManagerListener.h"
#include "ShareManagerListener.h"
#include "TimerManagerListener.h"

#include "ActionHook.h"
#include "DirectoryContentInfo.h"
#include "DispatcherQueue.h"
#include "DupeType.h"
#include "GetSet.h"
#include "HintedUser.h"
#include "Message.h"
#include "MerkleTree.h"
#include "Priority.h"
#include "QueueAddInfo.h"
#include "Speaker.h"
#include "StreamBase.h"
#include "TrackableDownloadItem.h"
#include "UserInfoBase.h"

namespace dcpp {

class DirectSearch;
class ListLoader;
class SearchQuery;

class DirectoryListing : public UserInfoBase, public TrackableDownloadItem,
	public Speaker<DirectoryListingListener>, private TimerManagerListener, 
	private ClientManagerListener, private ShareManagerListener
{
public:
	class Directory;
	class File: public boost::noncopyable {

	public:
		using Owner = const void*;
		using Ptr = std::shared_ptr<File>;

		struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };

		using List = std::vector<Ptr>;
		using Iter = List::const_iterator;
		
		File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, bool aCheckDupe, time_t aRemoteDate) noexcept;
		File(const File& rhs, Owner aOwner) noexcept;

		~File() = default;

		using ValidationHook = ActionHook<nullptr_t, const File::Ptr &, const DirectoryListing &>;

		string getAdcPathUnsafe() const noexcept {
			return parent->getAdcPathUnsafe() + name;
		}

		GETSET(string, name, Name);
		GETSET(int64_t, size, Size);
		GETSET(Directory*, parent, Parent);
		GETSET(TTHValue, tthRoot, TTH);
		IGETSET(DupeType, dupe, Dupe, DUPE_NONE);
		IGETSET(time_t, remoteDate, RemoteDate, 0);

		bool isInQueue() const noexcept;

		Owner getOwner() const noexcept {
			return owner;
		}
		DirectoryListingItemToken getToken() const noexcept {
			return token;
		}
		void getLocalPathsUnsafe(StringList& ret, const OptionalProfileToken& aShareProfileToken) const;
	private:
		Owner owner = nullptr;
		const DirectoryListingItemToken token;
	};

	enum class DirectoryLoadType {
		CHANGE_NORMAL,
		CHANGE_RELOAD,
		LOAD_CONTENT,
		NONE,
	};

	class Directory : public boost::noncopyable {
	public:
		enum DirType {
			TYPE_NORMAL,
			TYPE_INCOMPLETE_CHILD,
			TYPE_INCOMPLETE_NOCHILD,
			TYPE_VIRTUAL,
		};

		using Ptr = std::shared_ptr<Directory>;
		using ValidationHook = ActionHook<nullptr_t, const Directory::Ptr &, const DirectoryListing &>;

		struct ValidationHooks {
			ValidationHook directoryLoadHook;
			File::ValidationHook fileLoadHook;

			bool hasSubscribers() const noexcept {
				return directoryLoadHook.hasSubscribers() || fileLoadHook.hasSubscribers();
			}
		};

		struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };

		using List = std::vector<Ptr>;
		using TTHSet = unordered_set<TTHValue>;
		using Map = map<const string *, Ptr, noCaseStringLess>;
		
		Map directories;
		File::List files;

		static Directory::Ptr create(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, 
			bool aCheckDupe = false, const DirectoryContentInfo& aContentInfo = DirectoryContentInfo::uninitialized(),
			const string& aSize = Util::emptyString, time_t aRemoteDate = 0);

		virtual ~Directory();

		size_t getTotalFileCount(bool aCountVirtual) const noexcept;
		int64_t getTotalSize(bool aCountVirtual) const noexcept;
		void filterList(const DirectoryListing& dirList) noexcept;
		void filterList(TTHSet& l) noexcept;
		void getHashList(TTHSet& l) const noexcept;
		void clearVirtualDirectories() noexcept;
		void clearAll() noexcept;
		void getLocalPathsUnsafe(StringList& ret, const OptionalProfileToken& aShareProfileToken) const;

		bool findIncomplete() const noexcept;
		bool findCompleteChildren() const noexcept;
		void search(OrderedStringSet& aResults, SearchQuery& aStrings) const noexcept;
		void findFiles(const boost::regex& aReg, File::List& aResults) const noexcept;
		
		int64_t getFilesSizeUnsafe() const noexcept;

		string getAdcPathUnsafe() const noexcept;
		uint8_t checkDupesRecursive() noexcept;
		void runHooksRecursive(const DirectoryListing& aList) noexcept;
		
		IGETSET(int64_t, partialSize, PartialSize, 0);
		GETSET(Directory*, parent, Parent);
		GETSET(DirType, type, Type);
		IGETSET(DupeType, dupe, Dupe, DUPE_NONE);
		IGETSET(time_t, remoteDate, RemoteDate, 0);
		IGETSET(time_t, lastUpdateDate, LastUpdateDate, 0);
		IGETSET(DirectoryLoadType, loading, Loading, DirectoryLoadType::NONE);

		bool isComplete() const noexcept { return type == TYPE_VIRTUAL || type == TYPE_NORMAL; }
		void setComplete() noexcept { type = TYPE_NORMAL; }
		bool isVirtual() const noexcept { return type == TYPE_VIRTUAL; }
		bool isRoot() const noexcept { return !parent; }

		// Create recursive bundle file info listing with relative paths
		BundleFileAddData::List toBundleInfoList() const noexcept;

		const string& getName() const noexcept {
			return name;
		}

		// This function not thread safe as it will go through all complete directories
		DirectoryContentInfo getContentInfoRecursive(bool aCountVirtual) const noexcept;

		// Partial list content info only
		const DirectoryContentInfo& getContentInfo() const noexcept {
			return contentInfo;
		}

		void setContentInfo(const DirectoryContentInfo& aContentInfo) {
			contentInfo.files = aContentInfo.files;
			contentInfo.directories = aContentInfo.directories;
		}

		static bool NotVirtual(const Directory::Ptr& aDirectory) noexcept {
			return !aDirectory->isVirtual();
		}

		DirectoryListingItemToken getToken() const noexcept {
			return token;
		}
	protected:
		void toBundleInfoList(const string& aTarget, BundleFileAddData::List& aFiles) const noexcept;

		Directory(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, bool aCheckDupe, const DirectoryContentInfo& aContentInfo, const string& aSize, time_t aRemoteDate);

		void getContentInfo(size_t& directories_, size_t& files_, bool aCountVirtual) const noexcept;

		DirectoryContentInfo contentInfo = DirectoryContentInfo::uninitialized();
		const string name;
		const DirectoryListingItemToken token;
	};

	class VirtualDirectory : public Directory {
	public:
		using Ptr = shared_ptr<VirtualDirectory>;
		GETSET(string, fullAdcPath, FullAdcPath);
		static Ptr create(const string& aFullAdcPath, Directory* aParent, const string& aName, bool aAddToParent = true);
	private:
		VirtualDirectory(const string& aFullPath, Directory* aParent, const string& aName);
	};

	DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsClientView, Directory::ValidationHooks* aLoadHooks, bool aIsOwnList = false);
	~DirectoryListing() override;
	
	const CID& getToken() const noexcept {
		return hintedUser.user->getCID();
	}

	// Throws Exception, AbortException
	void loadFile();
	bool isLoaded() const noexcept;


	// Returns the number of loaded dirs
	// Throws AbortException
	int loadPartialXml(const string& aXml, const string& aAdcBase);

	optional<DirectoryBundleAddResult> createBundleHooked(const Directory::Ptr& aDir, const string& aTarget, const string& aName, Priority aPrio, string& errorMsg_) const noexcept;

	HintedUser getDownloadSourceUser() const noexcept;

	int64_t getTotalListSizeUnsafe() const noexcept { return root->getTotalSize(false); }
	int64_t getDirectorySizeUnsafe(const string& aDir) const noexcept;
	size_t getTotalFileCountUnsafe() const noexcept { return root->getTotalFileCount(false); }

	Directory::Ptr getRoot() const noexcept { return root; }

	// Throws ShareException
	void getLocalPathsUnsafe(const Directory::Ptr& d, StringList& ret) const;

	// Throws ShareException
	void getLocalPathsUnsafe(const File::Ptr& f, StringList& ret) const;

	bool isMyCID() const noexcept;
	string getNick(bool firstOnly) const noexcept;
	static string getNickFromFilename(const string& fileName) noexcept;
	static UserPtr getUserFromFilename(const string& fileName) noexcept;

	OptionalProfileToken getShareProfile() const noexcept;

	void addShareProfileChangeTask(ProfileToken aProfile) noexcept;
	void addHubUrlChangeTask(const string& aHubUrl) noexcept;

	void getPartialListInfo(int64_t& totalSize_, size_t& totalFiles_) const noexcept;
	
	const UserPtr& getUser() const noexcept override { return hintedUser.user; }
	const HintedUser& getHintedUser() const noexcept { return hintedUser; }
	const string& getHubUrl() const noexcept override { return hintedUser.hint; }
		
	GETSET(bool, partialList, PartialList);
	GETSET(string, fileName, FileName);
	IGETSET(bool, closing, Closing, false);

	void addListDiffTask(const string& aFile, bool aOwnList) noexcept;

	void addPartialListLoadTask(const string& aXml, const string& aBasePath, bool aBackgroundTask = false, const AsyncF& aCompletionF = nullptr) noexcept;
	void addOwnListLoadTask(const string& aBasePath, bool aBackgroundTask = false) noexcept;

	void addFullListTask(const string& aDir) noexcept;
	void addQueueMatchTask() noexcept;

	void addAsyncTask(Callback&& f) noexcept;
	void close() noexcept;

	void addSearchTask(const SearchPtr& aSearch) noexcept;

	bool nextResult(bool prev) noexcept;

	unique_ptr<SearchQuery> curSearch;

	bool isCurrentSearchPath(const string_view& aPath) const noexcept;
	size_t getResultCount() const noexcept { return searchResults.size(); }

	Directory::Ptr findDirectoryUnsafe(const string& aName) const noexcept { return findDirectoryUnsafe(aName, root.get()); }
	Directory::Ptr findDirectoryUnsafe(const string& aName, const Directory* current) const noexcept;
	
	bool supportsASCH() const noexcept;

	struct LocationInfo {
		int64_t totalSize = -1;
		int files = -1;
		int directories = -1;

		Directory::Ptr directory = nullptr;
	};

	const LocationInfo& getCurrentLocationInfo() const noexcept {
		return currentLocation;
	}

	void onListRemovedQueue(const string& aTarget, const string& aDir, bool aFinished) noexcept;
	void addDisableLoadingTask(const string& aTarget) noexcept;

	bool isRead() const noexcept {
		return read;
	}

	void setRead() noexcept;

	void addDirectoryChangeTask(const string& aPath, DirectoryLoadType aType, bool aForceQueue = false) noexcept;

	bool getIsOwnList() const noexcept {
		return isOwnList;
	}

	bool getIsClientView() const noexcept {
		return isClientView;
	}
protected:
	void onStateChanged() noexcept override;

private:
	Directory::ValidationHooks* loadHooks;

	const bool isOwnList;
	const bool isClientView;

	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	void setDirectoryLoadingState(const Directory::Ptr& aDir, DirectoryLoadType aLoading) noexcept;

	// Returns the number of loaded dirs
	// Throws AbortException
	int loadXML(InputStream& aXml, bool aUpdating, const string& aBase, time_t aListDate);

	// Create and insert a base directory with the given path (or return an existing one)
	Directory::Ptr createBaseDirectory(const string& aPath, time_t aDownloadDate);

	void changeDirectoryImpl(const string& aPath, DirectoryLoadType aType, bool aForceQueue = false) noexcept;

	void setShareProfileImpl(ProfileToken aProfile) noexcept;
	void setHubUrlImpl(const string_view& aHubUrl) noexcept;

	LocationInfo currentLocation;
	void updateCurrentLocation(const Directory::Ptr& aCurrentDirectory) noexcept;

	friend class ListLoader;

	Directory::Ptr root;

	void dispatch(Callback& aCallback) noexcept;

	atomic_flag running;

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool wasOffline) noexcept override;
	void on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept override;
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept override;

	void onUserUpdated(const UserPtr& aUser) noexcept;

	void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;

	// ShareManagerListener
	void on(ShareManagerListener::RefreshCompleted, const ShareRefreshTask& aTask, bool aSucceed, const ShareRefreshStats&) noexcept override;

	void endSearch(bool timedOut = false) noexcept;

	// Throws Exception, AbortException
	int loadShareDirectory(const string& aPath, bool aRecurse = false);

	OrderedStringSet searchResults;
	OrderedStringSet::iterator curResult;

	// Throws Exception, AbortException
	void listDiffImpl(const string& aFile, bool aOwnList);

	void updateStatus(const string& aMessage) noexcept;

	// Throws Exception, AbortException
	void loadFileImpl(const string& aInitialDir);
	void searchImpl(const SearchPtr& aSearch) noexcept;

	// Throws Exception, AbortException
	void loadPartialImpl(const string& aXml, const string& aBasePath, bool aBackgroundTask, const AsyncF& aCompletionF);

	void matchQueueImpl() noexcept;

	HintedUser hintedUser;
	bool read = false;

	void checkShareDupes() noexcept;
	void onLoadingFinished(int64_t aStartTime, const string& aLoadedPath, const string& aCurrentPath, bool aBackgroundTask) noexcept;

	unique_ptr<DirectSearch> directSearch;
	DispatcherQueue tasks;
};

inline bool operator==(const DirectoryListing::Directory::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }
inline bool operator==(const DirectoryListing::File::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)
