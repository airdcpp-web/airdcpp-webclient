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

#ifndef DCPLUSPLUS_DCPP_SHARE_MANAGER_H
#define DCPLUSPLUS_DCPP_SHARE_MANAGER_H


#include "HashManagerListener.h"
#include "SettingsManagerListener.h"
#include "ShareManagerListener.h"
#include "TimerManagerListener.h"

#include "BloomFilter.h"
#include "CriticalSection.h"
#include "DualString.h"
#include "DupeType.h"
#include "Exception.h"
#include "HashBloom.h"
#include "HashedFile.h"
#include "Message.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "SearchQuery.h"
#include "ShareDirectoryInfo.h"
#include "ShareProfile.h"
#include "Singleton.h"
#include "SortedVector.h"
#include "StringSearch.h"
#include "TaskQueue.h"
#include "Thread.h"
#include "TimerManager.h"
#include "UserConnection.h"

namespace dcpp {

class File;
class ErrorCollector;
class OutputStream;
class MemoryInputStream;
class SearchQuery;
class SharePathValidator;

class FileList;

typedef uint32_t TempShareToken;
struct TempShareInfo {
	TempShareInfo(const string& aName, const string& aPath, int64_t aSize, const TTHValue& aTTH, const UserPtr& aUser) noexcept;

	const TempShareToken id;
	const string name;
	const UserPtr user; //CID or hubUrl
	const string path; //filepath
	const int64_t size; //filesize
	const TTHValue tth;
	const time_t timeAdded;

	bool hasAccess(const UserPtr& aUser) const noexcept;
};

struct ShareRefreshStats {
	int64_t hashSize = 0;
	int64_t addedSize = 0;

	size_t existingDirectoryCount = 0;
	size_t existingFileCount = 0;
	size_t newDirectoryCount = 0;
	size_t newFileCount = 0;
	size_t skippedDirectoryCount = 0;
	size_t skippedFileCount = 0;

	void merge(const ShareRefreshStats& aOther) noexcept;
};

enum class ShareRefreshType : uint8_t {
	ADD_DIR,
	REFRESH_DIRS,
	REFRESH_INCOMING,
	REFRESH_ALL,
	STARTUP,
	BUNDLE
};

enum class ShareRefreshPriority : uint8_t {
	MANUAL,
	SCHEDULED,
	NORMAL,
	BLOCKING,
};

struct ShareRefreshTask : public Task {
	ShareRefreshTask(ShareRefreshTaskToken aToken, const RefreshPathList& aDirs, const string& aDisplayName, ShareRefreshType aRefreshType, ShareRefreshPriority aPriority);

	const ShareRefreshTaskToken token;
	const RefreshPathList dirs;
	const string displayName;
	const ShareRefreshType type;
	const ShareRefreshPriority priority;

