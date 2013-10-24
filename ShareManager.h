/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#include <string>
#include "TimerManager.h"
#include "SettingsManager.h"
#include "QueueManagerListener.h"

#include "AdcSearch.h"
#include "BloomFilter.h"
#include "CriticalSection.h"
#include "Exception.h"
#include "Flags.h"
#include "HashBloom.h"
#include "HashedFile.h"
#include "LogManager.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "SearchManager.h"
#include "Singleton.h"
#include "ShareProfile.h"
#include "SortedVector.h"
#include "StringMatch.h"
#include "StringSearch.h"
#include "TaskQueue.h"
#include "Thread.h"
#include "UserConnection.h"

#include "DirectoryMonitor.h"
#include "DirectoryMonitorListener.h"
#include "DualString.h"

namespace dcpp {

STANDARD_EXCEPTION(ShareException);

class SimpleXML;
class Client;
class File;
class OutputStream;
class MemoryInputStream;
//struct ShareLoader;
class AdcSearch;
class Worker;
class TaskQueue;

class ShareDirInfo;
typedef boost::intrusive_ptr<ShareDirInfo> ShareDirInfoPtr;

class ShareDirInfo : public FastAlloc<ShareDirInfo>, public intrusive_ptr_base<ShareDirInfo> {
public:
	enum DiffState { 
		DIFF_NORMAL,
		DIFF_ADDED,
		DIFF_REMOVED
	};

	enum State { 
		STATE_NORMAL,
		STATE_ADDED,
		STATE_REMOVED,
		STATE_CHANGED
	};

	ShareDirInfo(const ShareDirInfoPtr& aInfo, ProfileToken aNewProfile);
	ShareDirInfo(const string& aVname, ProfileToken aProfile, const string& aPath, bool aIncoming = false, State aState = STATE_NORMAL);

	~ShareDirInfo() {}

	// item currently exists in the profile
	bool isCurItem() const {
		return diffState != DIFF_REMOVED && state != STATE_REMOVED;
	}

	string vname;
	ProfileToken profile;
	string path;
	bool incoming;
	bool found; //used when detecting removed dirs with using dir tree
	int64_t size;

	DiffState diffState;
	State state;

	/*struct Hash {
		size_t operator()(const ShareDirInfo* x) const { return hash<string>()(x->path) + x->profile; }
	};

	typedef unordered_set<ShareDirInfoPtr, Hash> set;*/
	typedef vector<ShareDirInfoPtr> List;
	typedef unordered_map<int, List> Map;

	class PathCompare {
	public:
		PathCompare(const string& compareTo) : a(compareTo) { }
		bool operator()(const ShareDirInfoPtr& p) { return Util::stricmp(p->path.c_str(), a.c_str()) == 0; }
	private:
		PathCompare& operator=(const PathCompare&) ;
		const string& a;
	};
};

class ShareProfile;
class FileList;

class ShareManager : public Singleton<ShareManager>, private Thread, private SettingsManagerListener, private TimerManagerListener, private QueueManagerListener, private DirectoryMonitorListener
{
public:
	/**
	 * @param aDirectory Physical directory location
	 * @param aName Virtual name
	 */

	void setSkipList();

	bool matchSkipList(const string& aStr) const noexcept { return skipList.match(aStr); }
	bool checkSharedName(const string& fullPath, const string& fullPathLower, bool dir, bool report = true, int64_t size = 0) const noexcept;
	void validatePath(const string& realPath, const string& virtualName) const throw(ShareException);

	string toVirtual(const TTHValue& tth, ProfileToken aProfile) const throw(ShareException);
	pair<int64_t, string> getFileListInfo(const string& virtualFile, ProfileToken aProfile) throw(ShareException);
	void toRealWithSize(const string& virtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_) throw(ShareException);
	TTHValue getListTTH(const string& virtualFile, ProfileToken aProfile) const throw(ShareException);
	
	enum RefreshType {
		TYPE_MANUAL,
		TYPE_SCHEDULED,
		TYPE_STARTUP_BLOCKING,
		TYPE_STARTUP_DELAYED,
		TYPE_MONITORING,
		TYPE_BUNDLE
	};

