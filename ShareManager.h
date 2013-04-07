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

#include <boost/unordered_map.hpp>

namespace dcpp {

STANDARD_EXCEPTION(ShareException);

class SimpleXML;
class Client;
class File;
class OutputStream;
class MemoryInputStream;
struct ShareLoader;
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
		STATE_REMOVED
	};

	ShareDirInfo(const string& aVname, ProfileToken aProfile, const string& aPath, bool aIncoming=false) : vname(aVname), profile(aProfile), path(aPath), incoming(aIncoming), 
		found(false), diffState(DIFF_NORMAL), state(STATE_NORMAL), size(0) {}

	~ShareDirInfo() {}

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
};

class ShareProfile;
class FileList;

class ShareManager : public Singleton<ShareManager>, private Thread, private SettingsManagerListener, private TimerManagerListener, private QueueManagerListener
{
public:
	/**
	 * @param aDirectory Physical directory location
	 * @param aName Virtual name
	 */

	void setSkipList();

	bool matchSkipList(const string& aStr) { return skipList.match(aStr); }
	bool checkSharedName(const string& fullPath, bool dir, bool report = true, int64_t size = 0);
	void validatePath(const string& realPath, const string& virtualName);

	string toVirtual(const TTHValue& tth, ProfileToken aProfile) const;
	pair<int64_t, string> getFileListInfo(const string& virtualFile, ProfileToken aProfile);
	void toRealWithSize(const string& virtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_);
	TTHValue getListTTH(const string& virtualFile, ProfileToken aProfile) const;
	
	enum RefreshType {
		TYPE_MANUAL,
		TYPE_SCHEDULED,
		TYPE_STARTUP
	};

	int refresh(bool incoming, RefreshType aType, function<void (float)> progressF = nullptr);
	int refresh(const string& aDir);

	bool isRefreshing() {	return refreshRunning; }
	
	//need to be called from inside a lock.
	void setProfilesDirty(ProfileTokenSet aProfiles, bool forceXmlRefresh=false);

	void startup(function<void (const string&)> stepF, function<void (float)> progressF);
	void shutdown(function<void (float)> progressF);
	void abortRefresh();

	void changeExcludedDirs(const ProfileTokenStringList& aAdd, const ProfileTokenStringList& aRemove);
	void rebuildTotalExcludes();

	void search(SearchResultList& l, const string& aString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults, bool aHideShare) noexcept;
	void search(SearchResultList& l, AdcSearch& aSearch, StringList::size_type maxResults, ProfileToken aProfile, const CID& cid, const string& aDir);

	bool isDirShared(const string& aDir) const;
	uint8_t isDirShared(const string& aPath, int64_t aSize) const;
	bool isFileShared(const TTHValue& aTTH, const string& fileName) const;
	bool isFileShared(const string& aFileName, int64_t aSize) const;
	bool isFileShared(const TTHValue& aTTH, const string& fileName, ProfileToken aProfile) const;

	bool allowAddDir(const string& dir);
	tstring getDirPath(const string& directory);

	bool loadCache(function<void (float)> progressF);

	vector<pair<string, StringList>> getGroupedDirectories() const noexcept;
	static bool checkType(const string& aString, int aType);
	MemoryInputStream* generatePartialList(const string& dir, bool recurse, ProfileToken aProfile) const;
	MemoryInputStream* generateTTHList(const string& dir, bool recurse, ProfileToken aProfile) const;
	MemoryInputStream* getTree(const string& virtualFile, ProfileToken aProfile) const;

	void saveXmlList(bool verbose=false, function<void (float)> progressF = nullptr);	//for filelist caching

	AdcCommand getFileInfo(const string& aFile, ProfileToken aProfile);

	int64_t getTotalShareSize(ProfileToken aProfile) const noexcept;
	int64_t getShareSize(const string& realPath, ProfileToken aProfile) const noexcept;
	void getProfileInfo(ProfileToken aProfile, int64_t& size, size_t& files) const;
	
	void getBloom(HashBloom& bloom) const;

	static SearchManager::TypeModes getType(const string& fileName) noexcept;

	string validateVirtual(const string& /*aVirt*/) const noexcept;
	void addHits(uint32_t aHits) {
		hits += aHits;
	}

	string generateOwnList(ProfileToken aProfile);

	bool isTTHShared(const TTHValue& tth) const;

	void getRealPaths(const string& path, StringList& ret, ProfileToken aProfile) const;

	string getRealPath(const TTHValue& root) const;
	string getRealPath(const string& aFileName, int64_t aSize) const;

	enum { 
		REFRESH_STARTED = 0,
		REFRESH_PATH_NOT_FOUND = 1,
		REFRESH_IN_PROGRESS = 2,
		REFRESH_ALREADY_QUEUED = 3
	};

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
	bool isTempShared(const string& aKey, const TTHValue& tth);
	//tempShares end