	bool canceled = false;
	bool running = false;
};

typedef vector<ShareRefreshTask> ShareRefreshTaskList;

class ShareManager : public Singleton<ShareManager>, public Speaker<ShareManagerListener>, private Thread, private SettingsManagerListener, 
	private TimerManagerListener, private HashManagerListener
{
public:
	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	const unique_ptr<SharePathValidator> validator;

	SharePathValidator& getValidator() noexcept {
		return *validator.get();
	}

	// Validate that the new root can be added in share (sub/parent/existing directory matching)
	// Throws ShareException
	void validateRootPath(const string& aRealPath, bool aMatchCurrentRoots = true) const;

	// Returns virtual path of a TTH
	// Throws ShareException
	string toVirtual(const TTHValue& aTTH, ProfileToken aProfile) const;

	// Returns size and file name of a filelist
	// virtualFile = name requested by the other user (Transfer::USER_LIST_NAME_BZ or Transfer::USER_LIST_NAME)
	// Throws ShareException
	pair<int64_t, string> getFileListInfo(const string& virtualFile, ProfileToken aProfile);

	// Get real path and size for a virtual path
	// noAccess_ will be set to true if the file is availabe but not in the supplied profiles
	// Throws ShareException
	void toRealWithSize(const string& aVirtualPath, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_);

	// Returns TTH value for a file list (not very useful but the ADC specs...)
	// virtualFile = name requested by the other user (Transfer::USER_LIST_NAME_BZ or Transfer::USER_LIST_NAME)
	// Throws ShareException
	TTHValue getListTTH(const string& aVirtualPath, ProfileToken aProfile) const;

	enum TaskType: uint8_t {
		ASYNC,
		REFRESH,
	};

	enum class RefreshTaskQueueResult : uint8_t {
		STARTED,
		QUEUED,
		EXISTS,
	};

	struct RefreshTaskQueueInfo {
		optional<ShareRefreshTaskToken> token;
		RefreshTaskQueueResult result;
	};

	// Refresh the whole share or in
	RefreshTaskQueueInfo refresh(ShareRefreshType aType, ShareRefreshPriority aPriority, function<void(float)> progressF = nullptr) noexcept;

	// Refresh a single single path or all paths under a virtual name (roots only)
	// Returns nullopt if the path doesn't exist in share
	optional<RefreshTaskQueueInfo> refreshVirtualName(const string& aVirtualName, ShareRefreshPriority aPriority) noexcept;

	// Refresh the specific directories
	// Returns nullopt if the path doesn't exist in share (and it can't be added there)
	optional<RefreshTaskQueueInfo> refreshPathsHooked(ShareRefreshPriority aPriority, const StringList& aPaths, const void* aCaller, const string& aDisplayName = Util::emptyString, function<void(float)> aProgressF = nullptr) noexcept;

	// Refresh the specific directories
	// Throws if the path doesn't exist in share and can't be added there
	RefreshTaskQueueInfo refreshPathsHookedThrow(ShareRefreshPriority aPriority, const StringList& aPaths, const void* aCaller, const string& aDisplayName = Util::emptyString, function<void(float)> aProgressF = nullptr);

	bool isRefreshing() const noexcept { return refreshRunning; }
	
	// aIsMajor will regenerate the file list on next time when someone requests it
	void setProfilesDirty(const ProfileTokenSet& aProfiles, bool aIsMajor) noexcept;

	void startup(StartupLoader& aLoader) noexcept;
	void shutdown(function<void(float)> progressF) noexcept;

	// Abort filelist refresh (or an individual refresh task)
	bool abortRefresh(optional<ShareRefreshTaskToken> aToken = nullopt) noexcept;

	ShareRefreshTaskList getRefreshTasks() const noexcept;

	void nmdcSearch(SearchResultList& l, const string& aString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type aMaxResults, bool aHideShare) noexcept;

	// Throws ShareException in case an invalid path is provided
	void adcSearch(SearchResultList& l, SearchQuery& aSearch, const OptionalProfileToken& aProfile, const CID& cid, const string& aDir, bool aIsAutoSearch = false);

	// Check if a directory is shared
	// You may also give a path in NMDC format and the relevant 
	// directory (+ possible subdirectories) are detected automatically
	bool isAdcDirectoryShared(const string& aAdcPath) const noexcept;

	// Mostly for dupe check with size comparison (partial/exact dupe)
	DupeType isAdcDirectoryShared(const string& aAdcPath, int64_t aSize) const noexcept;

	bool isFileShared(const TTHValue& aTTH) const noexcept;
	bool isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept;
	bool isRealPathShared(const string& aPath) const noexcept;

	// Returns true if the real path can be added in share
	bool allowShareDirectoryHooked(const string& aPath, const void* aCaller) const noexcept;

	// Validate a file/directory path
	// Throws on errors
	void validatePathHooked(const string& aPath, bool aSkipQueueCheck, const void* aCaller) const;

	// Returns the dupe paths by directory name/NMDC path
	StringList getAdcDirectoryPaths(const string& aAdcPath) const noexcept;

	GroupedDirectoryMap getGroupedDirectories() const noexcept;
	MemoryInputStream* generatePartialList(const string& aVirtualPath, bool aRecursive, const OptionalProfileToken& aProfile) const noexcept;
	MemoryInputStream* generateTTHList(const string& aVirtualPath, bool aRecursive, ProfileToken aProfile) const noexcept;
	MemoryInputStream* getTree(const string& virtualFile, ProfileToken aProfile) const noexcept;
	void toFilelist(OutputStream& os_, const string& aVirtualPath, const OptionalProfileToken& aProfile, bool aRecursive) const;

	void saveXmlList(function<void (float)> progressF = nullptr) noexcept;	//for filelist caching

	// Throws ShareException
	AdcCommand getFileInfo(const string& aFile, ProfileToken aProfile);

	int64_t getTotalShareSize(ProfileToken aProfile) const noexcept;

	// Get share size and number of files for a specified profile
	void getProfileInfo(ProfileToken aProfile, int64_t& totalSize_, size_t& fileCount_) const noexcept;
	
	// Adds all shared TTHs (permanent and temp) to the filter
	void getBloom(HashBloom& bloom) const noexcept;

	// Removes path characters from virtual name
	string validateVirtualName(const string& aName) const noexcept;

	// Generate own full filelist on disk
	// Throws ShareException
	string generateOwnList(ProfileToken aProfile);

	bool isTTHShared(const TTHValue& tth) const noexcept;

	// Get real paths for an ADC virtual path
	// Throws ShareException
	void getRealPaths(const string& aVirtualPath, StringList& realPaths_, const OptionalProfileToken& aProfile = nullopt) const;

	StringList getRealPaths(const TTHValue& root) const noexcept;

	IGETSET(size_t, hits, Hits, 0);
	IGETSET(int64_t, sharedSize, SharedSize, 0);

	typedef unordered_multimap<TTHValue, TempShareInfo> TempShareMap;

	optional<TempShareInfo> addTempShare(const TTHValue& aTTH, const string& aName, const string& aFilePath, int64_t aSize, ProfileToken aProfile, const UserPtr& aUser) noexcept;

	bool hasTempShares() const noexcept;
	TempShareInfoList getTempShares() const noexcept;
	TempShareInfoList getTempShares(const TTHValue& aTTH) const noexcept;

	bool removeTempShare(const UserPtr& aUser, const TTHValue& aTTH) noexcept;
	bool removeTempShare(TempShareToken aId) noexcept;
	bool isTempShared(const UserPtr& aUser, const TTHValue& aTTH) const noexcept;
	//tempShares end

	// Get real paths of all shared root directories
	void getRootPaths(StringList& aDirs) const noexcept;

	// Get a printable version of various share-related statistics
	string printStats() const noexcept;

	struct ShareItemStats {
		int profileCount = 0;
		size_t rootDirectoryCount = 0;

		int64_t totalSize = 0;
		size_t totalFileCount = 0;
		size_t totalDirectoryCount = 0;
		size_t uniqueFileCount = 0;
		size_t lowerCaseFiles = 0;
		double averageNameLength = 0;
		size_t totalNameSize = 0;
		time_t averageFileAge = 0;
	};
	optional<ShareItemStats> getShareItemStats() const noexcept;

	struct ShareSearchStats {
		uint64_t totalSearches = 0;
		double totalSearchesPerSecond = 0;
		uint64_t recursiveSearches = 0, filteredSearches = 0;
		uint64_t averageSearchMatchMs = 0;
		uint64_t recursiveSearchesResponded = 0;

		double unfilteredRecursiveSearchesPerSecond = 0;

		double averageSearchTokenCount = 0;
		double averageSearchTokenLength = 0;

		uint64_t autoSearches = 0, tthSearches = 0;
	};
	ShareSearchStats getSearchMatchingStats() const noexcept;

	void addRootDirectories(const ShareDirectoryInfoList& aNewDirs) noexcept;
	void updateRootDirectories(const ShareDirectoryInfoList& renameDirs) noexcept;
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
	string realToVirtualAdc(const string& aPath, const OptionalProfileToken& aToken = nullopt) const noexcept;

	// If allowFallback is true, the default profile will be returned if the requested one is not found
	ShareProfilePtr getShareProfile(ProfileToken aProfile, bool allowFallback = false) const noexcept;

	// Get information of all shared directories grouped by profile tokens
	ShareDirectoryInfoList getRootInfos() const noexcept;
	ShareDirectoryInfoPtr getRootInfo(const string& aPath) const noexcept;

	ShareProfileList getProfiles() const noexcept;
	ShareProfileInfo::List getProfileInfos() const noexcept;

	// Get a profile token by its display name
	OptionalProfileToken getProfileByName(const string& aName) const noexcept;


	mutable SharedMutex cs;

	struct ShareLoader;

	void setDefaultProfile(ProfileToken aNewDefault) noexcept;

	enum class RefreshState : uint8_t {
		STATE_NORMAL,
		STATE_PENDING,
		STATE_RUNNING,
	};

	void shareBundle(const BundlePtr& aBundle) noexcept;
	void onFileHashed(const string& aRealPath, const HashedFile& aFileInfo) noexcept;

	StringSet getExcludedPaths() const noexcept;
	void addExcludedPath(const string& aPath);
	bool removeExcludedPath(const string& aPath) noexcept;

	void reloadSkiplist();
	void setExcludedPaths(const StringSet& aPaths) noexcept;
private:
	TempShareMap tempShares;

	void countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles, size_t& lowerCaseFiles, size_t& totalStrLen_, size_t& roots_) const noexcept;

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

