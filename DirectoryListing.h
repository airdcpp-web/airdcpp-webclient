/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#include "DirectoryListingListener.h"
#include "ClientManagerListener.h"
#include "SearchManagerListener.h"
#include "ShareManagerListener.h"
#include "TimerManager.h"

#include "AirUtil.h"
#include "Bundle.h"
#include "FastAlloc.h"
#include "GetSet.h"
#include "HintedUser.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "QueueItemBase.h"
//#include "QueueManager.h"
#include "TaskQueue.h"
#include "UserInfoBase.h"
#include "SearchResult.h"
#include "ShareManager.h"
#include "Streams.h"
#include "TargetUtil.h"
#include "Thread.h"

namespace dcpp {

class ListLoader;
STANDARD_EXCEPTION(AbortException);

class DirectoryListing : public intrusive_ptr_base<DirectoryListing>, public UserInfoBase, public Thread, public Speaker<DirectoryListingListener>, 
	private SearchManagerListener, private TimerManagerListener, private ClientManagerListener, private ShareManagerListener
{
public:
	class Directory;
	class File {

	public:
		typedef File* Ptr;
		struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };

		typedef std::vector<Ptr> List;
		typedef List::const_iterator Iter;
		
		File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, bool checkDupe, time_t aRemoteDate) noexcept;

		File(const File& rhs, bool _adls = false) : name(rhs.name), size(rhs.size), parent(rhs.parent), tthRoot(rhs.tthRoot), adls(_adls), dupe(rhs.dupe), remoteDate(rhs.remoteDate)
		{
		}

		~File() { }


		string getPath() const noexcept {
			return parent->getPath() + name;
		}

		GETSET(string, name, Name);
		GETSET(int64_t, size, Size);
		GETSET(Directory*, parent, Parent);
		GETSET(TTHValue, tthRoot, TTH);
		GETSET(bool, adls, Adls);
		GETSET(DupeType, dupe, Dupe);
		GETSET(time_t, remoteDate, RemoteDate);
		bool isQueued() const noexcept {
			return (dupe == DUPE_QUEUE || dupe == DUPE_FINISHED);
		}
	};

	class Directory : boost::noncopyable, public intrusive_ptr_base<Directory> {
	public:
		enum DirType {
			TYPE_NORMAL,
			TYPE_INCOMPLETE_CHILD,
			TYPE_INCOMPLETE_NOCHILD,
			TYPE_ADLS,
		};

		//typedef Directory* Ptr;
		typedef boost::intrusive_ptr<Directory> Ptr;

		struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };

		typedef std::vector<Ptr> List;
		typedef List::const_iterator Iter;
		typedef unordered_set<TTHValue> TTHSet;
		
		List directories;
		File::List files;

		Directory(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, bool checkDupe = false, const string& aSize = Util::emptyString, time_t aRemoteDate = 0);

		virtual ~Directory();

		size_t getTotalFileCount(bool countAdls) const noexcept;
		int64_t getTotalSize(bool countAdls) const noexcept;
		void filterList(DirectoryListing& dirList) noexcept;
		void filterList(TTHSet& l) noexcept;
		void getHashList(TTHSet& l) const noexcept;
		void clearAdls() noexcept;
		void clearAll() noexcept;

		bool findIncomplete() const noexcept;
		void search(OrderedStringSet& aResults, SearchQuery& aStrings) const noexcept;
		void findFiles(const boost::regex& aReg, File::List& aResults) const noexcept;
		
		size_t getFileCount() const noexcept { return files.size(); }
		
		int64_t getFilesSize() const noexcept;

		string getPath() const noexcept;
		uint8_t checkShareDupes() noexcept;
		
		GETSET(string, name, Name);
		GETSET(int64_t, partialSize, PartialSize);
		GETSET(Directory*, parent, Parent);
		GETSET(DirType, type, Type);
		GETSET(DupeType, dupe, Dupe);
		GETSET(time_t, remoteDate, RemoteDate);
		GETSET(time_t, updateDate, UpdateDate);
		GETSET(bool, loading, Loading);

		bool isComplete() const noexcept { return type == TYPE_ADLS || type == TYPE_NORMAL; }
		void setComplete() noexcept { type = TYPE_NORMAL; }
		bool getAdls() const noexcept { return type == TYPE_ADLS; }

		void download(const string& aTarget, BundleFileInfo::List& aFiles) noexcept;
	};

	class AdlDirectory : public Directory {
	public:
		AdlDirectory(const string& aFullPath, Directory* aParent, const string& aName) : Directory(aParent, aName, Directory::TYPE_ADLS, GET_TIME()), fullPath(aFullPath) { }

		GETSET(string, fullPath, FullPath);
	};

	DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool isClientView, bool aIsOwnList=false);
	~DirectoryListing();
	
	void loadFile() throw(Exception, AbortException);

	//return the number of loaded dirs
	int updateXML(const string& aXml, const string& aBase);

	//return the number of loaded dirs
	int loadXML(InputStream& xml, bool updating, const string& aBase = "/", time_t aListDate = GET_TIME()) throw(AbortException);

	bool downloadDir(const string& aRemoteDir, const string& aTarget, QueueItemBase::Priority prio = QueueItem::DEFAULT, ProfileToken aAutoSearch = 0);
	bool createBundle(Directory::Ptr& aDir, const string& aTarget, QueueItemBase::Priority prio, ProfileToken aAutoSearch);

	void openFile(const File* aFile, bool isClientView) const throw(/*QueueException,*/ FileException);

	int64_t getTotalListSize(bool adls = false) const noexcept { return root->getTotalSize(adls); }
	int64_t getDirSize(const string& aDir) const noexcept;
	size_t getTotalFileCount(bool adls = false) const noexcept { return root->getTotalFileCount(adls); }

	const Directory::Ptr getRoot() const { return root; }
	Directory::Ptr getRoot() { return root; }
	void getLocalPaths(const Directory::Ptr& d, StringList& ret) const throw(ShareException);
	void getLocalPaths(const File* f, StringList& ret) const throw(ShareException);

	bool isMyCID() const noexcept;
	string getNick(bool firstOnly) const noexcept;
	static string getNickFromFilename(const string& fileName);
	static UserPtr getUserFromFilename(const string& fileName);
	void checkShareDupes() noexcept;
	bool findNfo(const string& aPath) noexcept;
	
	const UserPtr& getUser() const { return hintedUser.user; }
	const HintedUser& getHintedUser() const { return hintedUser; }
	const string& getHubUrl() const { return hintedUser.hint; }	
	void setHubUrl(const string& newUrl, bool isGuiChange) noexcept;
		
	GETSET(bool, partialList, PartialList);
	GETSET(bool, isOwnList, IsOwnList);
	GETSET(bool, isClientView, isClientView);
	GETSET(string, fileName, FileName);
	GETSET(bool, matchADL, MatchADL);
	IGETSET(bool, waiting, Waiting, false);
	IGETSET(bool, closing, Closing, false);

	void addMatchADLTask() noexcept;
	void addListDiffTask(const string& aFile, bool aOwnList) noexcept;
	void addPartialListTask(const string& aXml, const string& aBase, bool reloadAll = false, bool changeDir = true, std::function<void()> f = nullptr) noexcept;
	void addFullListTask(const string& aDir) noexcept;
	void addQueueMatchTask() noexcept;

	void addAsyncTask(std::function<void()> f) noexcept;
	void close() noexcept;

	void addSearchTask(const string& aSearchString, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, const string& aDir) noexcept;
	bool nextResult(bool prev) noexcept;
	unique_ptr<SearchQuery> curSearch;

	bool isCurrentSearchPath(const string& path) const noexcept;
	size_t getResultCount() const { return searchResults.size(); }

	Directory::Ptr findDirectory(const string& aName) const noexcept { return findDirectory(aName, root); }
	Directory::Ptr findDirectory(const string& aName, const Directory::Ptr& current) const noexcept;
	
	bool supportsASCH() const noexcept;

	void onRemovedQueue(const string& aDir) noexcept;

	/* only call from the file list thread*/
	bool downloadDirImpl(Directory::Ptr& aDir, const string& aTarget, QueueItemBase::Priority prio, ProfileToken aAutoSearch);
	void setActive() noexcept;