	int refresh(bool incoming, RefreshType aType, function<void(float)> progressF = nullptr) noexcept;
	int refresh(const string& aDir) noexcept;

	bool isRefreshing() const noexcept { return refreshRunning; }
	
	//need to be called from inside a lock.
	void setProfilesDirty(ProfileTokenSet aProfiles, bool forceXmlRefresh=false) noexcept;

	void startup(function<void(const string&)> stepF, function<void(float)> progressF) noexcept;
	void shutdown(function<void(float)> progressF) noexcept;
	void abortRefresh() noexcept;

	void changeExcludedDirs(const ProfileTokenStringList& aAdd, const ProfileTokenStringList& aRemove) noexcept;
	void rebuildTotalExcludes() noexcept;

	void search(SearchResultList& l, const string& aString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults, bool aHideShare) noexcept;
	void search(SearchResultList& l, AdcSearch& aSearch, StringList::size_type maxResults, ProfileToken aProfile, const CID& cid, const string& aDir) throw(ShareException);

	bool isDirShared(const string& aDir) const noexcept;
	uint8_t isDirShared(const string& aPath, int64_t aSize) const noexcept;
	bool isFileShared(const TTHValue& aTTH) const noexcept;
	bool isFileShared(const string& aFileName, int64_t aSize) const noexcept;
	bool isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept;

	bool allowAddDir(const string& dir) const noexcept;
	StringList getDirPaths(const string& aDir) const noexcept;

	bool loadCache(function<void (float)> progressF) noexcept;

	vector<pair<string, StringList>> getGroupedDirectories() const noexcept;
	static bool checkType(const string& aString, int aType);
	MemoryInputStream* generatePartialList(const string& dir, bool recurse, ProfileToken aProfile) const noexcept;
	MemoryInputStream* generateTTHList(const string& dir, bool recurse, ProfileToken aProfile) const noexcept;
	MemoryInputStream* getTree(const string& virtualFile, ProfileToken aProfile) const noexcept;

	void saveXmlList(bool verbose=false, function<void (float)> progressF = nullptr) noexcept;	//for filelist caching

	AdcCommand getFileInfo(const string& aFile, ProfileToken aProfile) throw(ShareException);

	int64_t getTotalShareSize(ProfileToken aProfile) const noexcept;
	int64_t getShareSize(const string& realPath, ProfileToken aProfile) const noexcept;
	void getProfileInfo(ProfileToken aProfile, int64_t& size, size_t& files) const noexcept;
	
	void getBloom(HashBloom& bloom) const noexcept;

	static SearchManager::TypeModes getType(const string& fileName) noexcept;

	string validateVirtual(const string& /*aVirt*/) const noexcept;
	void addHits(uint32_t aHits) noexcept{
		hits += aHits;
	}

	string generateOwnList(ProfileToken aProfile) throw(ShareException);

	bool isTTHShared(const TTHValue& tth) const noexcept;

	// Get real paths for an ADC virtual path
	void getRealPaths(const string& path, StringList& ret, ProfileToken aProfile) const throw(ShareException);

	string getRealPath(const TTHValue& root) const throw(ShareException);

	enum { 
		REFRESH_STARTED = 0,
		REFRESH_PATH_NOT_FOUND = 1,
		REFRESH_IN_PROGRESS = 2,
		REFRESH_ALREADY_QUEUED = 3
	};

	GETSET(bool, monitorDebug, MonitorDebug);
	GETSET(size_t, hits, Hits);
	GETSET(int64_t, sharedSize, SharedSize);

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
	TempShareMap& getTempShares() { return tempShares; }

	void removeTempShare(const string& aKey, const TTHValue& tth);
	void clearTempShares();
	bool isTempShared(const string& aKey, const TTHValue& tth);
	//tempShares end

	typedef vector<ShareProfilePtr> ShareProfileList;

	void getShares(ShareDirInfo::Map& aDirs) const noexcept;

	enum Tasks {
		ASYNC,
		ADD_DIR,
		REFRESH_ALL,
		REFRESH_DIRS,
		REFRESH_INCOMING,
		ADD_BUNDLE
	};

	ShareProfilePtr getShareProfile(ProfileToken aProfile, bool allowFallback = false) const noexcept;
	void getParentPaths(StringList& aDirs) const noexcept;
	void countStats(uint64_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles, size_t& lowerCaseFiles, size_t& totalStrLen_, size_t& roots_) const noexcept;

