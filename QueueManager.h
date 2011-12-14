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

#ifndef DCPLUSPLUS_DCPP_QUEUE_MANAGER_H
#define DCPLUSPLUS_DCPP_QUEUE_MANAGER_H

#include "TimerManager.h"
#include "ClientManager.h"

#include "Exception.h"
#include "User.h"
#include "File.h"
#include "QueueItem.h"
#include "Singleton.h"
#include "DirectoryListing.h"
#include "MerkleTree.h"

#include "QueueManagerListener.h"
#include "SearchManagerListener.h"
#include "ClientManagerListener.h"
#include "DownloadManagerListener.h"
#include "LogManager.h"
#include "pme.h"
#include "HashManager.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

STANDARD_EXCEPTION(QueueException);

class UserConnection;

class ConnectionQueueItem;
class QueueLoader;

class QueueManager : public Singleton<QueueManager>, public Speaker<QueueManagerListener>, private TimerManagerListener, 
	private SearchManagerListener, private ClientManagerListener, private HashManagerListener, private DownloadManagerListener
{
public:
	/** Add a file to the queue. */
	void add(const string& aTarget, int64_t aSize, const TTHValue& root, const HintedUser& aUser,
		Flags::MaskType aFlags = 0, BundlePtr aBundle = NULL, bool addBad = true) throw(QueueException, FileException);
		/** Add a user's filelist to the queue. */
	void addList(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString) throw(QueueException, FileException);
	void addListDir(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString) throw(QueueException, FileException);

	/** Readd a source that was removed */
	void readdQISource(const string& target, const HintedUser& aUser) throw(QueueException);
	void readdBundleSource(BundlePtr aBundle, const HintedUser& aUser) throw(QueueException);
	/** Add a directory to the queue (downloads filelist and matches the directory). */
	void addDirectory(const string& aDir, const HintedUser& aUser, const string& aTarget, 
		QueueItem::Priority p = QueueItem::DEFAULT, bool useFullList = false) noexcept;
	int matchListing(const DirectoryListing& dl, bool partialList) noexcept;
	bool findNfo(const DirectoryListing::Directory* dl, const DirectoryListing& dir) noexcept;

	bool getTTH(const string& name, TTHValue& tth) noexcept;

	void remove(QueueItem* qi) noexcept;
	void remove(const string aTarget) noexcept;
	void removeSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;
	void removeSource(const UserPtr& aUser, Flags::MaskType reason) noexcept;

	void recheck(const string& aTarget);

	void setQIPriority(const string& aTarget, QueueItem::Priority p) noexcept;
	void setQIPriority(QueueItem* qi, QueueItem::Priority p, bool isAP=false, bool isBundleChange=false) noexcept;
	void setQIAutoPriority(const string& aTarget, bool ap, bool isBundleChange=false) noexcept;

	StringList getTargets(const TTHValue& tth);
	const QueueItem::StringMap& lockQueue() noexcept { cs.lock(); return fileQueue.getQueue(); } ;
	void unlockQueue() noexcept { cs.unlock(); }

	QueueItem::SourceList getSources(const QueueItem* qi) const { Lock l(cs); return qi->getSources(); }
	QueueItem::SourceList getBadSources(const QueueItem* qi) const { Lock l(cs); return qi->getBadSources(); }
	size_t getSourcesCount(const QueueItem* qi) const { Lock l(cs); return qi->getSources().size(); }
	vector<Segment> getChunksVisualisation(const QueueItem* qi, int type) const { Lock l(cs); return qi->getChunksVisualisation(type); }

	bool getQueueInfo(const UserPtr& aUser, string& aTarget, int64_t& aSize, int& aFlags, string& bundleToken) noexcept;
	Download* getDownload(UserConnection& aSource, string& aMessage, bool smallSlot) noexcept;
	void putDownload(Download* aDownload, bool finished, bool reportFinish = true) noexcept;
	void setFile(Download* download);
	
	/** @return The highest priority download the user has, PAUSED may also mean no downloads */
	QueueItem::Priority hasDownload(const UserPtr& aUser, bool smallSlot) noexcept;
	
	void loadQueue() noexcept;
	void saveQueue(bool force) noexcept;
	void saveQI(OutputStream &save, QueueItem* qi, string tmp, string b32tmp);

	void noDeleteFileList(const string& path);
	

	bool handlePartialSearch(const UserPtr& aUser, const TTHValue& tth, PartsInfo& _outPartsInfo, string& _bundle, bool& _reply, bool& _add);
	bool handlePartialResult(const HintedUser& aUser, const TTHValue& tth, const QueueItem::PartialSource& partialSource, PartsInfo& outPartialInfo);
	void addBundleTTHList(const HintedUser& aUser, const string& bundle, const TTHValue& tth);
	MemoryInputStream* generateTTHList(const string& bundleToken, bool isInSharingHub);

	//merging, adding, deletion
	bool addBundle(BundlePtr aBundle, bool loading = false);
	BundlePtr getMergeBundle(const string& aTarget);
	int mergeBundle(BundlePtr targetBundle, BundlePtr sourceBundle);
	void mergeFileBundles(BundlePtr aBundle);
	void moveBundle(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished);
	void splitBundle(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished);
	void moveFileBundle(BundlePtr aBundle, const string& aTarget) noexcept;
	BundlePtr createFileBundle(QueueItem* qi);
	bool addBundleItem(QueueItem* qi, BundlePtr aBundle, bool newBundle, bool loading = false);
	void removeBundleItem(QueueItem* qi, bool finished);
	void removeBundle(BundlePtr aBundle, bool finished, bool removeFinished);
	BundlePtr findMergeBundle(QueueItem* qi);
	bool isDirQueued(const string& aDir);
	tstring getDirPath(const string& aDir);
	void getDiskInfo(UIntStringList& dirs);
	uint64_t getDiskInfo(const string& aPath);
	void saveBundle(BundlePtr aBundle);
	void getUnfinishedPaths(StringList& bundles);
	void getForbiddenPaths(StringList& bundles, StringPairList paths);

	BundlePtr getBundle(const string& bundleToken) { Lock l (cs); return findBundle(bundleToken); }
	BundlePtr findBundle(const TTHValue& tth);
	bool checkPBDReply(HintedUser& aUser, const TTHValue& aTTH, string& _bundleToken, bool& _notify, bool& _add, const string& remoteBundle);
	void addFinishedNotify(HintedUser& aUser, const TTHValue& aTTH, const string& remoteBundle);
	void updatePBD(const HintedUser& aUser, const TTHValue& aTTH);
	void removeBundleNotify(const UserPtr& aUser, const string& bundleToken);
	void sendRemovePBD(const UserPtr& aUser, BundlePtr aBundle);
	void setBundlePriority(const string& bundleToken, Bundle::Priority p) noexcept;
	void setBundlePriority(BundlePtr aBundle, Bundle::Priority p, bool isAuto=false, bool isQIChange=false) noexcept;
	void setBundleAutoPriority(const string& bundleToken, bool isQIChange=false) noexcept;
	void getBundleSources(BundlePtr aBundle, Bundle::SourceIntList& sources, Bundle::SourceIntList& badSources) noexcept;
	void removeBundleSource(const string& bundleToken, const UserPtr& aUser) noexcept;
	void removeBundleSource(BundlePtr aBundle, const UserPtr& aUser) noexcept;
	void removeBundleSources(BundlePtr aBundle) noexcept;
	BundleList getBundleInfo(const string& aSource, int& finishedFiles, int& dirBundles, int& fileBundles);
	void handleBundleUpdate(const string& bundleToken);

	void moveDir(const string& aSource, const string& aTarget, BundleList sourceBundles, bool moveFinished);
	void removeDir(const string& aSource, BundleList sourceBundles, bool removeFinished);
	bool move(QueueItem* qs, const string& aTarget) noexcept;
	string convertMovePath(const string& aSourceCur, const string& aSourceRoot, const string& aTarget);

	void setBundlePriorities(const string& aSource, BundleList sourceBundles, Bundle::Priority p, bool autoPrio=false);
	void calculateBundlePriorities(bool verbose);
	void searchBundle(BundlePtr aBundle, bool newBundle, bool manual);
	BundlePtr findSearchBundle(uint64_t aTick, bool force=false);

	/** Move the target location of a queued item. Running items are silently ignored */
	void move(const StringPairList& sourceTargetList) noexcept;
	int isTTHQueued(const TTHValue& tth) { return fileQueue.isTTHQueued(tth); }
	
	bool dropSource(Download* d);

	const QueueItemList getRunningFiles() noexcept {
		QueueItemList ql;
		for(auto i = fileQueue.getQueue().begin(); i != fileQueue.getQueue().end(); ++i) {
			QueueItem* q = i->second;
			if(q->isRunning()) {
				ql.push_back(q);
			}
		}
		return ql;
	}

	bool getTargetByRoot(const TTHValue& tth, string& target, string& tempTarget) {
		Lock l(cs);
		QueueItemList  ql = fileQueue.find(tth);
	
		if(ql.empty()) return false;

		target = ql.front()->getTarget();
		tempTarget = ql.front()->getTempTarget();
		return true;
	}

	bool isChunkDownloaded(const TTHValue& tth, int64_t startPos, int64_t& bytes, string& tempTarget);
	
	GETSET(uint64_t, lastSave, LastSave);
	GETSET(uint64_t, lastAutoPrio, LastAutoPrio);
	GETSET(string, queueFile, QueueFile);

	enum { MOVER_LIMIT = 10*1024*1024 };
	class FileMover : public Thread {
	public:
		FileMover() : active(false) { }
		virtual ~FileMover() { join(); }

		void moveFile(const string& source, const string& target, BundlePtr aBundle);
		virtual int run();
	private:
		typedef pair<BundlePtr, StringPair> FileBundlePair;
		typedef vector<FileBundlePair> FileList;

		bool active;

		FileList files;
		CriticalSection cs;
	} mover;

	typedef vector<pair<QueueItem::SourceConstIter, const QueueItem*> > PFSSourceList;

	class Rechecker : public Thread {
		struct DummyOutputStream : OutputStream {
			virtual size_t write(const void*, size_t n) throw(Exception) { return n; }
			virtual size_t flush() throw(Exception) { return 0; }
		};

	public:
		explicit Rechecker(QueueManager* qm_) : qm(qm_), active(false) { }
		virtual ~Rechecker() { join(); }

		void add(const string& file);
		virtual int run();

	private:
		QueueManager* qm;
		bool active;

		StringList files;
		CriticalSection cs;
	} rechecker;

	/** All queue items by target */
	class FileQueue {
	public:
		FileQueue() : targetMapInsert(queue.end()), queueSize(0) { }
		~FileQueue();
		void add(QueueItem* qi, bool addFinished, bool addTTH = true);
		QueueItem* add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, QueueItem::Priority p, 
			const string& aTempTarget, time_t aAdded, const TTHValue& root)
			throw(QueueException, FileException);

		QueueItem* find(const string& target);
		void find(QueueItemList& sl, int64_t aSize, const string& ext);
		uint8_t getMaxSegments(int64_t filesize) const;
		void find(StringList& sl, int64_t aSize, const string& ext);
		QueueItemList find(const TTHValue& tth);

		// find some PFS sources to exchange parts info
		void findPFSSources(PFSSourceList&);

		int getRecentSize() { return (int)recentSearchQueue.size(); }
		int getPrioSum(int& prioBundles);
		BundlePtr findRecent(int& recentBundles);
		BundlePtr findAutoSearch(int& prioBundles);
		size_t getSize() { return queue.size(); }
		QueueItem::StringMap& getQueue() { return queue; }
		QueueItem::TTHMap& getTTHIndex() { return tthIndex; }
		void move(QueueItem* qi, const string& aTarget);
		void remove(QueueItem* qi, bool removeTTH);
		void removeTTH(QueueItem* qi);
		int isTTHQueued(const TTHValue& tth);

		uint64_t getTotalQueueSize() { return queueSize; };

		void addSearchPrio(BundlePtr aBundle, Bundle::Priority p);
		void removeSearchPrio(BundlePtr aBundle, Bundle::Priority p);

		void setSearchPriority(BundlePtr aBundle, Bundle::Priority oldPrio, Bundle::Priority newPrio);

	private:
		QueueItem::StringMap queue;
		QueueItem::TTHMap tthIndex;

		uint64_t queueSize;
		QueueItem::StringMap::iterator targetMapInsert;

		/** Bundles by priority (low-highest, for auto search) */
		deque<BundlePtr> prioSearchQueue[Bundle::LAST];
		deque<BundlePtr> recentSearchQueue;
	};

	/** QueueItems by target */
	FileQueue fileQueue;

