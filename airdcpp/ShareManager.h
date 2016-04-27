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

#ifndef DCPLUSPLUS_DCPP_SHARE_MANAGER_H
#define DCPLUSPLUS_DCPP_SHARE_MANAGER_H


#include "DirectoryMonitorListener.h"
#include "QueueManagerListener.h"
#include "SettingsManagerListener.h"
#include "ShareManagerListener.h"
#include "TimerManagerListener.h"

#include "BloomFilter.h"
#include "CriticalSection.h"
#include "DirectoryMonitor.h"
#include "DualString.h"
#include "DupeType.h"
#include "Exception.h"
#include "HashBloom.h"
#include "HashedFile.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "SearchQuery.h"
#include "ShareDirectoryInfo.h"
#include "ShareProfile.h"
#include "Singleton.h"
#include "SortedVector.h"
#include "StringMatch.h"
#include "StringSearch.h"
#include "TaskQueue.h"
#include "Thread.h"
#include "TimerManager.h"
#include "UserConnection.h"

namespace dcpp {

class File;
class OutputStream;
class MemoryInputStream;
class SearchQuery;

class FileList;

class ShareManager : public Singleton<ShareManager>, public Speaker<ShareManagerListener>, private Thread, private SettingsManagerListener, 
	private TimerManagerListener, private QueueManagerListener, private DirectoryMonitorListener
{
public:
	// Call when a drive has been removed and it should be removed from monitoring
	// Monitoring won't fail it otherwise and the monitoring will neither be restored if the device is readded
	void deviceRemoved(const string& aDrive);

	// Prepares the skiplist regex after the pattern has been changed
	void setSkipList();

	// Check if a directory/file name matches skiplist
	bool matchSkipList(const string& aName) const noexcept { return skipList.match(aName); }

	// Comprehensive check for a directory/file whether it is valid to be added in share
	// Use validatePath for new root directories instead
	bool checkSharedName(const string& fullPath, const string& fullPathLower, bool dir, bool report = true, int64_t size = 0) const noexcept;

	// Validate that the profiles are valid for the supplied path (sub/parent directory matching)
	// Existing profiles shouldn't be supplied
	void validateNewRootProfiles(const string& realPath, const ProfileTokenSet& aProfiles) const throw(ShareException);

	// Check that the root path is valid to be added in share
	// Use checkSharedName for non-root directories
	void validateRootPath(const string& realPath) const throw(ShareException);

	// Returns virtual path of a TTH
	string toVirtual(const TTHValue& aTTH, ProfileToken aProfile) const throw(ShareException);

	// Returns size and file name of a filelist
	// virtualFile = name requested by the other user (Transfer::USER_LIST_NAME_BZ or Transfer::USER_LIST_NAME)
	pair<int64_t, string> getFileListInfo(const string& virtualFile, ProfileToken aProfile) throw(ShareException);

	// Get real path and size for a virtual path
	// noAccess_ will be set to true if the file is availabe but not in the supplied profiles
	void toRealWithSize(const string& virtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_) throw(ShareException);

	// Returns TTH value for a file list (not very useful but the ADC specs...)
	// virtualFile = name requested by the other user (Transfer::USER_LIST_NAME_BZ or Transfer::USER_LIST_NAME)
	TTHValue getListTTH(const string& virtualFile, ProfileToken aProfile) const throw(ShareException);
	
	enum RefreshType: uint8_t {
		TYPE_MANUAL,
		TYPE_SCHEDULED,
		TYPE_STARTUP_BLOCKING,
		TYPE_STARTUP_DELAYED,
		TYPE_MONITORING,
		TYPE_BUNDLE
	};

	enum TaskType: uint8_t {
		ASYNC,
		ADD_DIR,
		REFRESH_ALL,
		REFRESH_DIRS,
		REFRESH_INCOMING,
		ADD_BUNDLE
	};

	enum class RefreshResult {
		REFRESH_STARTED = 0,
		REFRESH_PATH_NOT_FOUND = 1,
		REFRESH_IN_PROGRESS = 2,
		REFRESH_ALREADY_QUEUED = 3
	};

	// Refresh the whole share or in
	RefreshResult refresh(bool incoming, RefreshType aType = RefreshType::TYPE_MANUAL, function<void(float)> progressF = nullptr) noexcept;

	// Refresh a single single path or all paths under a virtual name
	RefreshResult refreshVirtual(const string& aDir) noexcept;

	// Refresh the specific directories
	// This validates that each path exists
	RefreshResult refreshPaths(const StringList& aPaths, const string& displayName = Util::emptyString, function<void(float)> progressF = nullptr) noexcept;

	bool isRefreshing() const noexcept { return refreshRunning; }
	
	// aIsMajor will regenerate the file list on next time when someone requests it
	void setProfilesDirty(ProfileTokenSet aProfiles, bool aIsMajor) noexcept;

	void startup(function<void(const string&)> stepF, function<void(float)> progressF) noexcept;
	void shutdown(function<void(float)> progressF) noexcept;

	// Should only be called on shutdown for now
	void abortRefresh() noexcept;

	void nmdcSearch(SearchResultList& l, const string& aString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults, bool aHideShare) noexcept;
	void search(SearchResultList& l, SearchQuery& aSearch, OptionalProfileToken aProfile, const CID& cid, const string& aDir, bool isAutoSearch = false) throw(ShareException);

	// Check if a directory is shared
	// You may also give a path in NMDC format and the relevant 
	// directory (+ possible subdirectories) are detected automatically
	bool isDirShared(const string& aDir) const noexcept;

	// Mostly for dupe check with size comparison (partial/exact dupe)
	DupeType isDirShared(const string& aPath, int64_t aSize) const noexcept;

	bool isFileShared(const TTHValue& aTTH) const noexcept;
	bool isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept;
	bool isRealPathShared(const string& aPath) const noexcept;

	bool allowAddDir(const string& dir) const noexcept;

	// Returns the dupe paths by directory name/NMDC path
	StringList getDirPaths(const string& aDir) const noexcept;

	vector<pair<string, StringList>> getGroupedDirectories() const noexcept;
	MemoryInputStream* generatePartialList(const string& dir, bool recurse, OptionalProfileToken aProfile) const noexcept;
	MemoryInputStream* generateTTHList(const string& dir, bool recurse, ProfileToken aProfile) const noexcept;
	MemoryInputStream* getTree(const string& virtualFile, ProfileToken aProfile) const noexcept;

	void saveXmlList(function<void (float)> progressF = nullptr) noexcept;	//for filelist caching

	AdcCommand getFileInfo(const string& aFile, ProfileToken aProfile) throw(ShareException);

	int64_t getTotalShareSize(ProfileToken aProfile) const noexcept;

	// Get share size and number of files for a specified profile
	void getProfileInfo(ProfileToken aProfile, int64_t& size, size_t& files) const noexcept;
	
	// Adds all shared TTHs (permanent and temp) to the filter
	void getBloom(HashBloom& bloom) const noexcept;

	// Removes path characters from virtual name
	string validateVirtualName(const string& aName) const noexcept;

	// Generate own full filelist on disk
	string generateOwnList(ProfileToken aProfile) throw(ShareException);

	bool isTTHShared(const TTHValue& tth) const noexcept;

	// Get real paths for an ADC virtual path
	void getRealPaths(const string& path, StringList& ret, ProfileToken aProfile) const throw(ShareException);

	StringList getRealPaths(const TTHValue& root) const noexcept;

	IGETSET(bool, monitorDebug, MonitorDebug, false);
	IGETSET(size_t, hits, Hits, 0);
	IGETSET(int64_t, sharedSize, SharedSize, 0);

	//tempShares
	struct TempShareInfo {
		TempShareInfo(const string& aKey, const string& aPath, int64_t aSize) : key(aKey), path(aPath), size(aSize) { }
		
		string key; //CID or hubUrl
		string path; //filepath
		int64_t size; //filesize
	};

	typedef unordered_multimap<TTHValue, TempShareInfo> TempShareMap;
	TempShareMap tempShares;
	void addTempShare(const string& aKey, const TTHValue& tth, const string& filePath, int64_t aSize, ProfileToken aProfile);

	// GUI only
	bool hasTempShares() { return !tempShares.empty(); }

	// GUI only
	TempShareMap& getTempShares() { return tempShares; }

	void removeTempShare(const string& aKey, const TTHValue& tth);
	void clearTempShares();
	bool isTempShared(const string& aKey, const TTHValue& tth);
	//tempShares end

	// Get real paths of all shared root directories
	void getRootPaths(StringList& aDirs) const noexcept;

	// Get a printable version of various share-related statistics
	string printStats() const noexcept;

	struct ShareStats {
		int profileCount = 0;
		size_t profileDirectoryCount = 0;

		int64_t totalSize = 0;
		size_t totalFileCount = 0;
		size_t totalDirectoryCount = 0;
		size_t uniqueFileCount = 0;
		double lowerCasePercentage = 0;
		double uniqueFilePercentage = 0;
		double rootDirectoryPercentage = 0;
		double filesPerDirectory = 0;
		double averageNameLength = 0;
		size_t totalNameSize = 0;
		time_t averageFileAge = 0;
	};
	optional<ShareStats> getShareStats() const noexcept;

	void addRootDirectories(const ShareDirectoryInfoList& aNewDirs) noexcept;
	void updateRootDirectories(const ShareDirectoryInfoList& renameDirs) noexcept;
	//void removeDirectories(const ShareDirectoryInfoList& removeDirs) noexcept;
	void removeRootDirectories(const StringList& removeDirs) noexcept;

	bool addRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept;
	bool updateRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept;

	// Removes the root path entirely share (from all profiles)
	bool removeRootDirectory(const string& aPath) noexcept;

	void addProfiles(const ShareProfileInfo::List& aProfiles) noexcept;
	void removeProfiles(const ShareProfileInfo::List& aProfiles) noexcept;
	void renameProfiles(const ShareProfileInfo::List& aProfiles) noexcept;

	void addProfile(const ShareProfilePtr& aProfile) noexcept;
	void updateProfile(const ShareProfilePtr& aProfile) noexcept;
	bool removeProfile(ProfileToken aToken) noexcept;

	// Convert real path to virtual path. Returns an empty string if not shared.
	string realToVirtual(const string& aPath, const OptionalProfileToken& aToken = boost::none) const noexcept;

	// If allowFallback is true, the default profile will be returned if the requested one is not found
	ShareProfilePtr getShareProfile(ProfileToken aProfile, bool allowFallback = false) const noexcept;

	// Get information of all shared directories grouped by profile tokens
	ShareDirectoryInfoList getRootInfos() const noexcept;
	ShareDirectoryInfoPtr getRootInfo(const string& aPath) const noexcept;

	ShareProfileList getProfiles() const noexcept;
	ShareProfileInfo::List getProfileInfos() const noexcept;

	// Get a list of excluded real paths
	StringSet getExcludedPaths() const noexcept;

	void setExcludedPaths(const StringSet& aPaths) noexcept;

	// Get a profile token by its display name
	OptionalProfileToken getProfileByName(const string& aName) const noexcept;


	mutable SharedMutex cs;

	struct ShareLoader;

	// Called when the monitoring mode has been changed
	void rebuildMonitoring() noexcept;

	// Handle monitoring changes (being called regularly from TimerManager so manual calls aren't mandatory)
	void handleChangedFiles() noexcept;

	void setDefaultProfile(ProfileToken aNewDefault) noexcept;

	enum class RefreshState : uint8_t {
		STATE_NORMAL,
		STATE_PENDING,
		STATE_RUNNING,
	};
private:
	void countStats(uint64_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles, size_t& lowerCaseFiles, size_t& totalStrLen_, size_t& roots_) const noexcept;

	DirectoryMonitor monitor;

	uint64_t totalSearches = 0;
	uint64_t tthSearches = 0;
	uint64_t recursiveSearches = 0;
	uint64_t recursiveSearchTime = 0;
	uint64_t filteredSearches = 0;
	uint64_t recursiveSearchesResponded = 0;
	uint64_t searchTokenCount = 0;
	uint64_t searchTokenLength = 0;
	uint64_t autoSearches = 0;
	typedef BloomFilter<5> ShareBloom;

	class ProfileDirectory : public intrusive_ptr_base<ProfileDirectory>, boost::noncopyable {
		public:
			typedef boost::intrusive_ptr<ProfileDirectory> Ptr;
			typedef unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> Map;

			static Ptr create(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, Map& profileDirectories_) noexcept;

			GETSET(string, path, Path);

			GETSET(ProfileTokenSet, rootProfiles, RootProfiles);
			IGETSET(bool, cacheDirty, CacheDirty, false);
			IGETSET(bool, incoming, Incoming, false);
			IGETSET(RefreshState, refreshState, RefreshState, RefreshState::STATE_NORMAL);
			IGETSET(time_t, lastRefreshTime, LastRefreshTime, 0);

			~ProfileDirectory() { }

			bool hasRootProfile(ProfileToken aProfile) const noexcept;
			bool hasRootProfile(const ProfileTokenSet& aProfiles) const noexcept;
			void addRootProfile(ProfileToken aProfile) noexcept;
			bool removeRootProfile(ProfileToken aProfile) noexcept;
			inline string getName() const noexcept{
				return virtualName->getNormal();
			}
			inline const string& getNameLower() const noexcept{
				return virtualName->getLower();
			}

			bool useMonitoring() const noexcept;

			void setName(const string& aName) noexcept;
			string getCacheXmlPath() const noexcept;
		private:
			ProfileDirectory(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming) noexcept;

			unique_ptr<DualString> virtualName;
	};

	typedef vector<ProfileDirectory::Ptr> ProfileDirectoryList;
	unique_ptr<ShareBloom> bloom;

	struct FileListDir;
	class Directory : public intrusive_ptr_base<Directory> {
	public:
		typedef boost::intrusive_ptr<Directory> Ptr;
		typedef unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> Map;
		typedef unordered_multimap<string*, Directory::Ptr, StringPtrHash, StringPtrEq> MultiMap;
		typedef Map::iterator MapIter;
		typedef std::vector<Directory::Ptr> List;

		struct NameLower {
			const string& operator()(const Ptr& a) const { return a->realName.getLower(); }
		};

		class File {
		public:
			struct NameLower {
				const string& operator()(const File* a) const { return a->name.getLower(); }
			};

			//typedef set<File, FileLess> Set;
			typedef SortedVector<File*, std::vector, string, Compare, NameLower> Set;

			File(DualString&& aName, const Directory::Ptr& aParent, const HashedFile& aFileInfo);
			~File();
		
			inline string getADCPath() const noexcept{ return parent->getADCPath() + name.getNormal(); }
			inline string getFullName() const noexcept{ return parent->getFullName() + name.getNormal(); }
			inline string getRealPath() const noexcept { return parent->getRealPath(name.getNormal()); }
			inline bool hasProfile(OptionalProfileToken aProfile) const noexcept { return parent->hasProfile(aProfile); }

			void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const;
			void addSR(SearchResultList& aResults, bool addParent) const noexcept;

			GETSET(int64_t, size, Size);
			GETSET(Directory*, parent, Parent);
			GETSET(uint64_t, lastWrite, LastWrite);
			GETSET(TTHValue, tth, TTH);

			DualString name;
		};

		class SearchResultInfo {
		public:
			struct Sort {
				bool operator()(const SearchResultInfo& left, const SearchResultInfo& right) const { return left.scores > right.scores; }
			};

			explicit SearchResultInfo(const File* f, const SearchQuery& aSearch, int aLevel) :
				file(f), type(FILE), scores(SearchQuery::getRelevanceScore(aSearch, aLevel, false, f->name.getLower())) {

			}

			explicit SearchResultInfo(const Directory* d, const SearchQuery& aSearch, int aLevel) :
				directory(d), type(DIRECTORY), scores(SearchQuery::getRelevanceScore(aSearch, aLevel, true, d->realName.getLower())) {

			}

			typedef multiset<SearchResultInfo, Sort> Set;
			enum Type: uint8_t {
				FILE,
				DIRECTORY
			};

			union {
				const Directory* directory;
				const Directory::File* file;
			};

			Type getType() const { return type; }
		private:
			Type type;
			double scores;
		};

		typedef SortedVector<Ptr, std::vector, string, Compare, NameLower> Set;
		Set directories;
		File::Set files;

		static Ptr createNormal(DualString&& aRealName, const Ptr& aParent, uint64_t aLastWrite, Directory::MultiMap& dirNameMap_, ShareBloom& bloom) noexcept;
		static Ptr createRoot(DualString&& aRealName, uint64_t aLastWrite, const ProfileDirectory::Ptr& aProfileDir, Map& rootPaths_, Directory::MultiMap& dirNameMap_, ShareBloom& bloom) noexcept;

		struct HasRootProfile {
			HasRootProfile(const OptionalProfileToken& aT) : t(aT) { }
			bool operator()(const Ptr& d) const noexcept {
				return d->hasProfile(t);
			}
			const OptionalProfileToken& t;

			HasRootProfile& operator=(const HasRootProfile&) = delete;
		};

		struct IsParent {
			bool operator()(const Ptr& d) const {
				return !d->getParent();
			}
		};

		string getADCPath() const noexcept;
		string getVirtualName() const noexcept;
		const string& getVirtualNameLower() const noexcept;
		string getFullName() const noexcept; 

		inline string getRealPath() const noexcept{ return getRealPath(Util::emptyString); };

		bool hasProfile(ProfileTokenSet& aProfiles) const noexcept;
		bool hasProfile(OptionalProfileToken aProfile) const noexcept;

		void getContentInfo(int64_t& size_, size_t& files_, size_t& folders_) const noexcept;
		int64_t getSize() const noexcept;
		int64_t getTotalSize() const noexcept;
		void getProfileInfo(ProfileToken aProfile, int64_t& totalSize, size_t& filesCount) const noexcept;

		void search(SearchResultInfo::Set& aResults, SearchQuery& aStrings, int aLevel) const noexcept;

		void toFileList(FileListDir* aListDir, bool isFullList);
		void toTTHList(OutputStream& tthList, string& tmp2, bool recursive) const;

		//for file list caching
		void toXmlList(OutputStream& xmlFile, string&& path, string& indent, string& tmp);
		void filesToXmlList(OutputStream& xmlFile, string& indent, string& tmp2) const;

		GETSET(uint64_t, lastWrite, LastWrite);
		GETSET(Directory*, parent, Parent);
		GETSET(ProfileDirectory::Ptr, profileDir, ProfileDir);

		~Directory();

		void copyRootProfiles(ProfileTokenSet& aProfiles, bool setCacheDirty) const noexcept;
		bool isRoot() const noexcept;
		int64_t size;

		//void addBloom(ShareBloom& aBloom) const noexcept;

		void countStats(uint64_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles, size_t& lowerCaseFiles, size_t& totalStrLen_) const noexcept;
		DualString realName;

		// check for an updated modify date from filesystem
		void updateModifyDate();
		void getRenameInfoList(const string& aPath, RenameList& aRename) noexcept;
		Directory::Ptr findDirByPath(const string& aPath, char separator) const noexcept;

		Directory(Directory&) = delete;
		Directory& operator=(Directory&) = delete;
	private:
		Directory(DualString&& aRealName, const Ptr& aParent, uint64_t aLastWrite, ProfileDirectory::Ptr root = nullptr);
		friend void intrusive_ptr_release(intrusive_ptr_base<Directory>*);

		string getRealPath(const string& path) const noexcept;
	};

	struct FileListDir {
		typedef unordered_map<string*, FileListDir*, noCaseStringHash, noCaseStringEq> ListDirectoryMap;
		Directory::List shareDirs;

		FileListDir(const string& aName, int64_t aSize, uint64_t aDate);
		~FileListDir();

		string name;
		int64_t size;
		uint64_t date;
		ListDirectoryMap listDirs;

		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) const;
		void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const;
	};