	typedef vector<ShareProfilePtr> ShareProfileList;

	void getShares(ShareDirInfo::Map& aDirs);

	enum Tasks {
		ADD_DIR,
		REFRESH_ALL,
		REFRESH_DIR,
		REFRESH_INCOMING
	};

	ShareProfilePtr getShareProfile(ProfileToken aProfile, bool allowFallback=false) const;
	void getParentPaths(StringList& aDirs) const;

	void addDirectories(const ShareDirInfo::List& aNewDirs);
	void removeDirectories(const ShareDirInfo::List& removeDirs);
	void changeDirectories(const ShareDirInfo::List& renameDirs);

	void addProfiles(const ShareProfile::set& aProfiles);
	void removeProfiles(ProfileTokenList aProfiles);

	/* Only for gui use purposes, no locking */
	const ShareProfileList& getProfiles() { return shareProfiles; }
	void getExcludes(ProfileToken aProfile, StringList& excludes);
private:
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

			bool hasExcludes() const { return !excludedProfiles.empty(); }
			bool hasRoots() const { return !rootProfiles.empty(); }

			bool hasRootProfile(ProfileToken aProfile) const;
			bool hasRootProfile(const ProfileTokenSet& aProfiles) const;
			bool isExcluded(ProfileToken aProfile) const;
			bool isExcluded(const ProfileTokenSet& aProfiles) const;
			void addRootProfile(const string& aName, ProfileToken aProfile);
			void addExclude(ProfileToken aProfile);
			bool removeRootProfile(ProfileToken aProfile);
			bool removeExcludedProfile(ProfileToken aProfile);
			string getName(ProfileToken aProfile) const;

