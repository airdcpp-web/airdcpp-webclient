/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#include "Exception.h"
#include "Thread.h"
#include "StringSearch.h"
#include "Singleton.h"
#include "BloomFilter.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "LogManager.h"
#include "pme.h"
#include "AirUtil.h"
#include "ShareProfile.h"
#include "Flags.h"

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
class Worker;

struct ShareDirInfo {
	ShareDirInfo(const string& aVname, const string& aProfile, const string& aPath, bool aIncoming=false) : vname(aVname), profile(aProfile), path(aPath), incoming(aIncoming), found(false) { }
	string vname;
	string profile;
	string path;
	bool incoming;
	bool found; //used when detecting removed dirs with using dir tree

	bool operator==(const ShareDirInfo& rhs) const {
		return rhs.path == path && compare(rhs.profile, profile) == 0;
	}

	struct Hash {
		size_t operator()(const ShareDirInfo& x) const { return hash<string>()(x.path + x.profile); }
	};
	typedef unordered_set<ShareDirInfo, Hash> set;
	typedef vector<ShareDirInfo> list;
	typedef unordered_map<string, list> map;
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

	void validatePath(const string& realPath, const string& virtualName);

	string toVirtual(const TTHValue& tth, const string& shareProfile) const;
	pair<string, int64_t> toRealWithSize(const string& virtualFile, const string& aProfile);
	pair<string, int64_t> toRealWithSize(const string& virtualFile, const StringSet& aProfiles, const HintedUser& aUser);
	TTHValue getListTTH(const string& virtualFile, const string& aProfile) const;
	
	int refresh(bool incoming=false, bool isStartup=false);
	int initTaskThread(bool isStartup=false) noexcept;
	int refresh(const string& aDir);

	bool isRefreshing() {	return refreshRunning; }
	
	//need to be called from inside a lock.
	void setDirty(bool force = false);
	
	void setDirty(const string& aProfile);
	void save() { 
		w.join();
		//LogManager::getInstance()->message("Creating share cache...");
		w.start();
	}

	void startup();
	void shutdown();

	void changeExcludedDirs(const StringSetMap& aAdd, const StringSetMap& aRemove);
	void rebuildExcludeTypes();

	void search(SearchResultList& l, const string& aString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults) noexcept;
	void search(SearchResultList& l, const StringList& params, StringList::size_type maxResults, const string& aProfile, const CID& cid) noexcept;
	bool isDirShared(const string& aDir) const;
	uint8_t isDirShared(const string& aPath, uint64_t aSize) const;
	bool isFileShared(const TTHValue aTTH, const string& fileName) const;
	bool allowAddDir(const string& dir);
	string getReleaseDir(const string& aName);
	tstring getDirPath(const string& directory);
	string getBloomStats();

	bool loadCache();

	vector<pair<string, StringList>> getGroupedDirectories() const noexcept;
	static bool checkType(const string& aString, int aType);
	MemoryInputStream* generatePartialList(const string& dir, bool recurse, const string& aProfile);
	MemoryInputStream* generateTTHList(const string& dir, bool recurse, const string& aProfile);
	MemoryInputStream* getTree(const string& virtualFile, const string& aProfile) const;

	AdcCommand getFileInfo(const string& aFile, const string& aProfile);

	int64_t getTotalShareSize(const string& aProfile) const noexcept;
	int64_t getShareSize(const string& realPath, const string& aProfile) const noexcept;
	void getProfileInfo(const string& aProfile, int64_t& size, size_t& files) const;
	
	void getBloom(ByteVector& v, size_t k, size_t m, size_t h) const;

	SearchManager::TypeModes getType(const string& fileName) noexcept;

	string validateVirtual(const string& /*aVirt*/) const noexcept;
	void addHits(uint32_t aHits) {
		hits += aHits;
	}

	string generateOwnList(const string& aProfile);

	bool isTTHShared(const TTHValue& tth);

	void getRealPaths(const string& path, StringList& ret, const string& aProfile);

	//void LockRead() noexcept { cs.lock_shared(); }
	//void unLockRead() noexcept { cs.unlock_shared(); }

	string getRealPath(const TTHValue& root);

