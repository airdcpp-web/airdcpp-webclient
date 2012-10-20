/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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
#include "SearchManager.h"
#include "SettingsManager.h"
#include "HashManager.h"
#include "QueueManagerListener.h"

#include "TaskQueue.h"
#include "Exception.h"
#include "Thread.h"
#include "StringSearch.h"
#include "Singleton.h"
#include "BloomFilter.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "LogManager.h"
#include "ShareProfile.h"
#include "Flags.h"
#include "AdcSearch.h"
#include "StringMatch.h"
#include "HashBloom.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

STANDARD_EXCEPTION(ShareException);
static string FileListALL = "All";

class SimpleXML;
class Client;
class File;
class OutputStream;
class MemoryInputStream;
struct ShareLoader;
class AdcSearch;
class Worker;
class TaskQueue;

class ShareDirInfo : public FastAlloc<ShareDirInfo> {
public:
	enum State { 
		NORMAL,
		ADDED,
		REMOVED
	};

	ShareDirInfo(const string& aVname, ProfileToken aProfile, const string& aPath, bool aIncoming=false) : vname(aVname), profile(aProfile), path(aPath), incoming(aIncoming), 
		found(false), state(NORMAL), size(0) {}

	~ShareDirInfo() {}

	string vname;
	int profile;
	string path;
	bool incoming;
	bool found; //used when detecting removed dirs with using dir tree
	int64_t size;

	State state;

	bool operator==(const ShareDirInfo* rhs) const {
		return rhs->path == path && compare(rhs->profile, profile) == 0;
	}

	struct Hash {
		size_t operator()(const ShareDirInfo* x) const { return hash<string>()(x->path) + x->profile; }
	};

	typedef unordered_set<ShareDirInfo*, Hash> set;
	typedef vector<ShareDirInfo*> list;
	typedef unordered_map<int, list> map;
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
	string getFileListName(const string& virtualFile, ProfileToken aProfile);
	void toRealWithSize(const string& virtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_);
	TTHValue getListTTH(const string& virtualFile, ProfileToken aProfile) const;
	
	int refresh(bool incoming=false, bool isStartup=false);
	int refresh(const string& aDir);

	bool isRefreshing() {	return refreshRunning; }
	
	//need to be called from inside a lock.
	//void setDirty(bool force = false);

	void setDirty(ProfileTokenSet aProfiles, bool setCacheDirty, bool forceXmlRefresh=false);

	void startup();
	void shutdown();

	void changeExcludedDirs(const ProfileTokenStringList& aAdd, const ProfileTokenStringList& aRemove);
	void rebuildTotalExcludes();

	void search(SearchResultList& l, const string& aString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults) noexcept;
	void search(SearchResultList& l, const StringList& params, StringList::size_type maxResults, ProfileToken aProfile, const CID& cid) noexcept;
	void directSearch(DirectSearchResultList& l, AdcSearch& aStrings, StringList::size_type maxResults, ProfileToken aProfile, const string& aDirectory) noexcept;

	bool isDirShared(const string& aDir) const;
	uint8_t isDirShared(const string& aPath, int64_t aSize) const;
	bool isFileShared(const TTHValue& aTTH, const string& fileName) const;
	bool isFileShared(const string& aFileName, int64_t aSize) const;
	bool allowAddDir(const string& dir);
	tstring getDirPath(const string& directory);

	bool loadCache();

	vector<pair<string, StringList>> getGroupedDirectories() const noexcept;
	static bool checkType(const string& aString, int aType);
	MemoryInputStream* generatePartialList(const string& dir, bool recurse, ProfileToken aProfile);
	MemoryInputStream* generateTTHList(const string& dir, bool recurse, ProfileToken aProfile);
	MemoryInputStream* getTree(const string& virtualFile, ProfileToken aProfile) const;

	void saveXmlList(bool verbose = false);	//for filelist caching

	AdcCommand getFileInfo(const string& aFile, ProfileToken aProfile);

	int64_t getTotalShareSize(ProfileToken aProfile) const noexcept;
	int64_t getShareSize(const string& realPath, ProfileToken aProfile) const noexcept;
	void getProfileInfo(ProfileToken aProfile, int64_t& size, size_t& files) const;
	