	class RootDirectory : boost::noncopyable {
		public:
			typedef shared_ptr<RootDirectory> Ptr;
			typedef unordered_map<TTHValue, Ptr> Map;

			static Ptr create(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastRefreshTime) noexcept;

			GETSET(ProfileTokenSet, rootProfiles, RootProfiles);
			IGETSET(bool, cacheDirty, CacheDirty, false);
			IGETSET(bool, incoming, Incoming, false);
			IGETSET(RefreshState, refreshState, RefreshState, RefreshState::STATE_NORMAL);
			IGETSET(optional<ShareRefreshTaskToken>, refreshTaskToken, RefreshTaskToken, nullopt);
			IGETSET(time_t, lastRefreshTime, LastRefreshTime, 0);

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

			inline const string& getPath() const noexcept {
				return path;
			}

			inline const string& getPathLower() const noexcept {
				return pathLower;
			}

			void setName(const string& aName) noexcept;
			string getCacheXmlPath() const noexcept;
		private:
			RootDirectory(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastRefreshTime) noexcept;

			unique_ptr<DualString> virtualName;
			const string path;
			const string pathLower;
	};

	typedef vector<RootDirectory::Ptr> RootDirectoryList;
	unique_ptr<ShareBloom> bloom;

	struct FilelistDirectory;
	class Directory : public intrusive_ptr_base<Directory> {
	public:
		typedef boost::intrusive_ptr<Directory> Ptr;
		typedef unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> Map;
		typedef unordered_multimap<string*, Directory::Ptr, StringPtrHash, StringPtrEq> MultiMap;
		typedef Map::iterator MapIter;
		typedef std::vector<Directory::Ptr> List;