	void addDirectories(const ShareDirInfo::List& aNewDirs) noexcept;
	void removeDirectories(const ShareDirInfo::List& removeDirs) noexcept;
	void changeDirectories(const ShareDirInfo::List& renameDirs) noexcept;

	void addProfiles(const ShareProfileInfo::List& aProfiles) noexcept;
	void removeProfiles(const ShareProfileInfo::List& aProfiles) noexcept;
	void renameProfiles(const ShareProfileInfo::List& aProfiles) noexcept;

	bool isRealPathShared(const string& aPath) noexcept;
	ShareProfilePtr getProfile(ProfileToken aProfile) const noexcept;

	/* Only for gui use purposes, no locking */
	const ShareProfileList& getProfiles() { return shareProfiles; }
	ShareProfileInfo::List getProfileInfos() const noexcept;
	void getExcludes(ProfileToken aProfile, StringList& excludes) const noexcept;
	optional<ProfileToken> getProfileByName(const string& aName) const noexcept;

	string printStats() const noexcept;
	mutable SharedMutex cs;

	int addRefreshTask(uint8_t aTaskType, StringList& dirs, RefreshType aRefreshType, const string& displayName=Util::emptyString, function<void (float)> progressF = nullptr) noexcept;
	struct ShareLoader;

	void rebuildMonitoring() noexcept;
	void handleChangedFiles() noexcept;
private:
	unique_ptr<DirectoryMonitor> monitor;

	uint64_t totalSearches;
	typedef BloomFilter<5> ShareBloom;

	class ProfileDirectory : public intrusive_ptr_base<ProfileDirectory>, boost::noncopyable, public Flags {
		public:
			typedef boost::intrusive_ptr<ProfileDirectory> Ptr;

			ProfileDirectory(const string& aRootPath, const string& aVname, ProfileToken aProfile, bool incoming = false);
			ProfileDirectory(const string& aRootPath, ProfileToken aProfile);

			GETSET(string, path, Path);

			//lists the profiles where this directory is set as root and virtual names
			GETSET(ProfileTokenStringMap, rootProfiles, RootProfiles);
			GETSET(ProfileTokenSet, excludedProfiles, ExcludedProfiles);
			GETSET(bool, cacheDirty, CacheDirty);

			~ProfileDirectory() { }

			enum InfoFlags {
				FLAG_ROOT				= 0x01,
				FLAG_EXCLUDE_TOTAL		= 0x02,
				FLAG_EXCLUDE_PROFILE	= 0x04,
				FLAG_INCOMING			= 0x08
			};

			bool hasExcludes() const noexcept { return !excludedProfiles.empty(); }
			bool hasRoots() const noexcept { return !rootProfiles.empty(); }

			bool hasRootProfile(ProfileToken aProfile) const noexcept;
			bool hasRootProfile(const ProfileTokenSet& aProfiles) const noexcept;
			bool isExcluded(ProfileToken aProfile) const noexcept;
			bool isExcluded(const ProfileTokenSet& aProfiles) const noexcept;
			void addRootProfile(const string& aName, ProfileToken aProfile) noexcept;
			void addExclude(ProfileToken aProfile) noexcept;
			bool removeRootProfile(ProfileToken aProfile) noexcept;
			bool removeExcludedProfile(ProfileToken aProfile) noexcept;
			string getName(ProfileToken aProfile) const noexcept;

			string getCacheXmlPath() const noexcept;
	};

	unique_ptr<ShareBloom> bloom;

	struct FileListDir;
	class Directory : public intrusive_ptr_base<Directory>, boost::noncopyable {
	public:
		typedef boost::intrusive_ptr<Directory> Ptr;
		typedef unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> Map;
		typedef Map::iterator MapIter;
		typedef std::vector<Directory::Ptr> List;

		struct NameLower {
			const string& operator()(const Ptr& a) const { return a->name.getLower(); }
		};

		class File {
		public:
			/*struct FileLess {
				bool operator()(const File& a, const File& b) const { return strcmp(a.name.getLower().c_str(), b.name.getLower().c_str()) < 0; }
			};*/