	ShareDirectoryInfoPtr getRootInfo(const Directory::Ptr& aDir) const noexcept;

	void addAsyncTask(AsyncF aF) noexcept;

	// Returns the dupe directories by directory name/NMDC path
	void getDirsByName(const string& aPath, Directory::List& dirs_) const noexcept;

	friend class Singleton<ShareManager>;

	typedef unordered_multimap<TTHValue*, const Directory::File*> HashFileMap;
	HashFileMap tthIndex;
	
	ShareManager();
	~ShareManager();

	struct TaskData {
		virtual ~TaskData() { }
	};

	struct RefreshTask : public TaskData {
		RefreshTask(int refreshOptions_) : refreshOptions(refreshOptions_) { }
		int refreshOptions;
	};

	bool addDirResult(const Directory* aDir, SearchResultList& aResults, OptionalProfileToken aProfile, SearchQuery& srch) const noexcept;

	ProfileDirectory::Map profileDirs;

	TaskQueue tasks;

	FileList* generateXmlList(ProfileToken aProfile, bool forced = false) throw(ShareException);
	FileList* getFileList(ProfileToken aProfile) const throw(ShareException);

	bool loadCache(function<void(float)> progressF) noexcept;

	volatile bool aShutdown = false;
	
	static atomic_flag refreshing;
	bool refreshRunning = false;