		struct NameLower {
			const string& operator()(const Ptr& a) const noexcept { return a->realName.getLower(); }
		};

		class File {
		public:
			struct NameLower {
				const string& operator()(const File* a) const noexcept { return a->name.getLower(); }
			};

			//typedef set<File, FileLess> Set;
			typedef SortedVector<File*, std::vector, string, Compare, NameLower> Set;
			typedef unordered_multimap<TTHValue*, const Directory::File*> TTHMap;

			File(DualString&& aName, const Directory::Ptr& aParent, const HashedFile& aFileInfo);
			~File();
		
			inline string getAdcPath() const noexcept{ return parent->getAdcPath() + name.getNormal(); }
			inline string getRealPath() const noexcept { return parent->getRealPath(name.getNormal()); }
			inline bool hasProfile(const OptionalProfileToken& aProfile) const noexcept { return parent->hasProfile(aProfile); }

			void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const;
			void addSR(SearchResultList& aResults, bool addParent) const noexcept;

			GETSET(int64_t, size, Size);
			GETSET(Directory*, parent, Parent);
			GETSET(time_t, lastWrite, LastWrite);
			GETSET(TTHValue, tth, TTH);

			DualString name;

			void updateIndices(ShareBloom& aBloom_, int64_t& sharedSize_, File::TTHMap& tthIndex_) noexcept;
			void cleanIndices(int64_t& sharedSize_, TTHMap& tthIndex_) noexcept;
		};

		class SearchResultInfo {
		public:
			struct Sort {
				bool operator()(const SearchResultInfo& left, const SearchResultInfo& right) const noexcept { return left.scores > right.scores; }
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

			Type getType() const noexcept { return type; }
		private:
			const Type type;
			double scores;
		};

		typedef SortedVector<Ptr, std::vector, string, Compare, NameLower> Set;
		File::Set files;

		static Ptr createNormal(DualString&& aRealName, const Ptr& aParent, time_t aLastWrite, Directory::MultiMap& dirNameMap_, ShareBloom& bloom) noexcept;
		static Ptr createRoot(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastWrite, Map& rootPaths_, Directory::MultiMap& dirNameMap_, ShareBloom& bloom_, time_t aLastRefreshTime) noexcept;