			struct NameLower {
				const string& operator()(const File* a) const { return a->name.getLower(); }
			};

			//typedef set<File, FileLess> Set;
			typedef SortedVector<File*, std::vector, string, Compare, NameLower> Set;

			File(DualString&& aName, const Directory::Ptr& aParent, HashedFile& aFileInfo);
			~File();

			/*bool operator==(const File& rhs) const {
				return name.getLower().compare(rhs.name.getLower()) == 0 && parent == rhs.getParent();
			}*/
		
			string getADCPath(ProfileToken aProfile) const { return parent->getADCPath(aProfile) + name.getNormal(); }
			string getFullName(ProfileToken aProfile) const { return parent->getFullName(aProfile) + name.getNormal(); }
			string getRealPath(bool validate = true) const { return parent->getRealPath(name.getNormal(), validate); }
			bool hasProfile(ProfileToken aProfile) const noexcept { return parent->hasProfile(aProfile); }

			void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const;
			void addSR(SearchResultList& aResults, ProfileToken aProfile, bool addParent) const noexcept;

			GETSET(int64_t, size, Size);
			GETSET(Directory*, parent, Parent);
			GETSET(uint64_t, lastWrite, LastWrite);
			GETSET(TTHValue, tth, TTH);

			DualString name;
		};

		//typedef set<Directory::Ptr, DirLess> Set;

		typedef SortedVector<Ptr, std::vector, string, Compare, NameLower> Set;
		Set directories;
		File::Set files;

		static Ptr create(DualString&& aRealName, const Ptr& aParent, uint64_t aLastWrite, ProfileDirectory::Ptr aRoot = nullptr);

		struct HasRootProfile {
			HasRootProfile(ProfileToken aT) : t(aT) { }
			bool operator()(const Ptr& d) const {
				return d->getProfileDir()->hasRootProfile(t);
			}
			ProfileToken t;
		private:
			HasRootProfile& operator=(const HasRootProfile&);
		};

		struct IsParent {
			bool operator()(const Ptr& d) const {
				return !d->getParent();
			}
		};

		bool hasType(uint32_t type) const noexcept {
			return ( (type == SearchManager::TYPE_ANY) || (fileTypes & (1 << type)) );
		}
		void addType(uint32_t type) noexcept;

		string getADCPath(ProfileToken aProfile) const noexcept;
		string getVirtualName(ProfileToken aProfile) const noexcept;
		string getFullName(ProfileToken aProfile) const noexcept; 
		string getRealPath(bool checkExistance) const throw(ShareException)  { return getRealPath(Util::emptyString, checkExistance); };

		bool hasProfile(const ProfileTokenSet& aProfiles) const noexcept;
		bool hasProfile(ProfileToken aProfiles) const noexcept;

		void getResultInfo(ProfileToken aProfile, int64_t& size_, size_t& files_, size_t& folders_) const noexcept;
		int64_t getSize(ProfileToken aProfile) const noexcept;
		int64_t getTotalSize() const noexcept;
		void getProfileInfo(ProfileToken aProfile, int64_t& totalSize, size_t& filesCount) const noexcept;

		void search(SearchResultList& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults, ProfileToken aProfile) const noexcept;
		void search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults, ProfileToken aProfile) const noexcept;

		void toFileList(FileListDir* aListDir, ProfileToken aProfile, bool isFullList);
		void toXml(SimpleXML& aXml, bool fullList, ProfileToken aProfile) const;
		void toTTHList(OutputStream& tthList, string& tmp2, bool recursive) const;

		//for file list caching
		void toXmlList(OutputStream& xmlFile, string&& path, string& indent, string& tmp);
		void filesToXmlList(OutputStream& xmlFile, string& indent, string& tmp2) const;

		GETSET(uint64_t, lastWrite, LastWrite);
		GETSET(Directory*, parent, Parent);
		GETSET(ProfileDirectory::Ptr, profileDir, ProfileDir);

		Directory(DualString&& aRealName, const Ptr& aParent, uint64_t aLastWrite, ProfileDirectory::Ptr root = nullptr);
		~Directory();