	uint64_t lastFullUpdate = GET_TICK();
	uint64_t lastIncomingUpdate = GET_TICK();
	uint64_t lastSave = 0;
	
	bool xml_saving = false;

	mutable SharedMutex dirNames; // Bundledirs, releasedirs and excluded dirs

	StringSet excludedPaths;

	// Map real name to virtual name - multiple real names may be mapped to a single virtual one
	Directory::Map rootPaths;

	// All directory names cached for easy lookups
	Directory::MultiMap dirNameMap;

	class RefreshInfo : boost::noncopyable {
	public:
		RefreshInfo(const string& aPath, const Directory::Ptr& aOldRoot, uint64_t aLastWrite, ShareBloom& bloom_);
		~RefreshInfo();

		Directory::Ptr oldShareDirectory;
		Directory::Ptr newShareDirectory;
		int64_t hashSize = 0;
		int64_t addedSize = 0;
		Directory::Map rootPathsNew;
		Directory::MultiMap dirNameMapNew;
		HashFileMap tthIndexNew;

		string path;

		void mergeRefreshChanges(Directory::MultiMap& aDirNameMap, Directory::Map& aRootPaths, HashFileMap& aTTHIndex, int64_t& totalHash, int64_t& totalAdded, ProfileTokenSet* dirtyProfiles) noexcept;
	};