	enum { 
		REFRESH_STARTED = 0,
		REFRESH_PATH_NOT_FOUND = 1,
		REFRESH_IN_PROGRESS = 2
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
	bool addTempShare(const string& aKey, TTHValue& tth, const string& filePath, int64_t aSize, bool adcHub);
	bool hasTempShares() { Lock l(tScs); return !tempShares.empty(); }
	TempShareMap getTempShares() { Lock l(tScs); return tempShares; }
	void removeTempShare(const string& aKey, TTHValue& tth);
	TempShareInfo findTempShare(const string& aKey, const string& virtualFile);
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

	ShareProfilePtr getShareProfile(const string& aProfile, bool allowFallback=false);
	void getParentPaths(StringList& aDirs) const;

	void addDirectories(const ShareDirInfo::list& aNewDirs);
	void removeDirectories(const ShareDirInfo::list& removeDirs);
	void changeDirectories(const ShareDirInfo::list& renameDirs);

	void addProfiles(const ShareProfile::set& aProfiles);
	void removeProfiles(const StringList& aProfiles);
	ShareProfileList& getProfiles() { return shareProfiles; }

	void getExcludes(const string& aProfile, StringList& excludes);
private:
	class ProfileDirectory : public intrusive_ptr_base<ProfileDirectory>, boost::noncopyable, public Flags {
		public:
			typedef boost::intrusive_ptr<ProfileDirectory> Ptr;

			ProfileDirectory(const string& aRootPath, const string& aVname, const string& aShareProfile);
			ProfileDirectory(const string& aRootPath, const string& aShareProfile);

			GETSET(string, path, Path);
			GETSET(StringMap, shareProfiles, ShareProfiles);
			GETSET(StringSet, excludedProfiles, excludedProfiles);

			~ProfileDirectory() { }

			enum InfoFlags {
				FLAG_ROOT				= 0x01,
				FLAG_EXCLUDE_TOTAL		= 0x02,
				FLAG_EXCLUDE_PROFILE	= 0x04,
				FLAG_INCOMING			= 0x08
			};

			bool hasExcludes() { return !excludedProfiles.empty(); }
			bool hasRoots() { return !shareProfiles.empty(); }

			bool hasProfile(const string& aProfile);
			bool isExcluded(const string& aProfile);
			bool hasProfile(const StringSet& aProfiles);
			void addRootProfile(const string& aName, const string& aProfile);
			void addExclude(const string& aProfile);
			bool removeRootProfile(const string& aProfile);
			string getName(const string& aProfile);
	};

	struct AdcSearch;
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
				return getParent() == rhs.getParent() && (stricmp(getName(), rhs.getName()) == 0);
			}
		
			string getADCPath(const string& aProfile) const { return parent->getADCPath(aProfile) + name; }
			string getFullName(const string& aProfile) const { return parent->getFullName(aProfile) + name; }
			string getRealPath(bool validate = true) const { return parent->getRealPath(name, validate); }