	//size_t getSharedFiles(ProfileToken aProfile) const noexcept;
	void getBloom(HashBloom& bloom) const;

	SearchManager::TypeModes getType(const string& fileName) noexcept;

	string validateVirtual(const string& /*aVirt*/) const noexcept;
	void addHits(uint32_t aHits) {
		hits += aHits;
	}

	string generateOwnList(ProfileToken aProfile);

	bool isTTHShared(const TTHValue& tth);

	void getRealPaths(const string& path, StringList& ret, ProfileToken aProfile);

	//void LockRead() noexcept { cs.lock_shared(); }
	//void unLockRead() noexcept { cs.unlock_shared(); }

	string getRealPath(const TTHValue& root);
	string getRealPath(const string& aFileName, int64_t aSize);

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
	CriticalSection tScs;
	bool addTempShare(const string& aKey, const TTHValue& tth, const string& filePath, int64_t aSize, bool adcHub);
	bool hasTempShares() { Lock l(tScs); return !tempShares.empty(); }
	TempShareMap getTempShares() { Lock l(tScs); return tempShares; }
	void removeTempShare(const string& aKey, const TTHValue& tth);
	bool isTempShared(const string& aKey, const TTHValue& tth);
	//tempShares end

	typedef vector<ShareProfilePtr> ShareProfileList;

	void getShares(ShareDirInfo::map& aDirs);

	enum Tasks {
		ADD_DIR,
		REMOVE_DIR,
		REFRESH_ALL,
		REFRESH_DIR,
		REFRESH_STARTUP,
		REFRESH_INCOMING
	};

	ShareProfilePtr getShareProfile(ProfileToken aProfile, bool allowFallback=false);
	void getParentPaths(StringList& aDirs) const;

	void addDirectories(const ShareDirInfo::list& aNewDirs);
	void removeDirectories(const ShareDirInfo::list& removeDirs);
	void changeDirectories(const ShareDirInfo::list& renameDirs);

	void addProfiles(const ShareProfile::set& aProfiles);
	void removeProfiles(ProfileTokenList aProfiles);

	/* Only for gui use purposes, no locking */
	const ShareProfileList& getProfiles() { return shareProfiles; }
	void getExcludes(ProfileToken aProfile, StringSet& excludes);
private:
	class ProfileDirectory : public intrusive_ptr_base<ProfileDirectory>, boost::noncopyable, public Flags {
		public:
			typedef boost::intrusive_ptr<ProfileDirectory> Ptr;

			ProfileDirectory(const string& aRootPath, const string& aVname, ProfileToken aProfile);
			ProfileDirectory(const string& aRootPath, ProfileToken aProfile);

			GETSET(string, path, Path);

			//lists the profiles where this directory is set as root and virtual names
			GETSET(ProfileTokenStringMap, rootProfiles, RootProfiles);
			GETSET(ProfileTokenSet, excludedProfiles, ExcludedProfiles);

			~ProfileDirectory() { }

			enum InfoFlags {
				FLAG_ROOT				= 0x01,
				FLAG_EXCLUDE_TOTAL		= 0x02,
				FLAG_EXCLUDE_PROFILE	= 0x04,
				FLAG_INCOMING			= 0x08
			};

			bool hasExcludes() { return !excludedProfiles.empty(); }
			bool hasRoots() { return !rootProfiles.empty(); }

			bool hasRootProfile(ProfileToken aProfile);
			bool hasRootProfile(const ProfileTokenSet& aProfiles);
			bool isExcluded(ProfileToken aProfile);
			bool isExcluded(const ProfileTokenSet& aProfiles);
			void addRootProfile(const string& aName, ProfileToken aProfile);
			void addExclude(ProfileToken aProfile);
			bool removeRootProfile(ProfileToken aProfile);
			bool removeExcludedProfile(ProfileToken aProfile);
			string getName(ProfileToken aProfile);
	};

	struct FileListDir;
	class Directory : public intrusive_ptr_base<Directory>, boost::noncopyable {
	public:
		typedef boost::intrusive_ptr<Directory> Ptr;
		typedef unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> Map;
		typedef Map::iterator MapIter;