	typedef shared_ptr<RefreshInfo> RefreshInfoPtr;
	typedef vector<RefreshInfoPtr> RefreshInfoList;
	typedef set<RefreshInfoPtr, std::less<RefreshInfoPtr>> RefreshInfoSet;

	bool handleRefreshedDirectory(const RefreshInfo& ri);

	// Display a log message if the refresh can't be started immediately
	void reportPendingRefresh(TaskType aTask, const RefreshPathList& aDirectories, const string& displayName) const noexcept;

	// Add directories for refresh
	RefreshResult addRefreshTask(TaskType aTaskType, const StringList& aDirs, RefreshType aRefreshType, const string& displayName = Util::emptyString, function<void(float)> progressF = nullptr) noexcept;

	// Remove directories that have already been queued for refresh
	void validateRefreshTask(StringList& dirs_) noexcept;

	// Change the refresh status for a directory and its subroots
	// Safe to call with non-root directories
	void setRefreshState(const string& aPath, RefreshState aState, bool aUpdateRefreshTime) noexcept;

	// Recursive function for building a new share tree from a path
	void buildTree(const string& aPath, const string& aPathLower, const Directory::Ptr& aDir, Directory::MultiMap& directoryNameMapNew_, int64_t& hashSize_, int64_t& addedSize_, HashFileMap& tthIndexNew_, ShareBloom& bloomNew_);

