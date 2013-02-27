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

#ifndef DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H
#define DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H

#include "forward.h"
#include "noexcept.h"
#include "Thread.h"

#include "DirectoryListingListener.h"
#include "SearchManagerListener.h"
#include "TimerManager.h"

#include "HintedUser.h"
#include "FastAlloc.h"
#include "MerkleTree.h"
#include "Streams.h"
#include "QueueItem.h"
#include "UserInfoBase.h"
#include "GetSet.h"
#include "AirUtil.h"
#include "TaskQueue.h"
#include "SearchResult.h"
#include "TargetUtil.h"
#include "Pointer.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

class ListLoader;
STANDARD_EXCEPTION(AbortException);

class DirectoryListing : public intrusive_ptr_base<DirectoryListing>, public UserInfoBase, public Thread, public Speaker<DirectoryListingListener>, private SearchManagerListener, private TimerManagerListener
{
public:
	class Directory;
	class File {

	public:
		typedef File* Ptr;
		struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };
		struct DefaultSort { bool operator()(const Ptr& a, const Ptr& b) const; };

		typedef std::vector<Ptr> List;
		typedef List::const_iterator Iter;
		
		File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, bool checkDupe, time_t aDate) noexcept;

		File(const File& rhs, bool _adls = false) : name(rhs.name), size(rhs.size), parent(rhs.parent), tthRoot(rhs.tthRoot), adls(_adls), dupe(rhs.dupe), date(rhs.date)
		{
		}

		~File() { }


		string getPath() const {
			return getParent()->getPath();
		}

		GETSET(TTHValue, tthRoot, TTH);
		GETSET(string, name, Name);
		GETSET(int64_t, size, Size);
		GETSET(Directory*, parent, Parent);
		GETSET(bool, adls, Adls);
		GETSET(DupeType, dupe, Dupe);
		GETSET(time_t, date, Date);
		bool isQueued() {
			return (dupe == QUEUE_DUPE || dupe == FINISHED_DUPE);
		}
	};

	class Directory :  boost::noncopyable {
	public:
		enum DirType {
			TYPE_NORMAL,
			TYPE_INCOMPLETE_CHILD,
			TYPE_INCOMPLETE_NOCHILD,
			TYPE_ADLS,
		};

		typedef Directory* Ptr;

		struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };
		struct DefaultSort { bool operator()(const Ptr& a, const Ptr& b) const; };

		typedef std::vector<Ptr> List;
		typedef List::const_iterator Iter;
		typedef unordered_set<TTHValue> TTHSet;
		
		List directories;
		File::List files;

		Directory(Directory* aParent, const string& aName, DirType aType, bool checkDupe = false, const string& aSize = Util::emptyString, time_t aDate = 0);

		virtual ~Directory();

		size_t getTotalFileCount(bool countAdls);		
		int64_t getTotalSize(bool countAdls);
		void filterList(DirectoryListing& dirList);
		void filterList(TTHSet& l);
		void getHashList(TTHSet& l);
		void clearAdls();
		void clearAll();
		void sortDirs();
		void sortFiles();

		bool findIncomplete();
		void search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults);
		void findFiles(const boost::regex& aReg, File::List& aResults) const;
		
		size_t getFileCount() { return files.size(); }
		
		int64_t getFilesSize() const;

		string getPath() const;
		uint8_t checkShareDupes();
		
		GETSET(string, name, Name);
		GETSET(int64_t, partialSize, PartialSize);
		GETSET(Directory*, parent, Parent);
		GETSET(DirType, type, Type);
		GETSET(DupeType, dupe, Dupe);
		GETSET(time_t, date, Date);
		GETSET(bool, loading, Loading);

		bool isComplete() const { return type == TYPE_ADLS || type == TYPE_NORMAL; }
		void setComplete() { type = TYPE_NORMAL; }
		bool getAdls() const { return type == TYPE_ADLS; }
	};

	class AdlDirectory : public Directory {
	public:
		AdlDirectory(const string& aFullPath, Directory* aParent, const string& aName) : Directory(aParent, aName, Directory::TYPE_ADLS), fullPath(aFullPath) { }

		GETSET(string, fullPath, FullPath);
	};

	DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool isClientView, bool aIsOwnList=false);
	~DirectoryListing();
	
	void loadFile();

	//return the number of loaded dirs
	int updateXML(const string& aXml, const string& aBase);

	//return the number of loaded dirs
	int loadXML(InputStream& xml, bool updating, const string& aBase = "/");

	bool downloadDir(const string& aDir, const string& aTarget, TargetUtil::TargetType aTargetType, bool highPrio, QueueItemBase::Priority prio = QueueItem::DEFAULT, ProfileToken aAutoSearch = 0);
	bool downloadDir(Directory* aDir, const string& aTarget, TargetUtil::TargetType aTargetType, bool isSizeUnknown, QueueItemBase::Priority prio=QueueItem::DEFAULT, bool first=true, BundlePtr aBundle=nullptr, ProfileToken aAutoSearch=0);
	void download(File* aFile, const string& aTarget, bool view, QueueItemBase::Priority prio = QueueItem::DEFAULT, BundlePtr aBundle=NULL);

	string getPath(const Directory* d) const;
	string getPath(const File* f) const { return getPath(f->getParent()); }

	int64_t getTotalListSize(bool adls = false) { return root->getTotalSize(adls); }
	int64_t getDirSize(const string& aDir);
	size_t getTotalFileCount(bool adls = false) { return root->getTotalFileCount(adls); }

	/** sort directories and sub-directories recursively (case-insensitive). */
	void sortDirs();

	const Directory* getRoot() const { return root; }
	Directory* getRoot() { return root; }
	void getLocalPaths(const Directory* d, StringList& ret);
	void getLocalPaths(const File* f, StringList& ret);

	static UserPtr getUserFromFilename(const string& fileName);
	void checkShareDupes();
	bool findNfo(const string& aPath);
	
	const UserPtr& getUser() const { return hintedUser.user; }	
	const string& getHubUrl() const { return hintedUser.hint; }	
		
	GETSET(HintedUser, hintedUser, HintedUser);
	GETSET(bool, partialList, PartialList);
	GETSET(bool, abort, Abort);
	GETSET(bool, isOwnList, IsOwnList);
	GETSET(bool, isClientView, isClientView);
	GETSET(string, fileName, FileName);
	GETSET(bool, matchADL, MatchADL);
	GETSET(bool, waiting, Waiting);	

	void addMatchADLTask();
	void addListDiffTask(const string& aFile, bool aOwnList);
	void addPartialListTask(const string& aXml, const string& aBase, std::function<void ()> f = nullptr);
	void addFullListTask(const string& aDir);
	void addQueueMatchTask();
	void addFilterTask();
	void addDirDownloadTask(Directory* aDir, const string& aTarget, TargetUtil::TargetType aTargetType, bool isSizeUnknown, QueueItemBase::Priority prio=QueueItem::DEFAULT);

	void close();

	void addSearchTask(const string& aSearchString, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, const string& aDir);
	bool nextResult(bool prev);
	unique_ptr<AdcSearch> curSearch;

	bool isCurrentSearchPath(const string& path);
	size_t getResultCount() { return searchResults.size(); }

	Directory* findDirectory(const string& aName) const { return findDirectory(aName, root); }
	Directory* findDirectory(const string& aName, const Directory* current) const;
