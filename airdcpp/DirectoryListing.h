/*
 * Copyright (C) 2001-2021 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H
#define DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H

#include "forward.h"

#include "DirectoryListingListener.h"
#include "ClientManagerListener.h"
#include "ShareManagerListener.h"
#include "TimerManagerListener.h"

#include "QueueAddInfo.h"
#include "DirectSearch.h"
#include "DispatcherQueue.h"
#include "DupeType.h"
#include "GetSet.h"
#include "HintedUser.h"
#include "Message.h"
#include "MerkleTree.h"
#include "Priority.h"
#include "TaskQueue.h"
#include "UserInfoBase.h"
#include "Streams.h"
#include "TrackableDownloadItem.h"

namespace dcpp {

class ListLoader;
class SearchQuery;
typedef uint32_t DirectoryListingToken;

class DirectoryListing : public UserInfoBase, public TrackableDownloadItem,
	public Speaker<DirectoryListingListener>, private TimerManagerListener, 
	private ClientManagerListener, private ShareManagerListener
{
public:
	class Directory;
	class File : boost::noncopyable {

	public:
		typedef std::shared_ptr<File> Ptr;

		struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };

		typedef std::vector<Ptr> List;
		typedef List::const_iterator Iter;
		
		File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, bool checkDupe, time_t aRemoteDate) noexcept;
		File(const File& rhs, bool _adls = false) noexcept;

		~File() { }


		string getAdcPath() const noexcept {
			return parent->getAdcPath() + name;
		}

		GETSET(string, name, Name);
		GETSET(int64_t, size, Size);
		GETSET(Directory*, parent, Parent);
		GETSET(TTHValue, tthRoot, TTH);
		IGETSET(bool, adls, Adls, false);
		IGETSET(DupeType, dupe, Dupe, DUPE_NONE);
		IGETSET(time_t, remoteDate, RemoteDate, 0);

		bool isInQueue() const noexcept;
	};

	class Directory : boost::noncopyable {
	public:
		enum DirType {
			TYPE_NORMAL,
			TYPE_INCOMPLETE_CHILD,
			TYPE_INCOMPLETE_NOCHILD,
			TYPE_ADLS,
		};

		typedef std::shared_ptr<Directory> Ptr;

		struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };

		typedef std::vector<Ptr> List;
		typedef unordered_set<TTHValue> TTHSet;
		typedef map<const string*, Ptr, noCaseStringLess> Map;
		
		Map directories;
		File::List files;

		static Directory::Ptr create(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, 
			bool checkDupe = false, const DirectoryContentInfo& aContentInfo = DirectoryContentInfo(),
			const string& aSize = Util::emptyString, time_t aRemoteDate = 0);

		virtual ~Directory();

		size_t getTotalFileCount(bool countAdls) const noexcept;
		int64_t getTotalSize(bool countAdls) const noexcept;
		void filterList(DirectoryListing& dirList) noexcept;
		void filterList(TTHSet& l) noexcept;
		void getHashList(TTHSet& l) const noexcept;
		void clearAdls() noexcept;
		void clearAll() noexcept;

		bool findIncomplete() const noexcept;
		void search(OrderedStringSet& aResults, SearchQuery& aStrings) const noexcept;
		void findFiles(const boost::regex& aReg, File::List& aResults) const noexcept;
		
		int64_t getFilesSize() const noexcept;

		string getAdcPath() const noexcept;
		uint8_t checkShareDupes() noexcept;
		
		IGETSET(int64_t, partialSize, PartialSize, 0);
		GETSET(Directory*, parent, Parent);
		GETSET(DirType, type, Type);
		IGETSET(DupeType, dupe, Dupe, DUPE_NONE);
		IGETSET(time_t, remoteDate, RemoteDate, 0);
		IGETSET(time_t, lastUpdateDate, LastUpdateDate, 0);
		IGETSET(bool, loading, Loading, false);

		bool isComplete() const noexcept { return type == TYPE_ADLS || type == TYPE_NORMAL; }
		void setComplete() noexcept { type = TYPE_NORMAL; }
		bool getAdls() const noexcept { return type == TYPE_ADLS; }

		// Create recursive bundle file info listing with relative paths
		BundleFileAddData::List toBundleInfoList() const noexcept;

		const string& getName() const noexcept {
			return name;
		}

		// This function not thread safe as it will go through all complete directories
		DirectoryContentInfo getContentInfoRecursive(bool aCountAdls) const noexcept;

		// Partial list content info only
		const DirectoryContentInfo& getContentInfo() const noexcept {
			return contentInfo;
		}

		void setContentInfo(const DirectoryContentInfo& aContentInfo) {
			contentInfo.files = aContentInfo.files;
			contentInfo.directories = aContentInfo.directories;
		}
	protected:
		void toBundleInfoList(const string& aTarget, BundleFileAddData::List& aFiles) const noexcept;

		Directory(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, bool aCheckDupe, const DirectoryContentInfo& aContentInfo, const string& aSize, time_t aRemoteDate);

		void getContentInfo(size_t& directories_, size_t& files_, bool aCountAdls) const noexcept;

		DirectoryContentInfo contentInfo;
		const string name;
	};

	class AdlDirectory : public Directory {
	public:
		typedef shared_ptr<AdlDirectory> Ptr;
		GETSET(string, fullAdcPath, FullAdcPath);
		static Ptr create(const string& aFullAdcPath, Directory* aParent, const string& aName);
	private:
		AdlDirectory(const string& aFullPath, Directory* aParent, const string& aName);
	};

	DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool isClientView, bool aIsOwnList=false);
	~DirectoryListing();
	
	const CID& getToken() const noexcept {
		return hintedUser.user->getCID();
	}

	// Throws Exception, AbortException
	void loadFile();
	bool isLoaded() const noexcept;


	// Returns the number of loaded dirs
	// Throws AbortException
	int loadPartialXml(const string& aXml, const string& aAdcBase);

	optional<DirectoryBundleAddResult> createBundleHooked(const Directory::Ptr& aDir, const string& aTarget, const string& aName, Priority aPrio, string& errorMsg_) noexcept;

	HintedUser getDownloadSourceUser() const noexcept;

	int64_t getTotalListSize(bool adls = false) const noexcept { return root->getTotalSize(adls); }
	int64_t getDirSize(const string& aDir) const noexcept;
	size_t getTotalFileCount(bool adls = false) const noexcept { return root->getTotalFileCount(adls); }

	const Directory::Ptr getRoot() const noexcept { return root; }
	Directory::Ptr getRoot() noexcept { return root; }

	// Throws ShareException
	void getLocalPaths(const Directory::Ptr& d, StringList& ret) const;

	// Throws ShareException
	void getLocalPaths(const File::Ptr& f, StringList& ret) const;

	bool isMyCID() const noexcept;
	string getNick(bool firstOnly) const noexcept;
	static string getNickFromFilename(const string& fileName) noexcept;
	static UserPtr getUserFromFilename(const string& fileName) noexcept;

	ProfileToken getShareProfile() const noexcept;

	void addShareProfileChangeTask(ProfileToken aProfile) noexcept;
	void addHubUrlChangeTask(const string& aHubUrl) noexcept;

	void getPartialListInfo(int64_t& totalSize_, size_t& totalFiles_) const noexcept;
	
	const UserPtr& getUser() const noexcept override { return hintedUser.user; }
	const HintedUser& getHintedUser() const noexcept { return hintedUser; }
	const string& getHubUrl() const noexcept override { return hintedUser.hint; }
		
	GETSET(bool, partialList, PartialList);
	GETSET(bool, isOwnList, IsOwnList);
	GETSET(bool, isClientView, IsClientView);
	GETSET(string, fileName, FileName);
	GETSET(bool, matchADL, MatchADL);
	IGETSET(bool, closing, Closing, false);

	void addMatchADLTask() noexcept;
	void addListDiffTask(const string& aFile, bool aOwnList) noexcept;

	void addPartialListTask(const string& aXml, const string& aBase, bool aBackgroundTask = false, const AsyncF& aCompletionF = nullptr) noexcept;
	void addFullListTask(const string& aDir) noexcept;
	void addQueueMatchTask() noexcept;

	void addAsyncTask(DispatcherQueue::Callback&& f) noexcept;
	void close() noexcept;

	void addSearchTask(const SearchPtr& aSearch) noexcept;

	bool nextResult(bool prev) noexcept;

	unique_ptr<SearchQuery> curSearch;

	bool isCurrentSearchPath(const string& path) const noexcept;
	size_t getResultCount() const noexcept { return searchResults.size(); }

	Directory::Ptr findDirectory(const string& aName) const noexcept { return findDirectory(aName, root.get()); }
	Directory::Ptr findDirectory(const string& aName, const Directory* current) const noexcept;
	
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

	bool isRead() const noexcept {
		return read;
	}

	void setRead() noexcept;

	void addDirectoryChangeTask(const string& aPath, bool aReload, bool aIsSearchChange = false, bool aForceQueue = false) noexcept;
protected:
	void onStateChanged() noexcept override;

private:
	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	void setDirectoryLoadingState(const Directory::Ptr& aDir, bool aLoading) noexcept;

	// Returns the number of loaded dirs
	// Throws AbortException
	int loadXML(InputStream& aXml, bool aUpdating, const string& aBase, time_t aListDate = GET_TIME());

	// Create and insert a base directory with the given path (or return an existing one)
	Directory::Ptr createBaseDirectory(const string& aPath, time_t aDownloadDate = GET_TIME()) noexcept;

	void changeDirectoryImpl(const string& aPath, bool aReload, bool aIsSearchChange = false, bool aForceQueue = false) noexcept;

	void setShareProfileImpl(ProfileToken aProfile) noexcept;
	void setHubUrlImpl(const string& aHubUrl) noexcept;

	LocationInfo currentLocation;
	void updateCurrentLocation(const Directory::Ptr& aCurrentDirectory) noexcept;

	friend class ListLoader;

	Directory::Ptr root;

	void dispatch(DispatcherQueue::Callback& aCallback) noexcept;

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

	// Throws Exception, AbortException
	void loadFileImpl(const string& aInitialDir);
	void searchImpl(const SearchPtr& aSearch) noexcept;

	// Throws Exception, AbortException
	void loadPartialImpl(const string& aXml, const string& aBasePath, bool aBackgroundTask, const AsyncF& aCompletionF);

	// Throws AbortException
	void matchAdlImpl();
	void matchQueueImpl() noexcept;

	HintedUser hintedUser;
	bool read = false;

	void checkShareDupes() noexcept;
	void onLoadingFinished(int64_t aStartTime, const string& aDir, bool aBackgroundTask) noexcept;

	unique_ptr<DirectSearch> directSearch;
	DispatcherQueue tasks;
};

inline bool operator==(const DirectoryListing::Directory::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }
inline bool operator==(const DirectoryListing::File::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)