		void copyRootProfiles(ProfileTokenSet& aProfiles, bool setCacheDirty) const noexcept;
		bool isRootLevel(ProfileToken aProfile) const noexcept;
		bool isLevelExcluded(ProfileToken aProfile) const noexcept;
		bool isLevelExcluded(const ProfileTokenSet& aProfiles) const noexcept;
		int64_t size;

		void addBloom(ShareBloom& aBloom) const noexcept;

		void countStats(uint64_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles, size_t& lowerCaseFiles, size_t& totalStrLen_) const noexcept;
		DualString name;

		void getRenameInfoList(const string& aPath, RenameList& aRename) noexcept;
		Directory::Ptr findDirByPath(const string& aPath, char separator) const noexcept;
	private:
		friend void intrusive_ptr_release(intrusive_ptr_base<Directory>*);
		/** Set of flags that say which SearchManager::TYPE_* a directory contains */
		uint32_t fileTypes;

		string getRealPath(const string& path, bool checkExistance) const throw(ShareException);
	};

	struct FileListDir {
		typedef unordered_map<string, FileListDir*, noCaseStringHash, noCaseStringEq> ListDirectoryMap;
		Directory::List shareDirs;

		FileListDir(const string& aName, int64_t aSize, int aDate);
		~FileListDir();