		// Set a new parent for the directory
		// Possible directories with the same name must be removed from the parent first
		static bool setParent(const Directory::Ptr& aDirectory, const Directory::Ptr& aParent) noexcept;

		// Remove directory from possible parent and all shared containers
		static void cleanIndices(Directory& aDirectory, int64_t& sharedSize_, File::TTHMap& tthIndex_, Directory::MultiMap& aDirNames_) noexcept;

		struct HasRootProfile {
			HasRootProfile(const OptionalProfileToken& aProfile) : profile(aProfile) { }
			bool operator()(const Ptr& d) const noexcept {
				return d->hasProfile(profile);
			}
			OptionalProfileToken profile; // Never use a reference here as this predicate may also be initilized with ProfileToken without warnings

			HasRootProfile& operator=(const HasRootProfile&) = delete;
		};

		string getAdcPath() const noexcept;
		string getVirtualName() const noexcept;
		const string& getVirtualNameLower() const noexcept;

		inline string getRealPath() const noexcept{ return getRealPath(Util::emptyString); };

		bool hasProfile(const ProfileTokenSet& aProfiles) const noexcept;
		bool hasProfile(const OptionalProfileToken& aProfile) const noexcept;

		void getContentInfo(int64_t& size_, size_t& files_, size_t& folders_) const noexcept;

		// Return cached size for files directly inside this directory
		int64_t getLevelSize() const noexcept;

		// Count the recursive total size for the directory
		int64_t getTotalSize() const noexcept;

		void getProfileInfo(ProfileToken aProfile, int64_t& totalSize_, size_t& filesCount_) const noexcept;

		void search(SearchResultInfo::Set& aResults, SearchQuery& aStrings, int aLevel) const noexcept;

		void toFileList(FilelistDirectory& aListDir, bool aRecursive);
		void toTTHList(OutputStream& tthList, string& tmp2, bool aRecursive) const;

		//for file list caching
		void toXmlList(OutputStream& xmlFile, string& indent, string& tmp);
		void filesToXmlList(OutputStream& xmlFile, string& indent, string& tmp2) const;

		GETSET(time_t, lastWrite, LastWrite);

		~Directory();

		void copyRootProfiles(ProfileTokenSet& profiles_, bool aSetCacheDirty) const noexcept;
		bool isRoot() const noexcept;

		//void addBloom(ShareBloom& aBloom) const noexcept;

		void countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles_, size_t& lowerCaseFiles_, size_t& totalStrLen_) const noexcept;
		DualString realName;

		// check for an updated modify date from filesystem
		void updateModifyDate();

		Directory(Directory&) = delete;
		Directory& operator=(Directory&) = delete;

		const RootDirectory::Ptr& getRoot() const noexcept { return root; }
		void increaseSize(int64_t aSize, int64_t& totalSize_) noexcept;
		void decreaseSize(int64_t aSize, int64_t& totalSize_) noexcept;

		const Set& getDirectories() const noexcept {
			return directories;
		}

		Directory* getParent() const noexcept {
			return parent;
		}

		// Find child directory by path
		// Returning of the initial directory (aPath empty) is not supported
		Directory::Ptr findDirectoryByPath(const string& aPath, char aSeparator) const noexcept;

		Directory::Ptr findDirectoryByName(const string& aName) const noexcept;


		class RootIsParentOrExact {
		public:
			// Returns true for items matching the predicate that are parent directories of compareTo (or exact matches)
			RootIsParentOrExact(const string& aCompareTo) : compareToLower(Text::toLower(aCompareTo)), separator(PATH_SEPARATOR) {}
			bool operator()(const Directory::Ptr& aDirectory) const noexcept;

			RootIsParentOrExact& operator=(const RootIsParentOrExact&) = delete;
		private:
			const string compareToLower;
			const char separator;
		};
	private:
		void cleanIndices(int64_t& sharedSize_, File::TTHMap& tthIndex_, Directory::MultiMap& dirNames_) noexcept;

		Directory* parent;
		Set directories;

		// Size for files directly inside this directory
		int64_t size = 0;
		RootDirectory::Ptr root;

		Directory(DualString&& aRealName, const Ptr& aParent, time_t aLastWrite, const RootDirectory::Ptr& aRoot = nullptr);
		friend void intrusive_ptr_release(intrusive_ptr_base<Directory>*);