private:
	friend class ListLoader;

	Directory* root;

	//maps loaded base dirs with their full lowercase paths and whether they've been visited or not
	typedef boost::unordered_map<string, pair<Directory::Ptr, bool>> DirMap;
	DirMap baseDirs;

	int run();

	enum Tasks {
		SEARCH,
		MATCH_QUEUE,
		CLOSE,
		REFRESH_DIR,
		LOAD_FILE,
		MATCH_ADL,
		LISTDIFF,
		FILTER,
		DIR_DOWNLOAD
	};

	void runTasks();
	atomic_flag running;

	TaskQueue tasks;

	SearchResultList searchResults;
	SearchResultList::iterator curResult;

	void on(SearchManagerListener::SR, const SearchResultPtr& aSR) noexcept;
	void on(SearchManagerListener::DirectSearchEnd, const string& aToken) noexcept;
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;

	int secondsEllapsed;

	void endSearch(bool timedOut=false);

	void changeDir(bool reload=false);
	string searchToken;
	bool typingFilter;
};

inline bool operator==(DirectoryListing::Directory::Ptr a, const string& b) { return stricmp(a->getName(), b) == 0; }
inline bool operator==(DirectoryListing::File::Ptr a, const string& b) { return stricmp(a->getName(), b) == 0; }

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)