		string name;
		int64_t size;
		uint64_t date;
		ListDirectoryMap listDirs;

		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) const;
		void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const;
	};

	void addAsyncTask(AsyncF aF) noexcept;
	void getDirsByName(const string& aPath, Directory::List& dirs_) const noexcept;

	/* Directory items mapped to realpath*/
	typedef map<string, Directory::Ptr> DirMap;

	void addRoot(const string& aPath, Directory::Ptr& aDir) noexcept;
	DirMap::const_iterator findRoot(const string& aPath) const noexcept;

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

	bool addDirResult(const string& aPath, SearchResultList& aResults, ProfileToken aProfile, AdcSearch& srch) const noexcept;

	typedef unordered_map<string, ProfileDirectory::Ptr, noCaseStringHash, noCaseStringEq> ProfileDirMap;
	ProfileDirMap profileDirs;

	ProfileDirMap getSubProfileDirs(const string& aPath) const noexcept;

	TaskQueue tasks;

	FileList* generateXmlList(ProfileToken aProfile, bool forced = false) throw(ShareException);
	FileList* getFileList(ProfileToken aProfile) const throw(ShareException);

	volatile bool aShutdown;

	static boost::regex rxxReg;
	
	static atomic_flag refreshing;
	bool refreshRunning;

	uint64_t lastFullUpdate;
	uint64_t lastIncomingUpdate;
	uint64_t lastSave;
	
	//caching the share size so we dont need to loop tthindex everytime
	bool xml_saving;

	mutable SharedMutex dirNames; // Bundledirs, releasedirs and excluded dirs

	int refreshOptions;

	/*
	multimap to allow multiple same key values, needed to return from some functions.
	*/
	typedef unordered_multimap<string*, Directory::Ptr, noCaseStringHash, noCaseStringEq> DirMultiMap; 

	/** Map real name to virtual name - multiple real names may be mapped to a single virtual one */
	DirMap rootPaths;
	DirMultiMap dirNameMap;

	class RefreshInfo : boost::noncopyable {
	public:
		RefreshInfo(const string& aPath, const Directory::Ptr& aOldRoot, uint64_t aLastWrite);
		~RefreshInfo();

		Directory::Ptr oldRoot;
		Directory::Ptr root;
		int64_t hashSize;
		int64_t addedSize;
		ProfileDirMap subProfiles;
		DirMultiMap dirNameMapNew;
		HashFileMap tthIndexNew;
		DirMap rootPathsNew;

		string path;

		/*struct FileCount {
			int64_t operator()(const RefreshInfo& ri) const {
				return ri.root->getProfileDir() ? ri.root->getProfileDir()->getRootProfiles();
			}
		};*/
	};

	typedef shared_ptr<RefreshInfo> RefreshInfoPtr;
	typedef vector<RefreshInfoPtr> RefreshInfoList;

	//void mergeRefreshChanges(RefreshInfoList& aList, DirMultiMap& aDirNameMap, DirMap& aRootPaths, HashFileMap& aTTHIndex, int64_t& totalHash, int64_t& totalAdded, ProfileTokenSet* dirtyProfiles);
	template<typename T>
	void mergeRefreshChanges(T& aList, DirMultiMap& aDirNameMap, DirMap& aRootPaths, HashFileMap& aTTHIndex, int64_t& totalHash, int64_t& totalAdded, ProfileTokenSet* dirtyProfiles) noexcept {
		for (const auto& i: aList) {
			auto& ri = *i;
			aDirNameMap.insert(ri.dirNameMapNew.begin(), ri.dirNameMapNew.end());
			aRootPaths.insert(ri.rootPathsNew.begin(), ri.rootPathsNew.end());
			aTTHIndex.insert(ri.tthIndexNew.begin(), ri.tthIndexNew.end());

			totalHash += ri.hashSize;
			totalAdded += ri.addedSize;

			if (dirtyProfiles)
				ri.root->copyRootProfiles(*dirtyProfiles, true);
		}
	}

	void buildTree(string& aPath, string& aPathLower, const Directory::Ptr& aDir, const ProfileDirMap& aSubRoots, DirMultiMap& aDirs, DirMap& newShares, int64_t& hashSize, int64_t& addedSize, HashFileMap& tthIndexNew, ShareBloom& aBloom) noexcept;
	void addFile(const string& aName, Directory::Ptr& aDir, HashedFile& fi, ProfileTokenSet& dirtyProfiles_) noexcept;

	//void rebuildIndices();
	static void updateIndices(Directory::Ptr& aDirectory, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex, DirMultiMap& aDirNames) noexcept;
	static void updateIndices(Directory& dir, const Directory::File* f, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex) noexcept;
	void cleanIndices(Directory& dir) noexcept;
	void addDirName(Directory::Ptr& dir) noexcept;
	void removeDirName(Directory& dir) noexcept;
	void cleanIndices(Directory& dir, const Directory::File* f) noexcept;

	void onFileHashed(const string& fname, HashedFile& fileInfo) noexcept;
	
	StringList bundleDirs;

	void getByVirtual(const string& virtualName, ProfileToken aProfiles, Directory::List& dirs) const noexcept;
	void getByVirtual(const string& virtualName, const ProfileTokenSet& aProfiles, Directory::List& dirs) const noexcept;
	//void findVirtuals(const string& virtualPath, ProfileToken aProfiles, Directory::List& dirs) const;

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

		getByVirtual( virtualPath.substr(1, start-1), aProfile, virtuals);
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
				if(mi != d->directories.end() && !(*mi)->isLevelExcluded(aProfile)) {   //if we found something, look for more.
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

	/*template<class T>
	Directory::Ptr findDirectory(const string& virtualPath, const T& aProfile, const Directory::Ptr& aDir) const noexcept {
		string::size_type i = 0; // always start from the begin.
		string::size_type j = 1;

		Directory::Ptr d = aDir;
		while((i = virtualPath.find('/', j)) != string::npos) {
			auto mi = d->directories.find(virtualPath.substr(j, i - j));
			j = i + 1;
			if(mi != d->directories.end() && !(*mi)->isLevelExcluded(aProfile)) {   //if we found something, look for more.
				d = *mi;
			} else {
				return nullptr;
			}
		}

		return d;
	}*/

	string findRealRoot(const string& virtualRoot, const string& virtualLeaf) const throw(ShareException);

	Directory::Ptr findDirectory(const string& fname, bool allowAdd, bool report, bool checkExcludes=true) noexcept;

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

	//Directory::Ptr ShareManager::removeFileOrDirectory(Directory::Ptr& aDir, const string& aName) throw(ShareException);

	void load(SimpleXML& aXml);
	void loadProfile(SimpleXML& aXml, const string& aName, ProfileToken aToken);
	void save(SimpleXML& aXml);

	void reportTaskStatus(uint8_t aTask, const StringList& aDirectories, bool finished, int64_t aHashSize, const string& displayName, RefreshType aRefreshType) const noexcept;
	
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
	struct FileAddInfo {
		FileAddInfo(string&& aName, uint64_t aLastWrite, int64_t aSize) : name(aName), lastWrite(aLastWrite), size(aSize) { }

		string name;
		uint64_t lastWrite;
		int64_t size;
	};

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
}; //sharemanager end

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
