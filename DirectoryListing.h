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

#include "Bundle.h"
#include "DirectoryListingListener.h"

#include "ClientManagerListener.h"
#include "SearchManagerListener.h"
#include "TimerManager.h"

#include "HintedUser.h"
#include "FastAlloc.h"
#include "MerkleTree.h"
#include "Streams.h"
#include "QueueItemBase.h"
#include "UserInfoBase.h"
#include "GetSet.h"
#include "AirUtil.h"
#include "TaskQueue.h"
#include "SearchResult.h"
#include "TargetUtil.h"
#include "Pointer.h"

namespace dcpp {

class ListLoader;
STANDARD_EXCEPTION(AbortException);

class DirectoryListing : public intrusive_ptr_base<DirectoryListing>, public UserInfoBase, public Thread, public Speaker<DirectoryListingListener>, private SearchManagerListener, private TimerManagerListener, private ClientManagerListener
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
		
		File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, bool checkDupe, time_t aRemoteDate) noexcept;

		File(const File& rhs, bool _adls = false) : name(rhs.name), size(rhs.size), parent(rhs.parent), tthRoot(rhs.tthRoot), adls(_adls), dupe(rhs.dupe), remoteDate(rhs.remoteDate)
		{
		}

		~File() { }


		string getPath() const {
			return parent->getPath() + name;
		}

		GETSET(TTHValue, tthRoot, TTH);
		GETSET(string, name, Name);
		GETSET(int64_t, size, Size);
		GETSET(Directory*, parent, Parent);
		GETSET(bool, adls, Adls);
		GETSET(DupeType, dupe, Dupe);
		GETSET(time_t, remoteDate, RemoteDate);
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

		Directory(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, bool checkDupe = false, const string& aSize = Util::emptyString, time_t aRemoteDate = 0);

		virtual ~Directory();

		size_t getTotalFileCount(bool countAdls) const;		
		int64_t getTotalSize(bool countAdls) const;
		void filterList(DirectoryListing& dirList);
		void filterList(TTHSet& l);
		void getHashList(TTHSet& l);
		void clearAdls();
		void clearAll();

		bool findIncomplete() const;
		void search(OrderedStringSet& aResults, AdcSearch& aStrings, StringList::size_type maxResults);
		void findFiles(const boost::regex& aReg, File::List& aResults) const;
		
		size_t getFileCount() const { return files.size(); }
		
		int64_t getFilesSize() const;

		string getPath() const;
		uint8_t checkShareDupes();
		
		GETSET(string, name, Name);
		GETSET(int64_t, partialSize, PartialSize);
		GETSET(Directory*, parent, Parent);
		GETSET(DirType, type, Type);
		GETSET(DupeType, dupe, Dupe);
		GETSET(time_t, remoteDate, RemoteDate);
		GETSET(time_t, updateDate, UpdateDate);
		GETSET(bool, loading, Loading);

		bool isComplete() const { return type == TYPE_ADLS || type == TYPE_NORMAL; }
		void setComplete() { type = TYPE_NORMAL; }
		bool getAdls() const { return type == TYPE_ADLS; }

		void download(const string& aTarget, BundleFileList& aFiles);
	};

	class AdlDirectory : public Directory {
	public:
		AdlDirectory(const string& aFullPath, Directory* aParent, const string& aName) : Directory(aParent, aName, Directory::TYPE_ADLS, GET_TIME()), fullPath(aFullPath) { }

		GETSET(string, fullPath, FullPath);
	};

	DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool isClientView, bool aIsOwnList=false);
	~DirectoryListing();
	
	void loadFile();

	//return the number of loaded dirs
	int updateXML(const string& aXml, const string& aBase);

	//return the number of loaded dirs
	int loadXML(InputStream& xml, bool updating, const string& aBase = "/", time_t aListDate = GET_TIME());

	bool downloadDir(const string& aRemoteDir, const string& aTarget, QueueItemBase::Priority prio = QueueItem::DEFAULT, ProfileToken aAutoSearch = 0);
	bool createBundle(Directory* aDir, const string& aTarget, QueueItemBase::Priority prio, ProfileToken aAutoSearch);

	void openFile(File* aFile, bool isClientView) const;

	int64_t getTotalListSize(bool adls = false) const { return root->getTotalSize(adls); }
	int64_t getDirSize(const string& aDir) const;
	size_t getTotalFileCount(bool adls = false) const { return root->getTotalFileCount(adls); }

	const Directory* getRoot() const { return root; }
	Directory* getRoot() { return root; }
	void getLocalPaths(const Directory* d, StringList& ret) const;
	void getLocalPaths(const File* f, StringList& ret) const;

	string getNick(bool firstOnly) const;
	static string getNickFromFilename(const string& fileName);
	static UserPtr getUserFromFilename(const string& fileName);
	void checkShareDupes();
	bool findNfo(const string& aPath);
	
	const UserPtr& getUser() const { return hintedUser.user; }
	const HintedUser& getHintedUser() const { return hintedUser; }
	const string& getHubUrl() const { return hintedUser.hint; }	
	void setHubUrl(const string& newUrl);
		
	GETSET(bool, partialList, PartialList);
	GETSET(bool, abort, Abort);
	GETSET(bool, isOwnList, IsOwnList);
	GETSET(bool, isClientView, isClientView);
	GETSET(string, fileName, FileName);
	GETSET(bool, matchADL, MatchADL);
	GETSET(bool, waiting, Waiting);	

	void addMatchADLTask();
	void addListDiffTask(const string& aFile, bool aOwnList);
	void addPartialListTask(const string& aXml, const string& aBase, bool reloadAll = false, bool changeDir = true, std::function<void ()> f = nullptr);
	void addFullListTask(const string& aDir);
	void addQueueMatchTask();

	void addAsyncTask(std::function<void ()> f);
	void close();

	void addSearchTask(const string& aSearchString, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, const string& aDir);
	bool nextResult(bool prev);
	unique_ptr<AdcSearch> curSearch;

	bool isCurrentSearchPath(const string& path) const;
	size_t getResultCount() const { return searchResults.size(); }

	Directory* findDirectory(const string& aName) const { return findDirectory(aName, root); }
	Directory* findDirectory(const string& aName, const Directory* current) const;
	
	bool supportsASCH() const;

	void onRemovedQueue(const string& aDir);

	/* only call from the file list thread*/
	bool downloadDirImpl(Directory* aDir, const string& aTarget, QueueItemBase::Priority prio, ProfileToken aAutoSearch);
	void setActive();
