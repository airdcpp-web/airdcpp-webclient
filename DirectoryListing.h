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

#ifndef DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H
#define DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H

#include "forward.h"
#include "noexcept.h"
#include "Thread.h"

#include "HintedUser.h"
#include "FastAlloc.h"
#include "MerkleTree.h"
#include "Streams.h"
#include "QueueItem.h"
#include "UserInfoBase.h"
#include "GetSet.h"
#include "DirectoryListingListener.h"
#include "AirUtil.h"
#include "TaskQueue.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

class ListLoader;
STANDARD_EXCEPTION(AbortException);

class DirectoryListing : public UserInfoBase, public Thread, public Speaker<DirectoryListingListener>
{
public:
	class Directory;
	class File {

	public:
		typedef File* Ptr;
		struct FileSort {
			bool operator()(const Ptr& a, const Ptr& b) const {
				return stricmp(a->getName().c_str(), b->getName().c_str()) < 0;
			}
		};
		typedef std::vector<Ptr> List;
		typedef List::const_iterator Iter;
		
		File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, bool checkDupe) noexcept;

		File(const File& rhs, bool _adls = false) : name(rhs.name), size(rhs.size), parent(rhs.parent), tthRoot(rhs.tthRoot), adls(_adls), dupe(rhs.dupe)
		{
		}

		File& operator=(const File& rhs) {
			name = rhs.name; size = rhs.size; parent = rhs.parent; tthRoot = rhs.tthRoot; dupe = rhs.dupe;
			return *this;
		}

		~File() { }


		string getPath() {
			return getParent()->getPath();
		}

		GETSET(TTHValue, tthRoot, TTH);
		GETSET(string, name, Name);
		GETSET(int64_t, size, Size);
		GETSET(Directory*, parent, Parent);
		GETSET(bool, adls, Adls);
		GETSET(DupeType, dupe, Dupe)
		bool isQueued() {
			return (dupe == QUEUE_DUPE || dupe == FINISHED_DUPE);
		}
	};

	class Directory :  boost::noncopyable {
	public:
		typedef Directory* Ptr;
		struct DirSort {
			bool operator()(const Ptr& a, const Ptr& b) const {
				return stricmp(a->getName().c_str(), b->getName().c_str()) < 0;
			}
		};
		typedef std::vector<Ptr> List;
		typedef List::const_iterator Iter;
		typedef unordered_set<TTHValue> TTHSet;
		typedef boost::unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> DirMap;
		
		List directories;
		File::List files;
		DirMap visitedDirs;

		Directory(Directory* aParent, const string& aName, bool _adls, bool aComplete, bool checkDupe = false, const string& aSize = Util::emptyString, const string& aDate = Util::emptyString);
		void setDate(const string& aDate);
		time_t getDate() { return date; }

		virtual ~Directory();

		size_t getTotalFileCount(bool adls = false);		
		int64_t getTotalSize(bool adls = false);
		void filterList(DirectoryListing& dirList);
		void filterList(TTHSet& l);
		void getHashList(TTHSet& l);
		void clearAdls();

		bool findIncomplete();
		
		size_t getFileCount() { return files.size(); }
		
		int64_t getSize() {
			int64_t x = 0;
			for(auto i = files.begin(); i != files.end(); ++i) {
				x+=(*i)->getSize();
			}
			return x;
		}

		string getPath() {
			string tmp;
			//make sure to not try and get the name of the root dir
			if(getParent() && getParent()->getParent()){
				return getParent()->getPath() +  getName() + '\\';
		}
			return getName() + '\\';
		}
		uint8_t checkShareDupes();
		
		GETSET(string, name, Name);
		GETSET(int64_t, size, Size);
		GETSET(Directory*, parent, Parent);		
		GETSET(bool, adls, Adls);		
		GETSET(bool, complete, Complete);
		GETSET(DupeType, dupe, Dupe)
	private:
		time_t date;
	};

	class AdlDirectory : public Directory {
	public:
		AdlDirectory(const string& aFullPath, Directory* aParent, const string& aName) : Directory(aParent, aName, true, true), fullPath(aFullPath) { }

		GETSET(string, fullPath, FullPath);
	};

	DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool isClientView, int64_t aSpeed=0, bool aIsOwnList=false);
	~DirectoryListing();
	
	void loadFile(const string& name);


	string updateXML(const std::string&);
	string loadXML(InputStream& xml, bool updating);

	void download(const string& aDir, const string& aTarget, bool highPrio, QueueItem::Priority prio = QueueItem::DEFAULT, bool recursiveList = false);
	void download(Directory* aDir, const string& aTarget, bool highPrio, QueueItem::Priority prio=QueueItem::DEFAULT, bool recursiveList=false, bool first=true, BundlePtr aBundle=NULL);
	void download(File* aFile, const string& aTarget, bool view, bool highPrio, QueueItem::Priority prio = QueueItem::DEFAULT, BundlePtr aBundle=NULL);

	string getPath(const Directory* d) const;
	string getPath(const File* f) const { return getPath(f->getParent()); }

	int64_t getTotalSize(bool adls = false) { return root->getTotalSize(adls); }
	size_t getTotalFileCount(bool adls = false) { return root->getTotalFileCount(adls); }

	const Directory* getRoot() const { return root; }
	Directory* getRoot() { return root; }
	void getLocalPaths(const Directory* d, StringList& ret);
	void getLocalPaths(const File* f, StringList& ret);

	static UserPtr getUserFromFilename(const string& fileName);
	void checkShareDupes();
	void findNfo(const string& aPath);
	
	const UserPtr& getUser() const { return hintedUser.user; }	
	const string& getHubUrl() const { return hintedUser.hint; }	
		
	GETSET(HintedUser, hintedUser, HintedUser);
	GETSET(bool, partialList, PartialList);
	GETSET(bool, abort, Abort);
	GETSET(bool, isOwnList, IsOwnList);
	GETSET(bool, isClientView, isClientView);
	GETSET(string, fileName, FileName);
	GETSET(int64_t, speed, speed); /**< Speed at which this file list was downloaded */

	void addMatchADLTask();
	void addListDiffTask(const string& aFile);
	void addPartialListTask(const string& aXmlDir);
	void addFullListTask(const string& aDir);
	void addQueueMatchTask();
	void close();
private:
	friend class ListLoader;

	Directory* root;
		
	Directory* find(const string& aName, Directory* current);

	int run();

	enum Tasks {
		MATCH_QUEUE,
		CLOSE,
		REFRESH_DIR,
		LOAD_FILE,
		MATCH_ADL,
		LISTDIFF
	};

	void runTasks();
	atomic_flag running;

	TaskQueue tasks;
};

inline bool operator==(DirectoryListing::Directory::Ptr a, const string& b) { return stricmp(a->getName(), b) == 0; }
inline bool operator==(DirectoryListing::File::Ptr a, const string& b) { return stricmp(a->getName(), b) == 0; }

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)

/**
 * @file
 * $Id: DirectoryListing.h 491 2010-03-20 11:32:35Z bigmuscle $
 */