		struct File {
			struct StringComp {
				StringComp(const string& s) : a(s) { }
				bool operator()(const File& b) const { return stricmp(a, b.getName()) == 0; }
				const string& a;
			private:
				StringComp& operator=(const StringComp&);
			};
			struct FileLess {
				bool operator()(const File& a, const File& b) const { return (stricmp(a.getName(), b.getName()) < 0); }
			};
			typedef set<File, FileLess> Set;

			File() : size(0), parent(0) { }
			File(const string& aName, int64_t aSize, Directory::Ptr aParent, const TTHValue& aRoot) : 
				name(aName), tth(aRoot), size(aSize), parent(aParent.get()) { }
			File(const File& rhs) : 
				name(rhs.getName()), tth(rhs.getTTH()), size(rhs.getSize()), parent(rhs.getParent()) { }

			~File() { }

			File& operator=(const File& rhs) {
				name = rhs.name; size = rhs.size; parent = rhs.parent; tth = rhs.tth;
				return *this;
			}

			bool operator==(const File& rhs) const {
				return stricmp(name, rhs.getName()) == 0;
			}
		
			string getADCPath(ProfileToken aProfile) const { return parent->getADCPath(aProfile) + name; }
			string getFullName(ProfileToken aProfile) const { return parent->getFullName(aProfile) + name; }
			string getRealPath(bool validate = true) const { return parent->getRealPath(name, validate); }

			void toXml(OutputStream& xmlFile, string& indent, string& tmp2) const;

			GETSET(TTHValue, tth, TTH);
			GETSET(string, name, Name);
			GETSET(int64_t, size, Size);
			GETSET(Directory*, parent, Parent);
		};

		Map directories;
		File::Set files;

		static Ptr create(const string& aName, const Ptr& aParent, uint32_t&& aLastWrite, ProfileDirectory::Ptr aRoot = nullptr) {
			auto dir = Ptr(new Directory(aName, aParent, aLastWrite, aRoot));
			if (aParent)
				aParent->directories[aName] = dir;
			return dir;
		}

		struct DateCompare {
			bool operator()(const Ptr left, const Ptr right) const {
				return left->getLastWrite() < right->getLastWrite();
			}
		};

		struct HasRootProfile {
			HasRootProfile(ProfileToken aT) : t(aT) { }
			bool operator()(const Ptr d) const {
				return d->getProfileDir()->hasRootProfile(t);
			}
			ProfileToken t;
		private:
			HasRootProfile& operator=(const HasRootProfile&);
		};

		bool hasType(uint32_t type) const noexcept {
			return ( (type == SearchManager::TYPE_ANY) || (fileTypes & (1 << type)) );
		}
		void addType(uint32_t type) noexcept;

		string getADCPath(ProfileToken aProfile) const noexcept;
		string getVirtualName(ProfileToken aProfile) const noexcept;
		string getRealName() { return realName; }
		string getFullName(ProfileToken aProfile) const noexcept; 
		string getRealPath(bool checkExistance) const { return getRealPath(Util::emptyString, checkExistance); };

		bool hasProfile(const ProfileTokenSet& aProfiles);
		bool hasProfile(ProfileToken aProfiles);

		int64_t getSize(ProfileToken aProfile) const noexcept;
		int64_t getTotalSize() const noexcept;
		void getProfileInfo(ProfileToken aProfile, int64_t& totalSize, size_t& filesCount) const;

		void search(SearchResultList& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults) const noexcept;
		void search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults, ProfileToken aProfile) const noexcept;

		void directSearch(DirectSearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults, ProfileToken aProfile) const noexcept;

		void toFileList(FileListDir* aListDir, ProfileToken aProfile, bool isFullList);
		void toXml(SimpleXML& aXml, bool fullList, ProfileToken aProfile);
		void toTTHList(OutputStream& tthList, string& tmp2, bool recursive);
		//for filelist caching
		void toXmlList(OutputStream& xmlFile, const string& path, string& indent);

		File::Set::const_iterator findFile(const string& aFile) const { return find_if(files.begin(), files.end(), Directory::File::StringComp(aFile)); }

		GETSET(uint32_t, lastWrite, LastWrite);
		GETSET(Directory*, parent, Parent);
		GETSET(ProfileDirectory::Ptr, profileDir, ProfileDir);

