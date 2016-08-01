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

#ifndef DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H
#define DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H

#include "forward.h"

#include "DirectoryListingListener.h"
#include "ClientManagerListener.h"
#include "ShareManagerListener.h"
#include "TimerManagerListener.h"

#include "Bundle.h"
#include "DirectSearch.h"
#include "DispatcherQueue.h"
#include "DupeType.h"
#include "FastAlloc.h"
#include "GetSet.h"
#include "HintedUser.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "QueueItemBase.h"
#include "SearchQuery.h"
#include "TaskQueue.h"
#include "UserInfoBase.h"
#include "Streams.h"
#include "TargetUtil.h"
#include "TrackableDownloadItem.h"

namespace dcpp {

class ListLoader;
typedef uint32_t DirectoryListingToken;

class DirectoryListing : public intrusive_ptr_base<DirectoryListing>, public UserInfoBase, public TrackableDownloadItem,
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


		string getPath() const noexcept {
			return parent->getPath() + name;
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
		typedef List::const_iterator Iter;
		typedef unordered_set<TTHValue> TTHSet;
		
		List directories;
		File::List files;

		Directory(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, bool checkDupe = false, const string& aSize = Util::emptyString, time_t aRemoteDate = 0);

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
		
		size_t getFileCount() const noexcept { return files.size(); }
		size_t getFolderCount() const noexcept { return directories.size(); }
		
		int64_t getFilesSize() const noexcept;

		string getPath() const noexcept;
		uint8_t checkShareDupes() noexcept;
		
		GETSET(string, name, Name);
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

		void download(const string& aTarget, BundleFileInfo::List& aFiles) noexcept;
	};

	class AdlDirectory : public Directory {
	public:
		AdlDirectory(const string& aFullPath, Directory* aParent, const string& aName) : Directory(aParent, aName, Directory::TYPE_ADLS, GET_TIME()), fullPath(aFullPath) { }

		GETSET(string, fullPath, FullPath);
	};

	DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool isClientView, bool aIsOwnList=false);
	~DirectoryListing();
	
	void loadFile() throw(Exception, AbortException);
	bool isLoaded() const noexcept;


	// Returns the number of loaded dirs
	int loadPartialXml(const string& aXml, const string& aAdcBase) throw(AbortException);

	bool downloadDir(const string& aRemoteDir, const string& aTarget, QueueItemBase::Priority prio = QueueItem::DEFAULT, ProfileToken aAutoSearch = 0) noexcept;
	bool createBundle(Directory::Ptr& aDir, const string& aTarget, QueueItemBase::Priority prio, ProfileToken aAutoSearch) noexcept;

	bool viewAsText(const File::Ptr& aFile) const noexcept;

	int64_t getTotalListSize(bool adls = false) const noexcept { return root->getTotalSize(adls); }
	int64_t getDirSize(const string& aDir) const noexcept;
	size_t getTotalFileCount(bool adls = false) const noexcept { return root->getTotalFileCount(adls); }

	const Directory::Ptr getRoot() const noexcept { return root; }
	Directory::Ptr getRoot() noexcept { return root; }
	void getLocalPaths(const Directory::Ptr& d, StringList& ret) const throw(ShareException);
	void getLocalPaths(const File::Ptr& f, StringList& ret) const throw(ShareException);

	bool isMyCID() const noexcept;
	string getNick(bool firstOnly) const noexcept;
	static string getNickFromFilename(const string& fileName) noexcept;
	static UserPtr getUserFromFilename(const string& fileName) noexcept;

	ProfileToken getShareProfile() const noexcept;

	void addShareProfileChangeTask(ProfileToken aProfile) noexcept;
	void addHubUrlChangeTask(const string& aHubUrl) noexcept;

	void getPartialListInfo(int64_t& totalSize_, size_t& totalFiles_) const noexcept;
	
	const UserPtr& getUser() const noexcept { return hintedUser.user; }
	const HintedUser& getHintedUser() const noexcept { return hintedUser; }
	const string& getHubUrl() const noexcept { return hintedUser.hint; }
		
	GETSET(bool, partialList, PartialList);
	GETSET(bool, isOwnList, IsOwnList);
	GETSET(bool, isClientView, isClientView);
	GETSET(string, fileName, FileName);
	GETSET(bool, matchADL, MatchADL);
	IGETSET(bool, closing, Closing, false);

	typedef std::function<void(const string& aPath)> DupeOpenF;
	void addViewNfoTask(const string& aDir, bool aAllowQueueList, DupeOpenF aDupeF = nullptr) noexcept;
	void addMatchADLTask() noexcept;
	void addListDiffTask(const string& aFile, bool aOwnList) noexcept;
	void addPartialListTask(const string& aXml, const string& aBase, bool reloadAll = false, bool changeDir = true, std::function<void()> f = nullptr) noexcept;
	void addFullListTask(const string& aDir) noexcept;
	void addQueueMatchTask() noexcept;

	void addAsyncTask(DispatcherQueue::Callback&& f) noexcept;
	void close() noexcept;

	void addSearchTask(const SearchPtr& aSearch) noexcept;

	bool nextResult(bool prev) noexcept;

	unique_ptr<SearchQuery> curSearch = nullptr;

	bool isCurrentSearchPath(const string& path) const noexcept;
	size_t getResultCount() const noexcept { return searchResults.size(); }

	Directory::Ptr findDirectory(const string& aName) const noexcept { return findDirectory(aName, root); }
	Directory::Ptr findDirectory(const string& aName, const Directory::Ptr& current) const noexcept;
	
	bool supportsASCH() const noexcept;

	/* only call from the file list thread*/
	bool downloadDirImpl(Directory::Ptr& aDir, const string& aTarget, QueueItemBase::Priority prio, ProfileToken aAutoSearch) noexcept;
	void setActive() noexcept;

	enum ReloadMode {
		RELOAD_NONE,
		RELOAD_DIR,
		RELOAD_ALL
	};

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

	void addDirectoryChangeTask(const string& aPath, ReloadMode aReloadMode, bool aIsSearchChange = false) noexcept;