private:
	friend class ListLoader;

	Directory* root;

	//maps loaded base dirs with their full lowercase paths and whether they've been visited or not
	typedef unordered_map<string, pair<Directory::Ptr, bool>> DirMap;
	DirMap baseDirs;

	int run();

	enum Tasks {
		ASYNC,
		CLOSE
	};

	void runTasks();
	atomic_flag running;

	TaskQueue tasks;

	void on(SearchManagerListener::SR, const SearchResultPtr& aSR) noexcept;
	void on(ClientManagerListener::DirectSearchEnd, const string& aToken, int resultCount) noexcept;

	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;

	void endSearch(bool timedOut=false);

	void changeDir(bool reload=false);


	OrderedStringSet searchResults;
	OrderedStringSet::iterator curResult;

	int curResultCount;
	int maxResultCount;
	uint64_t lastResult;
	string searchToken;

	void listDiffImpl(const string& aFile, bool aOwnList);
	void loadFileImpl(const string& aInitialDir);
	void searchImpl(const string& aSearchString, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, const string& aDir);
	void loadPartialImpl(const string& aXml, const string& aBaseDir, bool reloadAll, bool changeDir, std::function<void ()> completionF);
	void matchAdlImpl();
	void matchQueueImpl();
	void removedQueueImpl(const string& aDir);

	void waitActionFinish() const;
	HintedUser hintedUser;
};

inline bool operator==(DirectoryListing::Directory::Ptr a, const string& b) { return stricmp(a->getName(), b) == 0; }
inline bool operator==(DirectoryListing::File::Ptr a, const string& b) { return stricmp(a->getName(), b) == 0; }

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)