		string getRealPath(const string& path) const noexcept;
	};

	struct FilelistDirectory {
		typedef unordered_map<string*, FilelistDirectory*, noCaseStringHash, noCaseStringEq> Map;
		Directory::List shareDirs;

		FilelistDirectory(const string& aName, time_t aDate);
		~FilelistDirectory();

		const string name;
		time_t date;

		Map listDirs;

		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool aFullList) const;
		void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool aAddDate) const;
	};

	ShareDirectoryInfoPtr getRootInfo(const Directory::Ptr& aDir) const noexcept;

	void addAsyncTask(AsyncF aF) noexcept;

	// Returns the dupe directories by directory name/ADC path
	void getDirectoriesByAdcName(const string& aAdcPath, Directory::List& dirs_) const noexcept;

	friend class Singleton<ShareManager>;

	typedef Directory::File::TTHMap HashFileMap;
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

	bool addDirectoryResult(const Directory* aDir, SearchResultList& aResults, const OptionalProfileToken& aProfile, SearchQuery& srch) const noexcept;

	TaskQueue tasks;

	// Throws ShareException
	FileList* generateXmlList(ProfileToken aProfile, bool aForced = false);

	// Throws ShareException
	FileList* getFileList(ProfileToken aProfile) const;

	bool loadCache(function<void(float)> progressF) noexcept;
	
	static atomic_flag tasksRunning;
	bool refreshRunning = false;

	uint64_t lastFullUpdate = GET_TICK();
	uint64_t lastIncomingUpdate = GET_TICK();
	uint64_t lastSave = 0;
	
	bool xml_saving = false;

	// Map real name to virtual name - multiple real names may be mapped to a single virtual one
	Directory::Map rootPaths;

	// All directory names cached for easy lookups
	Directory::MultiMap lowerDirNameMap;

	class RefreshInfo : boost::noncopyable {
	public:
		RefreshInfo(const string& aPath, const Directory::Ptr& aOldRoot, time_t aLastWrite, ShareBloom& bloom_);
		~RefreshInfo();

		Directory::Ptr oldShareDirectory;
		Directory::Ptr newShareDirectory;

		ShareRefreshStats stats;

		Directory::Map rootPathsNew;
		Directory::MultiMap lowerDirNameMapNew;
		HashFileMap tthIndexNew;

		string path;

		ShareManager::ShareBloom& bloom;

		void applyRefreshChanges(Directory::MultiMap& lowerDirNameMap_, Directory::Map& rootPaths_, HashFileMap& tthIndex_, int64_t& sharedBytes_, ProfileTokenSet* dirtyProfiles) noexcept;
		bool checkContent(const Directory::Ptr& aDirectory) noexcept;
	};

	class ShareBuilder : public RefreshInfo {
	public:
		ShareBuilder(const string& aPath, const Directory::Ptr& aOldRoot, time_t aLastWrite, ShareBloom& bloom_, ShareManager* sm);

		// Recursive function for building a new share tree from a path
		bool buildTree(const bool& aStopping) noexcept;
	private:
		void buildTree(const string& aPath, const string& aPathLower, const Directory::Ptr& aCurrentDirectory, const Directory::Ptr& aOldDirectory, const bool& aStopping);

		bool validateFileItem(const FileItemInfoBase& aFileItem, const string& aPath, bool aIsNew, bool aNewParent, ErrorCollector& aErrorCollector) noexcept;

		const ShareManager& sm;
	};

	typedef shared_ptr<ShareBuilder> ShareBuilderPtr;
	typedef set<ShareBuilderPtr, std::less<ShareBuilderPtr>> ShareBuilderSet;

	bool applyRefreshChanges(RefreshInfo& ri, ProfileTokenSet* aDirtyProfiles);

	// Display a log message if the refresh can't be started immediately
	void reportPendingRefresh(ShareRefreshType aTask, const RefreshPathList& aDirectories, const string& aDisplayName) const noexcept;

	// Add directories for refresh
	RefreshTaskQueueInfo addRefreshTask(ShareRefreshPriority aPriority, const StringList& aDirs, ShareRefreshType aRefreshType, const string& aDisplayName = Util::emptyString, function<void(float)> aProgressF = nullptr) noexcept;

	// Remove directories that have already been queued for refresh
	void validateRefreshTask(StringList& dirs_) noexcept;

	// Change the refresh status for a directory and its subroots
	// Safe to call with non-root directories
	void setRefreshState(const string& aPath, RefreshState aState, bool aUpdateRefreshTime, const optional<ShareRefreshTaskToken>& aRefreshTaskToken) noexcept;

	static void addFile(DualString&& aName, const Directory::Ptr& aDir, const HashedFile& fi, HashFileMap& tthIndex_, ShareBloom& aBloom_, int64_t& sharedSize_, ProfileTokenSet* dirtyProfiles_ = nullptr) noexcept;

	static void addDirName(const Directory::Ptr& aDir, Directory::MultiMap& aDirNames, ShareBloom& aBloom) noexcept;
	static void removeDirName(const Directory& aDir, Directory::MultiMap& aDirNames) noexcept;