			unique_ptr<ShareBloom> bloom;
			string getCachePath() const;
	};

	struct FileListDir;
	class Directory : public intrusive_ptr_base<Directory>, boost::noncopyable {
	public:
		typedef boost::intrusive_ptr<Directory> Ptr;
		typedef unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> Map;
		typedef Map::iterator MapIter;

		struct DirLess {
			bool operator()(const Ptr& a, const Ptr& b) const { return (compare(a->getRealNameLower(), b->getRealNameLower()) < 0); }
		};

		struct NameLower {
			const string& operator()(const Ptr& a) const { return a->getRealNameLower(); }
		};

		class File {
		public:
			struct FileLess {
				bool operator()(const File& a, const File& b) const { return (compare(a.getNameLower(), b.getNameLower()) < 0); }
			};
			typedef set<File, FileLess> Set;

			File(const string& aName, int64_t aSize, Directory::Ptr aParent, HashedFilePtr& aFileInfo);
			~File();

			bool operator==(const File& rhs) const {
				return getNameLower().compare(rhs.getNameLower()) == 0 && parent == rhs.getParent();
			}
		
			string getADCPath(ProfileToken aProfile) const { return parent->getADCPath(aProfile) + getName(); }
			string getFullName(ProfileToken aProfile) const { return parent->getFullName(aProfile) + getName(); }
			string getRealPath(bool validate = true) const { return parent->getRealPath(getName(), validate); }
			bool hasProfile(ProfileToken aProfile) const { return parent->hasProfile(aProfile); }

			void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const;
			void addSR(SearchResultList& aResults, ProfileToken aProfile, bool addParent) const;

			GETSET(int64_t, size, Size);
			GETSET(Directory*, parent, Parent);
			GETSET(HashedFilePtr, fileInfo, FileInfo);

			const string& getNameLower() const { return fileInfo->getFileName(); }
			const string& getName() const { return name ? *name : fileInfo->getFileName(); }
			const TTHValue& getTTH() const { return fileInfo->getRoot(); }
			uint32_t getLastWrite() const { return fileInfo->getTimeStamp(); }

		private:
			File(const File& src);
			string* name;
		};

		//typedef set<Directory::Ptr, DirLess> Set;

		typedef SortedVector<Ptr, string, DirLess, NameLower> Set;
		Set directories;
		File::Set files;

		static Ptr create(const string& aName, const Ptr& aParent, uint32_t&& aLastWrite, ProfileDirectory::Ptr aRoot = nullptr);

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
		string getRealName() const { return realName; }
		string getFullName(ProfileToken aProfile) const noexcept; 
		string getRealPath(bool checkExistance) const { return getRealPath(Util::emptyString, checkExistance); };

		bool hasProfile(const ProfileTokenSet& aProfiles) const noexcept;
		bool hasProfile(ProfileToken aProfiles) const noexcept;

		void getResultInfo(ProfileToken aProfile, int64_t& size_, size_t& files_, size_t& folders_) const noexcept;
		int64_t getSize(ProfileToken aProfile) const noexcept;
		int64_t getTotalSize() const noexcept;
		void getProfileInfo(ProfileToken aProfile, int64_t& totalSize, size_t& filesCount) const;

		void search(SearchResultList& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults) const noexcept;
		void search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults, ProfileToken aProfile) const noexcept;

		void toFileList(FileListDir* aListDir, ProfileToken aProfile, bool isFullList);
		void toXml(SimpleXML& aXml, bool fullList, ProfileToken aProfile) const;
		void toTTHList(OutputStream& tthList, string& tmp2, bool recursive) const;

		//for file list caching
		void toXmlList(OutputStream& xmlFile, string&& path, string& indent, string& tmp);
		void filesToXmlList(OutputStream& xmlFile, string& indent, string& tmp2) const;

		GETSET(uint32_t, lastWrite, LastWrite);
		GETSET(Directory*, parent, Parent);
		GETSET(ProfileDirectory::Ptr, profileDir, ProfileDir);

		Directory(const string& aRealName, const Ptr& aParent, uint32_t aLastWrite, ProfileDirectory::Ptr root = nullptr);
		~Directory() { }

		void copyRootProfiles(ProfileTokenSet& aProfiles, bool setCacheDirty) const;
		bool isRootLevel(ProfileToken aProfile) const;
		bool isLevelExcluded(ProfileToken aProfile) const;
		bool isLevelExcluded(const ProfileTokenSet& aProfiles) const;
		int64_t size;

		const string& getRealNameLower() const { return realNameLower; }

		File::Set::const_iterator findFile(const string& aName) const;

		void addBloom(ShareBloom& aBloom) const;
		bool matchBloom(const StringSearch::List& aSearches) const;
		ShareBloom& getBloom() const;
	private:
		friend void intrusive_ptr_release(intrusive_ptr_base<Directory>*);
		/** Set of flags that say which SearchManager::TYPE_* a directory contains */
		uint32_t fileTypes;

		string getRealPath(const string& path, bool checkExistance) const;
		string realName;
		string realNameLower;
	};

	struct FileListDir {
		typedef unordered_map<string, FileListDir*, noCaseStringHash, noCaseStringEq> ListDirectoryMap;
		vector<Directory::Ptr> shareDirs;

		FileListDir(const string& aName, int64_t aSize, int aDate);
		~FileListDir();

		string name;
		int64_t size;
		uint32_t date;
		ListDirectoryMap listDirs;

		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) const;
		void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const;
	};

	int addTask(uint8_t aTaskType, StringList& dirs, RefreshType aRefreshType, const string& displayName=Util::emptyString, function<void (float)> progressF = nullptr) noexcept;
	Directory::Ptr getDirByName(const string& directory) const;

	/* Directory items mapped to realpath*/
	typedef map<string, Directory::Ptr> DirMap;

	void addRoot(const string& aPath, Directory::Ptr& aDir);
	DirMap::const_iterator findRoot(const string& aPath) const;

	friend struct ShareLoader;

	friend class Singleton<ShareManager>;

	typedef unordered_multimap<TTHValue*, Directory::File::Set::const_iterator> HashFileMap;
	typedef HashFileMap::const_iterator HashFileIter;

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

	bool addDirResult(const string& aPath, SearchResultList& aResults, ProfileToken aProfile, AdcSearch& srch) const;
	typedef boost::unordered_map<string, ProfileDirectory::Ptr, noCaseStringHash, noCaseStringEq> ProfileDirMap;
	ProfileDirMap profileDirs;

	ProfileDirMap getSubProfileDirs(const string& aPath);

	TaskQueue tasks;

	FileList* generateXmlList(ProfileToken aProfile, bool forced = false);
	FileList* getFileList(ProfileToken aProfile) const;

	volatile bool aShutdown;

	static boost::regex rxxReg;
	
	static atomic_flag refreshing;
	bool refreshRunning;

	uint64_t lastFullUpdate;
	uint64_t lastIncomingUpdate;
	uint64_t lastSave;
	uint32_t findLastWrite(const string& aName) const;
	
	//caching the share size so we dont need to loop tthindex everytime
	bool xml_saving;

	mutable SharedMutex cs;  // NON-recursive mutex BE Aware!!
	mutable SharedMutex dirNames; // Bundledirs, releasedirs and excluded dirs

	int refreshOptions;

	/*
	multimap to allow multiple same key values, needed to return from some functions.
	*/
	typedef boost::unordered_multimap<string, Directory::Ptr, noCaseStringHash, noCaseStringEq> DirMultiMap; 

	//list to return multiple directory item pointers
	typedef std::vector<Directory::Ptr> DirectoryList;

	/** Map real name to virtual name - multiple real names may be mapped to a single virtual one */
	DirMap rootPaths;
	DirMultiMap dirNameMap;

	class RefreshInfo {
	public:
		RefreshInfo(const string& aPath, Directory::Ptr aOldRoot, const string& loaderPath = Util::emptyString);
		~RefreshInfo();

		Directory::Ptr oldRoot;
		Directory::Ptr root;
		int64_t hashSize;
		int64_t addedSize;
		unique_ptr<ShareBloom> newBloom;
		ProfileDirMap subProfiles;
		DirMultiMap dirNameMapNew;
		HashFileMap tthIndexNew;
		DirMap rootPathsNew;
		//string path;

		string loaderPath;

		RefreshInfo(RefreshInfo&&);
		RefreshInfo& operator=(RefreshInfo&&) { return *this; }
	private:
		RefreshInfo(const RefreshInfo&);
		RefreshInfo& operator=(const RefreshInfo&);
	};

	typedef vector<RefreshInfo> RefreshInfoList;

	void mergeRefreshChanges(RefreshInfoList& aList, DirMultiMap& aDirNameMap, DirMap& aRootPaths, HashFileMap& aTTHIndex, int64_t& totalHash, int64_t& totalAdded);


	void buildTree(const string& aPath, const Directory::Ptr& aDir, bool checkQueued, const ProfileDirMap& aSubRoots, DirMultiMap& aDirs, DirMap& newShares, int64_t& hashSize, int64_t& addedSize, HashFileMap& tthIndexNew, ShareBloom& aBloom);
	bool checkHidden(const string& aName) const;

	//void rebuildIndices();
	static void updateIndices(Directory::Ptr& aDirectory, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex, DirMultiMap& aDirNames);
	static void updateIndices(Directory& dir, const Directory::File::Set::iterator& i, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex);
	void cleanIndices(Directory& dir);
	void cleanIndices(Directory& dir, const Directory::File::Set::iterator& i);

	void onFileHashed(const string& fname, HashedFilePtr& fileInfo);
	
	StringList bundleDirs;

	void getByVirtual(const string& virtualName, ProfileToken aProfiles, DirectoryList& dirs) const noexcept;
	void getByVirtual(const string& virtualName, const ProfileTokenSet& aProfiles, DirectoryList& dirs) const noexcept;
	//void findVirtuals(const string& virtualPath, ProfileToken aProfiles, DirectoryList& dirs) const;

	template<class T>
	void findVirtuals(const string& virtualPath, const T& aProfile, DirectoryList& dirs) const {

		DirectoryList virtuals; //since we are mapping by realpath, we can have more than 1 same virtualnames
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

			if(virtualPath.find('/', j) == string::npos) {	  // we only have root virtualpaths.
				dirs.push_back(d);
			} else {
				while((i = virtualPath.find('/', j)) != string::npos) {
					if(d) {
						auto mi = d->directories.find(Text::toLower(virtualPath.substr(j, i - j)));
						j = i + 1;
						if(mi != d->directories.end() && !(*mi)->isLevelExcluded(aProfile)) {   //if we found something, look for more.
							d = *mi;
						} else {
							d = nullptr;   //make the pointer null so we can check if found something or not.
							break;
						}
					}
				}

				if(d) 
					dirs.push_back(d);
			}
		}

		if(dirs.empty()) {
			//if we are here it means we didnt find anything, throw.
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}
	}

	string findRealRoot(const string& virtualRoot, const string& virtualLeaf) const;

	Directory::Ptr findDirectory(const string& fname, bool allowAdd, bool report, bool checkExcludes=true);

	virtual int run();

	void runTasks(function<void (float)> progressF = nullptr);

	// QueueManagerListener
	virtual void on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept;
	virtual void on(QueueManagerListener::BundleHashed, const string& aPath) noexcept;
	virtual void on(QueueManagerListener::FileHashed, const string& aPath, HashedFilePtr& aFileInfo) noexcept { onFileHashed(aPath, aFileInfo); }

	// SettingsManagerListener
	void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
		save(xml);
	}
	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
		load(xml);
	}
	
	// TimerManagerListener
	void on(TimerManagerListener::Minute, uint64_t tick) noexcept;

	void load(SimpleXML& aXml);
	void loadProfile(SimpleXML& aXml, const string& aName, ProfileToken aToken);
	void save(SimpleXML& aXml);

	void reportTaskStatus(uint8_t aTask, const StringList& aDirectories, bool finished, int64_t aHashSize, const string& displayName, RefreshType aRefreshType);
	
	ShareProfileList shareProfiles;

	StringMatch skipList;
	string winDir;
}; //sharemanager end

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