private:
	friend class ListLoader;

	Directory::Ptr root;

	//maps loaded base dirs with their full lowercase paths and whether they've been visited or not
	typedef unordered_map<string, pair<Directory::Ptr, bool>> DirMap;
	DirMap baseDirs;

	int run();

	enum Tasks {
		ASYNC,
		CLOSE
	};

	void runTasks() noexcept;
	atomic_flag running;

	TaskQueue tasks;

	void on(SearchManagerListener::SR, const SearchResultPtr& aSR) noexcept;
	void on(ClientManagerListener::DirectSearchEnd, const string& aToken, int resultCount) noexcept;

	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;

	// ShareManagerListener
	void on(ShareManagerListener::DirectoriesRefreshed, uint8_t, const StringList& aPaths) noexcept;

	void endSearch(bool timedOut = false) noexcept;

	void changeDir(bool reload = false) noexcept;


	OrderedStringSet searchResults;
	OrderedStringSet::iterator curResult;

	int curResultCount = 0;
	int maxResultCount = 0;
	uint64_t lastResult = 0;
	string searchToken;

	void listDiffImpl(const string& aFile, bool aOwnList) throw(Exception, AbortException);
	void loadFileImpl(const string& aInitialDir) throw(Exception, AbortException);
	void searchImpl(const string& aSearchString, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, const string& aDir);
	void loadPartialImpl(const string& aXml, const string& aBaseDir, bool reloadAll, bool changeDir, std::function<void()> completionF) throw(Exception, AbortException);
	void matchAdlImpl();
	void matchQueueImpl();
	void removedQueueImpl(const string& aDir) noexcept;

	void waitActionFinish() const throw(AbortException);
	HintedUser hintedUser;
};

inline bool operator==(const DirectoryListing::Directory::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }
inline bool operator==(const DirectoryListing::File::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)