	void addFile(const string& aName, Directory::Ptr& aDir, const HashedFile& fi, ProfileTokenSet& dirtyProfiles_) noexcept;

	static void updateIndices(Directory::Ptr& aDirectory, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex, Directory::MultiMap& aDirNames) noexcept;
	static void updateIndices(Directory& dir, const Directory::File* f, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex) noexcept;

	void cleanIndices(Directory& dir) noexcept;
	void cleanIndices(Directory& dir, const Directory::File* f) noexcept;

	/*inline void addDirName(Directory::Ptr& aDir) noexcept {
		addDirName(aDir, dirNameMap);
	}

	inline void removeDirName(Directory& aDir) noexcept {
		removeDirName(aDir, dirNameMap);
	}*/

	static void addDirName(const Directory::Ptr& dir, Directory::MultiMap& aDirNames, ShareBloom& aBloom) noexcept;
	static void removeDirName(const Directory& dir, Directory::MultiMap& aDirNames) noexcept;

	void onFileHashed(const string& fname, HashedFile& fileInfo) noexcept;
	
	StringList bundleDirs;

	// Get root directories matching the provided token
	// Unsafe
	void getRootsByVirtual(const string& aVirtualName, OptionalProfileToken aProfile, Directory::List& dirs_) const noexcept;