protected:
	void onStateChanged() noexcept;

private:
	// Returns the number of loaded dirs
	int loadXML(InputStream& aXml, bool aUpdating, const string& aBase, time_t aListDate = GET_TIME()) throw(AbortException);

	// Create and insert a base directory with the given path (or return an existing one)
	Directory::Ptr createBaseDirectory(const string& aPath, time_t aDownloadDate = GET_TIME()) noexcept;

	// Returns false if the directory was not found from the list
	bool changeDirectory(const string& aPath, ReloadMode aReloadMode, bool aIsSearchChange = false) noexcept;

	void setShareProfile(ProfileToken aProfile) noexcept;
	void setHubUrl(const string& aHubUrl) noexcept;

	LocationInfo currentLocation;
	void updateCurrentLocation(const Directory::Ptr& aCurrentDirectory) noexcept;

	friend class ListLoader;

	Directory::Ptr root;

	typedef unordered_map<string, pair<Directory::Ptr, bool>> DirBoolMap;

	// Maps loaded base directories (exact visited paths and their parents) with their full lowercase 
	// paths and whether they've been visited or not
	DirBoolMap baseDirs;

	void dispatch(DispatcherQueue::Callback& aCallback) noexcept;

	atomic_flag running;

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool wasOffline) noexcept;
	void on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept;
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept;

	void onUserUpdated(const UserPtr& aUser) noexcept;

	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;

	// ShareManagerListener
	void on(ShareManagerListener::DirectoriesRefreshed, uint8_t, const RefreshPathList& aPaths) noexcept;

	void endSearch(bool timedOut = false) noexcept;

	int loadShareDirectory(const string& aPath, bool aRecurse = false) throw(Exception, AbortException);

	OrderedStringSet searchResults;
	OrderedStringSet::iterator curResult;

	void listDiffImpl(const string& aFile, bool aOwnList) throw(Exception, AbortException);
	void loadFileImpl(const string& aInitialDir) throw(Exception, AbortException);
	void searchImpl(const SearchPtr& aSearch) noexcept;
	void loadPartialImpl(const string& aXml, const string& aBaseDir, bool reloadAll, bool changeDir, std::function<void()> completionF) throw(Exception, AbortException);
	void matchAdlImpl() throw(AbortException);
	void matchQueueImpl() noexcept;
	void findNfoImpl(const string& aPath, bool aAllowQueueList, DupeOpenF aDupeF) noexcept;

	HintedUser hintedUser;
	bool read = false;

	void checkShareDupes() noexcept;
	void onLoadingFinished(int64_t aStartTime, const string& aDir, bool aReloadList, bool aChangeDir) noexcept;

	unique_ptr<DirectSearch> directSearch;
	DispatcherQueue tasks;
};

inline bool operator==(const DirectoryListing::Directory::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }
inline bool operator==(const DirectoryListing::File::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)