private:
	/** All queue items indexed by user (this is a cache for the FileQueue really...) */
	class UserQueue {
	public:
		void add(QueueItem* qi, bool newBundle=false);
		void add(QueueItem* qi, const HintedUser& aUser, bool newBundle=false);
		QueueItem* getNext(const UserPtr& aUser, QueueItem::Priority minPrio = QueueItem::LOWEST, int64_t wantedSize = 0, int64_t lastSpeed = 0, bool allowRemove = false, bool smallSlot=false);
		QueueItem* getNextPrioQI(const UserPtr& aUser, int64_t wantedSize = 0, int64_t lastSpeed = 0, bool smallSlot=false, bool listAll=false);
		QueueItem* getNextBundleQI(const UserPtr& aUser, Bundle::Priority minPrio = Bundle::LOWEST, int64_t wantedSize = 0, int64_t lastSpeed = 0, bool smallSlot=false);
		QueueItemList getRunning(const UserPtr& aUser);
		bool addDownload(QueueItem* qi, Download* d);
		void removeDownload(QueueItem* qi, const UserPtr& d, const string& token = Util::emptyString);

		void removeQI(QueueItem* qi, bool removeRunning = true, bool removeBundle=false);
		void removeQI(QueueItem* qi, const UserPtr& aUser, bool removeRunning = true, bool addBad = false, bool removeBundle=false);
		void setQIPriority(QueueItem* qi, QueueItem::Priority p);

		void setBundlePriority(BundlePtr aBundle, Bundle::Priority p);

		boost::unordered_map<UserPtr, BundleList, User::Hash>& getBundleList()  { return userBundleQueue; }
		boost::unordered_map<UserPtr, QueueItemList, User::Hash>& getPrioList()  { return userPrioQueue; }
		boost::unordered_map<UserPtr, QueueItemList, User::Hash>& getRunning()  { return running; }

		string getLastError() { 
			string tmp = lastError;
			lastError = Util::emptyString;
			return tmp;
		}

	private:
		/** Bundles by priority and user (this is where the download order is determined) */
		boost::unordered_map<UserPtr, BundleList, User::Hash> userBundleQueue;
		/** High priority QueueItems by user (this is where the download order is determined) */
		boost::unordered_map<UserPtr, QueueItemList, User::Hash> userPrioQueue;
		/** Currently running downloads, a QueueItem is always either here or in the userQueue */
		boost::unordered_map<UserPtr, QueueItemList, User::Hash> running;
		/** Last error message to sent to TransferView */
		string lastError;
	};

	friend class QueueLoader;
	friend class Singleton<QueueManager>;
	
	QueueManager();
	~QueueManager();
	
	mutable CriticalSection cs;
	
	//using pme for now
	PME regexp;
	bool addAlternates(QueueItem* qi, const HintedUser& aUser);

	//temp stats
	int highestSel, highSel, normalSel, lowSel, calculations;

	/** Bundles */	
	typedef unordered_map<string, string> BundleDirMap;
	/** All bundles */
	Bundle::BundleTokenMap bundles;
	/** ReleaseDirs for bundles */
	BundleDirMap bundleDirs;

	/** QueueItems by user */
	UserQueue userQueue;
	/** Directories queued for downloading */
	unordered_multimap<UserPtr, DirectoryItemPtr, User::Hash> directories;
	/** Recent searches list, to avoid searching for the same thing too often */
	StringList recent;
	/** Next search */
	uint64_t nextSearch;
	/** Next recent search */
	uint64_t nextRecentSearch;
	/** File lists not to delete */
	StringList protectedFileLists;
	/** Sanity check for the target filename */
	static string checkTarget(const string& aTarget, bool checkExsistence, BundlePtr aBundle = NULL) throw(QueueException, FileException);
	/** Add a source to an existing queue item */
	bool addSource(QueueItem* qi, const HintedUser& aUser, Flags::MaskType addBad, bool newBundle=false) throw(QueueException, FileException);
	 
	void processList(const string& name, const HintedUser& user, const string path, int flags);
	void matchTTHList(const string& name, const HintedUser& user, int flags);

	BundlePtr findBundle(const string bundleToken);
	void addBundleUpdate(const string bundleToken, bool finished = false);
	void sendPBD(HintedUser& aUser, const TTHValue& tth, const string& bundleToken);

	void addFinishedTTH(const TTHValue& tth, BundlePtr aBundle, const string& aTarget, time_t aSize, int64_t aFinished);

	typedef vector<pair<string, uint64_t>> bundleTickMap;
	bundleTickMap bundleUpdates;

	void load(const SimpleXML& aXml);
	void moveFile(const string& source, const string& target, BundlePtr aBundle);
	static void moveFile_(const string& source, const string& target, BundlePtr aBundle);
	void moveStuckFile(QueueItem* qi);
	void rechecked(QueueItem* qi);
	void fileEvent(const string& tgt, bool file = false);
	void onFileHashed(const string& fname, const TTHValue& root, bool failed);
	void hashBundle(BundlePtr aBundle);

	void removeSource(QueueItem* qi, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;

	string getListPath(const HintedUser& user);

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	
	// SearchManagerListener
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;
	
	// HashManagerListener
	void on(HashManagerListener::TTHDone, const string& fname, const TTHValue& root) noexcept { onFileHashed(fname, root, false); }
	void on(HashManagerListener::HashFailed, const string& fname) noexcept { onFileHashed(fname, TTHValue(Util::emptyString), true); }

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const UserPtr& aUser) noexcept;
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser) noexcept;

	//DownloadManagerListener
	void on(DownloadManagerListener::BundleTick, const BundleList& tickBundles) noexcept;
};

} // namespace dcpp

#endif // !defined(QUEUE_MANAGER_H)

/**
 * @file
 * $Id: QueueManager.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