	// Get root directories matching any of the provided tokens
	// Unsafe
	void getRootsByVirtual(const string& aVirtualName, const ProfileTokenSet& aProfiles, Directory::List& dirs_) const noexcept;

	// Get directories matching the virtual path
	// Can be used with a single profile token or a set of them
	template<class T>
	void findVirtuals(const string& virtualPath, const T& aProfile, Directory::List& dirs) const throw(ShareException) {

		Directory::List virtuals; //since we are mapping by realpath, we can have more than 1 same virtualnames
		if(virtualPath.empty() || virtualPath[0] != '/') {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		string::size_type start = virtualPath.find('/', 1);
		if(start == string::npos || start == 1) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		getRootsByVirtual( virtualPath.substr(1, start-1), aProfile, virtuals);
		if(virtuals.empty()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		Directory::Ptr d;
		for(auto k = virtuals.begin(); k != virtuals.end(); k++) {
			string::size_type i = start; // always start from the begin.
			string::size_type j = i + 1;
			d = *k;

			while((i = virtualPath.find('/', j)) != string::npos) {
				auto mi = d->directories.find(Text::toLower(virtualPath.substr(j, i - j)));
				j = i + 1;
				if (mi != d->directories.end()) {   //if we found something, look for more.
					d = *mi;
				} else {
					d = nullptr;   //make the pointer null so we can check if found something or not.
					break;
				}
			}

			if(d) 
				dirs.push_back(d);
		}

		if(dirs.empty()) {
			//if we are here it means we didnt find anything, throw.
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}
	}

	// Find an existing directory by real path
	Directory::Ptr findDirectory(const string& aRealPath) const noexcept;

	// Attempt to add the path in share
	Directory::Ptr getDirectory(const string& aRealPath, bool report, bool aCheckExcluded = true) noexcept;

	// Attempts to find directory from share and returns the last existing directory
	// If the exact directory can't be found, the missing directory names are added in remainingTokens_
	Directory::Ptr findDirectory(const string& aRealPath, StringList& remainingTokens_) const noexcept;

	virtual int run();

	void runTasks(function<void (float)> progressF = nullptr) noexcept;

	// QueueManagerListener
	virtual void on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept;
	virtual void on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept;
	virtual void on(QueueManagerListener::FileHashed, const string& aPath, HashedFile& aFileInfo) noexcept { onFileHashed(aPath, aFileInfo); }

	// SettingsManagerListener
	void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
		save(xml);
	}
	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
		load(xml);
	}
	
	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t tick) noexcept;
	void on(TimerManagerListener::Minute, uint64_t tick) noexcept;


	//DirectoryMonitorListener
	virtual void on(DirectoryMonitorListener::FileCreated, const string& aPath) noexcept;
	virtual void on(DirectoryMonitorListener::FileModified, const string& aPath) noexcept;
	virtual void on(DirectoryMonitorListener::FileRenamed, const string& aOldPath, const string& aNewPath) noexcept;
	virtual void on(DirectoryMonitorListener::FileDeleted, const string& aPath) noexcept;
	virtual void on(DirectoryMonitorListener::Overflow, const string& aPath) noexcept;
	virtual void on(DirectoryMonitorListener::DirectoryFailed, const string& aPath, const string& aError) noexcept;

	void load(SimpleXML& aXml);
	void loadProfile(SimpleXML& aXml, const string& aName, ProfileToken aToken);
	void save(SimpleXML& aXml);

	void reportTaskStatus(uint8_t aTask, const RefreshPathList& aDirectories, bool finished, int64_t aHashSize, const string& displayName, RefreshType aRefreshType) const noexcept;
	
	ShareProfileList shareProfiles;

	StringMatch skipList;
	string winDir;

	void addMonitoring(const StringList& aPaths) noexcept;
	void removeMonitoring(const StringList& aPaths) noexcept;

	class DirModifyInfo {
	public:
		typedef deque<DirModifyInfo> List;
		enum ActionType {
			ACTION_NONE,
			ACTION_CREATED,
			ACTION_MODIFIED,
			ACTION_DELETED
		};

		struct FileInfo {
			FileInfo(ActionType aAction, const string& aOldPath) : action(aAction), oldPath(aOldPath) { }

			ActionType action;
			string oldPath;
		};

		//DirModifyInfo(ActionType aAction) : lastFileActivity(GET_TICK()), lastReportedError(0), dirAction(aAction) { }
		DirModifyInfo(const string& aFile, bool isDirectory, ActionType aAction, const string& aOldPath = Util::emptyString);

		void addFile(const string& aFile, ActionType aAction, const string& aOldPath = Util::emptyString) noexcept;

		typedef unordered_map<string, FileInfo> FileInfoMap;
		FileInfoMap files;
		time_t lastFileActivity = GET_TICK();
		time_t lastReportedError = 0;

		ActionType dirAction = ACTION_NONE;
		string volume;
		string path;
		string oldPath;

		void setPath(const string& aPath) noexcept;
	};

	typedef set<string, Util::PathSortOrderBool> PathSet;

	DirModifyInfo::List fileModifications;

	// Validates that the new/modified path can be shared and returns the full path (path separator is added for directories
	optional<pair<string, bool>> checkModifiedPath(const string& aPath) const noexcept;

	void addModifyInfo(const string& aPath, bool isDirectory, DirModifyInfo::ActionType) noexcept;
	bool handleDeletedFile(const string& aPath, bool isDirectory, ProfileTokenSet& dirtyProfiles_) noexcept;

	// Recursively removes all notifications for the given path
	void removeNotifications(const string& aPath) noexcept;
	void removeNotifications(DirModifyInfo::List::iterator aInfo, const string& aPath) noexcept;

	DirModifyInfo::List::iterator findModifyInfo(const string& aFile) noexcept;
	void handleChangedFiles(uint64_t aTick, bool forced = false) noexcept;
	bool handleModifyInfo(DirModifyInfo& aInfo, optional<StringList>& bundlePaths_, ProfileTokenSet& dirtyProfiles_, StringList& refresh_, uint64_t aTick, bool forced) noexcept;
	void onFileDeleted(const string& aPath);

	void restoreFailedMonitoredPaths();
}; //sharemanager end

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