			GETSET(TTHValue, tth, TTH);
			GETSET(string, name, Name);
			GETSET(int64_t, size, Size);
			GETSET(Directory*, parent, Parent);
		};

		Map directories;
		File::Set files;

		static Ptr create(const string& aName, const Ptr& aParent, uint32_t&& aLastWrite, ProfileDirectory::Ptr aRoot = nullptr) {
			auto Ptr(new Directory(aName, aParent, aLastWrite, aRoot));
			if (aParent)
				aParent->directories[aName] = Ptr;
			return Ptr;
		}

		bool hasType(uint32_t type) const noexcept {
			return ( (type == SearchManager::TYPE_ANY) || (fileTypes & (1 << type)) );
		}
		void addType(uint32_t type) noexcept;

		string getADCPath(const string& aProfile) const noexcept;
		string getVirtualName(const string& aProfile) const noexcept;
		string getRealName() { return realName; }
		string getFullName(const string& aProfile) const noexcept; 
		string getRealPath(bool checkExistance) const { return getRealPath(Util::emptyString, checkExistance); };

		bool hasProfile(const StringSet& aProfiles);
		bool hasProfile(const string& aProfiles);

		int64_t getSize(const string& aProfile) const noexcept;
		void getProfileInfo(const string& aProfile, int64_t& totalSize, size_t& filesCount) const;

		void search(SearchResultList& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults) const noexcept;
		void search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults, const string& aProfile) const noexcept;

		void toXml(SimpleXML& aXml, bool fullList, const string& aProfile);
		void toTTHList(OutputStream& tthList, string& tmp2, bool recursive);
		void filesToXml(SimpleXML& aXml) const;
		//for filelist caching
		void toXmlList(OutputStream& xmlFile, const string& path, string& indent);

		File::Set::const_iterator findFile(const string& aFile) const { return find_if(files.begin(), files.end(), Directory::File::StringComp(aFile)); }

		GETSET(uint32_t, lastWrite, LastWrite);
		GETSET(Directory*, parent, Parent);
		GETSET(ProfileDirectory::Ptr, profileDir, ProfileDir);

		Directory(const string& aRealName, const Ptr& aParent, uint32_t aLastWrite, ProfileDirectory::Ptr root = nullptr);
		~Directory() { }

		bool isRootLevel(const string& aProfile);
		bool isLevelExcluded(const string& aProfile);
		int64_t size;
	private:
		friend void intrusive_ptr_release(intrusive_ptr_base<Directory>*);
		/** Set of flags that say which SearchManager::TYPE_* a directory contains */
		uint32_t fileTypes;
		string getRealPath(const string& path, bool checkExistance) const;
		string realName;
	};

	struct AdcSearch {
		AdcSearch(const StringList& params);

		bool isExcluded(const string& str);
		bool hasExt(const string& name);

		StringSearch::List* include;
		StringSearch::List includeX;
		StringSearch::List exclude;
		StringList ext;
		StringList noExt;

		int64_t gt;
		int64_t lt;

		TTHValue root;
		bool hasRoot;

		bool isDirectory;
	};

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

	/*struct StringTask : public TaskData {
		StringTask(const string& s_) : st(s_) { }
		string st;
	};*/

	struct StringListTask : public TaskData {
		StringListTask(const StringList& spl_) : spl(spl_) { }
		StringList spl;
	};

	deque<pair<Tasks, unique_ptr<TaskData> > > tasks;

	FileList* generateXmlList(const string& shareProfile, bool forced = false);
	void createFileList(const string& shareProfile, FileList* fl, bool forced);
	FileList* getFileList(const string& shareProfile) const;

	void saveXmlList(bool verbose = false);	//for filelist caching

	bool ShareCacheDirty;
	bool aShutdown;

	boost::regex subDirRegPlain;
	PME RAR_regexp;
	
	atomic_flag refreshing;
	atomic_flag GeneratingFULLXmlList;
	bool refreshRunning;

	uint64_t lastFullUpdate;
	uint64_t lastIncomingUpdate;
	uint64_t lastSave;
	uint32_t findLastWrite(const string& aName) const;
	
	//caching the share size so we dont need to loop tthindex everytime
	bool xml_saving;

	mutable SharedMutex cs;  // NON-recursive mutex BE Aware!!
	mutable SharedMutex dirNames; // Bundledirs, releasedirs and excluded dirs

	int allSearches, stoppedSearches;
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
	DirMultiMap shareDirs;

	void buildTree(const string& aPath, const Directory::Ptr& aDir, bool checkQueued, const ProfileDirMap& aSubRoots, DirMultiMap& aDirs, DirMap& newShares);
	bool checkHidden(const string& aName) const;

	void rebuildIndices();
	void updateIndices(Directory& aDirectory, bool first=true);
	void updateIndices(Directory& dir, const Directory::File::Set::iterator& i);
	void cleanIndices(Directory::Ptr& dir);

	void onFileHashed(const string& fname, const TTHValue& root);
	
	StringList bundleDirs;

	void getByVirtual(const string& virtualName, const string& aProfiles, DirectoryList& dirs) const noexcept;
	void findVirtuals(const string& virtualPath, const string& aProfiles, DirectoryList& dirs) const;
	string findRealRoot(const string& virtualRoot, const string& virtualLeaf) const;

	Directory::Ptr findDirectory(const string& fname, bool allowAdd, bool report);

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
	void loadProfile(SimpleXML& aXml, const string& aName, const string& aToken);
	void save(SimpleXML& aXml);
	
	ShareProfileList shareProfiles;

	/*This will only be used by the big sharing people probobly*/
	class Worker: public Thread {
	public:
		Worker() { }
		~Worker() {}

	private:
		int run() {
			ShareManager::getInstance()->saveXmlList(true);
			return 0;
		}
	};//worker end

	friend class Worker;
	Worker w;

}; //sharemanager end

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
