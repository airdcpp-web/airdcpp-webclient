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
#include "FastAlloc.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "../client/LogManager.h"
#include "pme.h"

namespace dcpp {

STANDARD_EXCEPTION(ShareException);

class SimpleXML;
class Client;
class File;
class OutputStream;
class MemoryInputStream;
struct ShareLoader;
class Worker;
class ShareManager : public Singleton<ShareManager>, private Thread, private SettingsManagerListener, private TimerManagerListener,
	private HashManagerListener, private QueueManagerListener
{
public:
	/**
	 * @param aDirectory Physical directory location
	 * @param aName Virtual name
	 */
	void addDirectory(const string& realPath, const string &virtualName);
	void removeDirectory(const string& realPath);
	void renameDirectory(const string& realPath, const string& virtualName);


	string toVirtual(const TTHValue& tth) const;
	string toReal(const string& virtualFile, bool isInSharingHub);
	TTHValue getTTH(const string& virtualFile) const;
	
	int refresh(int refreshOptions);
	int startRefresh(int refreshOptions) noexcept;
	int refresh(const string& aDir);
	int refreshDirs( StringList dirs);
	int refreshIncoming();
	void setDirty() {xmlDirty = true;  ShareCacheDirty = true;}
	StringList getIncoming() { return incoming; };
	void setIncoming(const string& realPath) { incoming.push_back(realPath); };
	void DelIncoming() { incoming.clear(); };

	void Rebuild();
	
   void save() { 
		w.join();
		LogManager::getInstance()->message("Creating share cache...");
		w.start();
	}

	void Startup() {
		if(!loadCache())
			refresh(REFRESH_ALL | REFRESH_BLOCKING);
	}

	void shutdown();
	bool shareFolder(const string& path, bool thoroughCheck = false) const;
	int64_t removeExcludeFolder(const string &path, bool returnSize = true);
	int64_t addExcludeFolder(const string &path);

	void search(SearchResultList& l, const string& aString, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) noexcept;
	void search(SearchResultList& l, const StringList& params, StringList::size_type maxResults) noexcept;
	bool isDirShared(const string& directory);
	tstring getDirPath(const string& directory);

	bool loadCache() noexcept;

	StringPairList getDirectories(int refreshOptions) const noexcept;
	static bool checkType(const string& aString, int aType);
	MemoryInputStream* generatePartialList(const string& dir, bool recurse, bool isInSharingHub) const;
	MemoryInputStream* getTree(const string& virtualFile) const;

	AdcCommand getFileInfo(const string& aFile);

	int64_t getShareSize() const noexcept;
	int64_t getShareSize(const string& realPath) const noexcept;

	size_t getSharedFiles() const noexcept;

	string getShareSizeString() const { return Util::toString(getShareSize()); }
	string getShareSizeString(const string& aDir) const { return Util::toString(getShareSize(aDir)); }
	
	void getBloom(ByteVector& v, size_t k, size_t m, size_t h) const;

	SearchManager::TypeModes getType(const string& fileName) const noexcept;

	StringList getVirtualNames();

	string validateVirtual(const string& /*aVirt*/) const noexcept;
	bool hasVirtual(const string& name) const noexcept;

	void addHits(uint32_t aHits) {
		hits += aHits;
	}

	string getOwnListFile() {
		//Directorylisting load thread will generate own list, so dont generate here.
		return getBZXmlFile();
	}

	string generateOwnList() {
	
	if(xmlDirty) 
		generateXmlList(true);

	return getBZXmlFile();
	}
	void generateXmlList(bool forced = false);

	bool isTTHShared(const TTHValue& tth) {
		Lock l(cs);
		return tthIndex.find(tth) != tthIndex.end();
	}

	StringList getRealPaths(const std::string path);


	string getRealPath(const TTHValue& root) {
		string result = "";
		HashFileIter i = tthIndex.find(root);
		if(i != tthIndex.end()) {
			result = i->second->getRealPath();
		}
		return result;
	}
	enum { 
		REFRESH_STARTED,
		REFRESH_PATH_NOT_FOUND,
		REFRESH_IN_PROGRESS
	};
	enum {
		REFRESH_ALL = 0x1,
		REFRESH_DIRECTORY = 0x2,
		REFRESH_BLOCKING = 0x4,
		REFRESH_UPDATE = 0x8
	};


	GETSET(size_t, hits, Hits);
	GETSET(string, bzXmlFile, BZXmlFile);
	GETSET(int64_t, sharedSize, SharedSize);

private:
	struct AdcSearch;
	class Directory :/* public FastAlloc<Directory>,*/ public intrusive_ptr_base<Directory>, boost::noncopyable {
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

			string getADCPath() const { return parent->getADCPath() + name; }
			string getFullName() const { return parent->getFullName() + name; }
			string getRealPath() const { return parent->getRealPath(name); }

			GETSET(TTHValue, tth, TTH);
			GETSET(string, name, Name);
			GETSET(int64_t, size, Size);
			GETSET(Directory*, parent, Parent);
		};

		Map directories;
		File::Set files;
		int64_t size;

		static Ptr create(const string& aName, const Ptr& aParent = Ptr()) { return Ptr(new Directory(aName, aParent)); }

		bool hasType(uint32_t type) const noexcept {
			return ( (type == SearchManager::TYPE_ANY) || (fileTypes & (1 << type)) );
		}
		void addType(uint32_t type) noexcept;

		string getADCPath() const noexcept;
		string getFullName() const noexcept; 
		string getRealPath(const std::string& path) const;

		int64_t getSize() const noexcept;
		int64_t getSize(const string& realpath) const noexcept;
		size_t countFiles() const noexcept; //ApexDC

		void search(SearchResultList& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) const noexcept;
		void search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults) const noexcept;
		void findDirsRE(bool remove);

		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) const;
		void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2) const;
		//for filelist caching
		void toXmlList(OutputStream& xmlFile, string& indent) const;

		File::Set::const_iterator findFile(const string& aFile) const { return find_if(files.begin(), files.end(), Directory::File::StringComp(aFile)); }

		void merge(const Ptr& source);
		string find(const string& dir);

		GETSET(string, lastwrite, LastWrite);
		GETSET(string, name, Name);
		GETSET(Directory*, parent, Parent);
		GETSET(bool, fullyHashed, FullyHashed); //ApexDC
	private:
		friend void intrusive_ptr_release(intrusive_ptr_base<Directory>*);

		Directory(const string& aName, const Ptr& aParent);
		~Directory() { }

		/** Set of flags that say which SearchManager::TYPE_* a directory contains */
		uint32_t fileTypes;

	};

	friend class Directory;
	friend struct ShareLoader;

	friend class Singleton<ShareManager>;
	
	ShareManager();
	
	~ShareManager();
	
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

	int64_t xmlListLen;
	TTHValue xmlRoot;
	int64_t bzXmlListLen;
	TTHValue bzXmlRoot;
	unique_ptr<File> bzXmlRef;

	bool xmlDirty;
	bool ShareCacheDirty;
	bool forceXmlRefresh; /// bypass the 15-minutes guard
	bool rebuild;
	PME releaseReg, subDirReg;
	
	int listN;
	//for filelist caching
	void saveXmlList();


	atomic_flag refreshing;
	atomic_flag GeneratingXmlList;

	uint64_t lastXmlUpdate;
	uint64_t lastFullUpdate;
	uint64_t lastIncomingUpdate;

	mutable CriticalSection cs;

	
	typedef unordered_map<int, string> nameMap;
	nameMap dirNames;


	StringList dirNameList;
	//typedef std::multimap<string, string> DirNameMap;
	//DirNameMap dirNameList;

	void addReleaseDir(const string& aName);
	void deleteReleaseDir(const string& aName);
	string getReleaseDir(const string& aName);
	void sortReleaseList();


	typedef std::vector<Directory::Ptr> DirList;
	DirList directories;

	/** Map real name to virtual name - multiple real names may be mapped to a single virtual one */
	StringMap shares;

	typedef unordered_map<TTHValue, Directory::File::Set::const_iterator> HashFileMap;
	typedef HashFileMap::const_iterator HashFileIter;

	HashFileMap tthIndex;

	BloomFilter<5> bloom;
	
	Directory::File::Set::const_iterator findFile(const string& virtualFile) const;

	Directory::Ptr buildTree(const string& aName, const Directory::Ptr& aParent);
	bool checkHidden(const string& aName) const;

	void rebuildIndices();

	void updateIndices(Directory& aDirectory);
	void updateIndices(Directory& dir, const Directory::File::Set::iterator& i);
	
	Directory::Ptr merge(const Directory::Ptr& directory);
	
	StringList notShared;
	StringList incoming;

	DirList::const_iterator getByVirtual(const string& virtualName) const noexcept;
	pair<Directory::Ptr, string> splitVirtual(const string& virtualPath) const;
	string findRealRoot(const string& virtualRoot, const string& virtualLeaf) const;

	Directory::Ptr getDirectory(const string& fname);

	StringList refreshPaths;
	int refreshOptions;

	int run();

	// QueueManagerListener
	virtual void on(QueueManagerListener::FileMoved, const string& n) noexcept;

	// HashManagerListener
	void on(HashManagerListener::TTHDone, const string& fname, const TTHValue& root) noexcept;

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
	void save(SimpleXML& aXml);
	

/*This will only be used by the big sharing people probobly*/
class Worker: public Thread
{
public:
	Worker() { }
	 ~Worker() {}

private:
		int run() {
			ShareManager::getInstance()->saveXmlList();
			LogManager::getInstance()->message("Share cache Created.");
			return 0;
		}
	};//worker end

friend class Worker;
Worker w;

}; //sharemanager end

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)

/**
 * @file
 * $Id: ShareManager.h 548 2010-09-06 08:54:37Z bigmuscle $
 */