		Directory(const string& aRealName, const Ptr& aParent, uint32_t aLastWrite, ProfileDirectory::Ptr root = nullptr);
		~Directory() { }

		void copyRootProfiles(ProfileTokenSet& aProfiles);
		bool isRootLevel(ProfileToken aProfile);
		bool isLevelExcluded(ProfileToken aProfile);
		bool isLevelExcluded(const ProfileTokenSet& aProfiles);
		int64_t size;
	private:
		friend void intrusive_ptr_release(intrusive_ptr_base<Directory>*);
		/** Set of flags that say which SearchManager::TYPE_* a directory contains */
		uint32_t fileTypes;
		string getRealPath(const string& path, bool checkExistance) const;
		string realName;
	};

	struct FileListDir {
		typedef unordered_map<string, FileListDir*, noCaseStringHash, noCaseStringEq> List;
		vector<Directory::Ptr> shareDirs;

		FileListDir(const string& aName, int64_t aSize, int aDate);
		~FileListDir();

		string name;
		int64_t size;
		uint32_t date;
		List listDirs;

		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList);
		void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2);
	};

	int addTask(uint8_t aType, StringList& dirs, const string& displayName=Util::emptyString, bool isStartup=false) noexcept;
	void removeDir(Directory::Ptr aDir);
	Directory::Ptr getDirByName(const string& directory) const;

	/* Directory items mapped to realpath*/
	typedef boost::unordered_map<string, Directory::Ptr, noCaseStringHash, noCaseStringEq> DirMap;

	void getParents(DirMap& aDirs) const;
	void addShares(const string& aPath, Directory::Ptr aDir) { shares[aPath] = aDir; }

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

	typedef boost::unordered_map<string, ProfileDirectory::Ptr, noCaseStringHash, noCaseStringEq> ProfileDirMap;
	ProfileDirMap profileDirs;

	ProfileDirMap getSubProfileDirs(const string& aPath);

	TaskQueue tasks;

	FileList* generateXmlList(ProfileToken aProfile, bool forced = false);
	FileList* getFileList(ProfileToken aProfile) const;

	bool ShareCacheDirty;
	bool aShutdown;

	boost::regex rxxReg;
	
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

	BloomFilter<5> bloom;

	/*
	multimap to allow multiple same key values, needed to return from some functions.
	*/
	typedef boost::unordered_multimap<string, Directory::Ptr, noCaseStringHash, noCaseStringEq> DirMultiMap; 

	//list to return multiple directory item pointers
	typedef std::vector<Directory::Ptr> DirectoryList;

	/** Map real name to virtual name - multiple real names may be mapped to a single virtual one */
	DirMap shares;
	DirMultiMap dirNameMap;

	void buildTree(const string& aPath, const Directory::Ptr& aDir, bool checkQueued, const ProfileDirMap& aSubRoots, DirMultiMap& aDirs, DirMap& newShares, int64_t& hashSize);
	bool checkHidden(const string& aName) const;

	void rebuildIndices();
	void updateIndices(Directory& aDirectory);
	void updateIndices(Directory& dir, const Directory::File::Set::iterator& i);
	void cleanIndices(Directory::Ptr& dir);

	void onFileHashed(const string& fname, const TTHValue& root);
	
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
						auto mi = d->directories.find(virtualPath.substr(j, i - j));
						j = i + 1;
						if(mi != d->directories.end() && !mi->second->isLevelExcluded(aProfile)) {   //if we found something, look for more.
							d = mi->second;
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

	// QueueManagerListener
	virtual void on(QueueManagerListener::BundleAdded, const BundlePtr aBundle) noexcept;
	virtual void on(QueueManagerListener::BundleHashed, const string& path) noexcept;
	virtual void on(QueueManagerListener::FileHashed, const string& fname, const TTHValue& root) noexcept { onFileHashed(fname, root); }

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

	void reportTaskStatus(uint8_t aTask, const StringList& aDirectories, bool finished, int64_t aHashSize, const string& displayName);
	
	ShareProfileList shareProfiles;

	StringMatch skipList;
	string winDir;
}; //sharemanager end

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