#ifdef _DEBUG
	// Checks that duplicate/incorrect directories/files won't get through
	static void checkAddedDirNameDebug(const Directory::Ptr& aDir, Directory::MultiMap& aDirNames) noexcept;
	static void checkAddedTTHDebug(const Directory::File* f, HashFileMap& aTTHIndex) noexcept;

	// Go through the whole tree and check that the global maps have been filled properly
	void validateDirectoryTreeDebug() noexcept;
	void validateDirectoryRecursiveDebug(const Directory::Ptr& dir, OrderedStringSet& directoryPaths_, OrderedStringSet& filePaths_) noexcept;
#endif

	// Get root directories matching the provided token
	// Unsafe
	void getRootsByVirtual(const string& aVirtualName, const OptionalProfileToken& aProfile, Directory::List& dirs_) const noexcept;

	// Get root directories matching any of the provided tokens
	// Unsafe
	void getRootsByVirtual(const string& aVirtualName, const ProfileTokenSet& aProfiles, Directory::List& dirs_) const noexcept;

	// Get root directories by profile
	// Unsafe
	void getRoots(const OptionalProfileToken& aProfile, Directory::List& dirs_) const noexcept;

	// Get directories matching the virtual path (root path is not accepted here)
	// Can be used with a single profile token or a set of them
	// Throws ShareException
	// Unsafe
	template<class T>
	void findVirtuals(const string& aVirtualPath, const T& aProfile, Directory::List& dirs_) const {
		Directory::List virtuals; //since we are mapping by realpath, we can have more than 1 same virtualnames
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

		for (const auto& root: virtuals) {
			string::size_type i = start; // always start from the begin.
			string::size_type j = i + 1;

			auto d = root;
			while ((i = aVirtualPath.find(ADC_SEPARATOR, j)) != string::npos) {
				d = d->findDirectoryByName(Text::toLower(aVirtualPath.substr(j, i - j)));
				if (!d) {
					break;
				}

				j = i + 1;
			}

			if (d) {
				dirs_.push_back(d);
			}
		}

		if(dirs_.empty()) {
			//if we are here it means we didnt find anything, throw.
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}
	}

	// Find an existing directory by real path
	Directory::Ptr findDirectory(const string& aRealPath) const noexcept;

	// Attempt to add the path in share
	Directory::Ptr getDirectory(const string& aRealPath) noexcept;

	// Attempts to find directory from share and returns the last existing directory
	// If the exact directory can't be found, the missing directory names are added in remainingTokens_
	Directory::Ptr findDirectory(const string& aRealPath, StringList& remainingTokens_) const noexcept;

	int run() override;

	void runTasks(function<void (float)> progressF = nullptr) noexcept;
	void runRefreshTask(const ShareRefreshTask& aTask, function<void(float)> progressF) noexcept;

	// HashManagerListener
	void on(HashManagerListener::FileHashed, const string& aPath, HashedFile& fi) noexcept override { onFileHashed(aPath, fi); }

	// SettingsManagerListener
	void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept override {
		save(xml);
	}
	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept override {
		load(xml);
	}

	void on(SettingsManagerListener::LoadCompleted, bool aFileLoaded) noexcept override;
	
	// TimerManagerListener
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;

	void load(SimpleXML& aXml);
	void loadProfile(SimpleXML& aXml, const string& aName, ProfileToken aToken);
	void save(SimpleXML& aXml);

	void reportTaskStatus(const ShareRefreshTask& aTask, bool aFinished, const ShareRefreshStats* aStats) const noexcept;
	
	ShareProfileList shareProfiles;
}; //sharemanager end

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
