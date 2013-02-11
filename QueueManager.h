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
#include "Socket.h"

#include "AutoSearchManager.h"
#include "QueueManagerListener.h"
#include "SearchManagerListener.h"
#include "ClientManagerListener.h"
#include "DownloadManagerListener.h"
#include "LogManager.h"
#include "HashManager.h"
#include "TargetUtil.h"
#include "StringMatch.h"
#include "TaskQueue.h"

#include "BundleQueue.h"
#include "FileQueue.h"
#include "UserQueue.h"
#include "DelayedEvents.h"
#include "HashBloom.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

STANDARD_EXCEPTION(QueueException);

class UserConnection;

class ConnectionQueueItem;
class QueueLoader;

class QueueManager : public Singleton<QueueManager>, public Speaker<QueueManagerListener>, private TimerManagerListener, 
	private SearchManagerListener, private ClientManagerListener, private HashManagerListener
{
public:
	void getBloom(HashBloom& bloom) const;
	size_t getQueuedFiles() const noexcept;
	bool hasDownloadedBytes(const string& aTarget) throw(QueueException);

	bool allowAdd(const string& aTarget, const TTHValue& root) throw(QueueException, FileException);
	/** Add a file to the queue. */
	void addFile(const string& aTarget, int64_t aSize, const TTHValue& root, const HintedUser& aUser, const string& aRemotePath,
		Flags::MaskType aFlags = 0, bool addBad = true, QueueItemBase::Priority aPrio = QueueItem::DEFAULT, BundlePtr aBundle=nullptr, ProfileToken aAutoSearch = 0) throw(QueueException, FileException);
		/** Add a user's filelist to the queue. */
	void addList(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString) throw(QueueException, FileException);
	void addListDir(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString) throw(QueueException, FileException);

	/** Readd a source that was removed */
	void readdQISource(const string& target, const HintedUser& aUser) throw(QueueException);
	void readdBundleSource(BundlePtr aBundle, const HintedUser& aUser);
	void onUseSeqOrder(BundlePtr& aBundle);

	/** Add a directory to the queue (downloads filelist and matches the directory). */
	void matchListing(const DirectoryListing& dl, int& matches, int& newFiles, BundleList& bundles);

	void removeQI(QueueItemPtr& qi, bool noFiring = false) noexcept;
	void remove(const string aTarget) noexcept;
	void removeSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;
	void removeSource(const UserPtr& aUser, Flags::MaskType reason) noexcept;

	void recheck(const string& aTarget);

	void setQIPriority(const string& aTarget, QueueItemBase::Priority p) noexcept;
	void setQIPriority(QueueItemPtr& qi, QueueItemBase::Priority p, bool isAP=false, bool isBundleChange=false) noexcept;
	void setQIAutoPriority(const string& aTarget, bool ap, bool isBundleChange=false) noexcept;

	StringList getTargets(const TTHValue& tth);
	void readLockedOperation(const function<void (const QueueItem::StringMap&)>& currentQueue);
	//const QueueItem::StringMap& getQueue() noexcept { RLock l (cs); return fileQueue.getQueue(); } ;
	//const QueueItem::StringMap& lockQueue() noexcept { cs.lock(); return fileQueue.getQueue(); } ;
	//void unlockQueue() noexcept { cs.unlock(); }
	void onSlowDisconnect(const string& aToken);
	bool getAutoDrop(const string& aToken);

	string getTempTarget(const string& aTarget);
	void setSegments(const string& aTarget, uint8_t aSegments);

	bool isFinished(const QueueItemPtr& qi) const { RLock l(cs); return qi->isFinished(); }
	bool isWaiting(const QueueItemPtr& qi) const { RLock l(cs); return qi->isWaiting(); }
	uint64_t getDownloadedBytes(const QueueItemPtr& qi) const { RLock l(cs); return qi->getDownloadedBytes(); }

	QueueItem::SourceList getSources(const QueueItemPtr& qi) const { RLock l(cs); return qi->getSources(); }
	QueueItem::SourceList getBadSources(const QueueItemPtr& qi) const { RLock l(cs); return qi->getBadSources(); }

	Bundle::SourceInfoList getBundleSources(const BundlePtr& b) const { RLock l(cs); return b->getSources(); }
	Bundle::SourceInfoList getBadBundleSources(const BundlePtr& b) const { RLock l(cs); return b->getBadSources(); }

	size_t getSourcesCount(const QueueItemPtr& qi) const { RLock l(cs); return qi->getSources().size(); }
	void getChunksVisualisation(const QueueItemPtr& qi, vector<Segment>& running, vector<Segment>& downloaded, vector<Segment>& done) const { RLock l(cs); qi->getChunksVisualisation(running, downloaded, done); }

	bool getQueueInfo(const HintedUser& aUser, string& aTarget, int64_t& aSize, int& aFlags, string& bundleToken) noexcept;
	Download* getDownload(UserConnection& aSource, const OrderedStringSet& onlineHubs, string& aMessage, string& newUrl, bool smallSlot) noexcept;
	void putDownload(Download* aDownload, bool finished, bool noAccess=false, bool rotateQueue=false) noexcept;
	
	/** @return The highest priority download the user has, PAUSED may also mean no downloads */
	QueueItemBase::Priority hasDownload(const UserPtr& aUser, const OrderedStringSet& onlineHubs, bool smallSlot) noexcept;
	/** The same thing but only used before any connect requests */
	QueueItemBase::Priority hasDownload(const UserPtr& aUser, string& hubUrl, bool smallSlot, string& bundleToken, bool& allowUrlChange) noexcept;
	
	void loadQueue() noexcept;
	void saveQueue(bool force) noexcept;

	void noDeleteFileList(const string& path);
	
	bool getSearchInfo(const string& aTarget, TTHValue& tth_, int64_t size_) noexcept;
	bool handlePartialSearch(const UserPtr& aUser, const TTHValue& tth, PartsInfo& _outPartsInfo, string& _bundle, bool& _reply, bool& _add);
	bool handlePartialResult(const HintedUser& aUser, const TTHValue& tth, const QueueItem::PartialSource& partialSource, PartsInfo& outPartialInfo);
	void addBundleTTHList(const HintedUser& aUser, const string& bundle, const TTHValue& tth);
	MemoryInputStream* generateTTHList(const string& bundleToken, bool isInSharingHub);

	//merging, adding, deletion
	bool addBundle(BundlePtr& aBundle, bool loading = false);
	void readdBundle(BundlePtr& aBundle);
	void connectBundleSources(BundlePtr& aBundle);
	void mergeBundle(BundlePtr& targetBundle, BundlePtr& sourceBundle);
	void mergeFileBundles(BundlePtr& aBundle);
	void moveBundle(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished);
	void splitBundle(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished);
	int changeBundleTarget(BundlePtr& aBundle, const string& newTarget);
	void moveFileBundle(BundlePtr& aBundle, const string& aTarget) noexcept;
	void removeBundleItem(QueueItemPtr& qi, bool finished, bool moved = false);
	void moveBundleItem(QueueItemPtr qi, BundlePtr& targetBundle, bool fireAdded); //don't use reference here!
	void moveBundleItems(const QueueItemList& ql, BundlePtr& targetBundle, bool fireAdded);
	void moveBundleItems(BundlePtr& sourceBundle, BundlePtr& targetBundle, bool fireAdded);
	void removeBundle(BundlePtr& aBundle, bool finished, bool removeFinished, bool moved = false);
	uint8_t isDirQueued(const string& aDir) const;
	tstring getDirPath(const string& aDir) const;
	void getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const TargetUtil::VolumeSet& volumes) const { RLock l (cs); bundleQueue.getDiskInfo(dirMap, volumes); }
	void getUnfinishedPaths(StringList& bundles);
	void getForbiddenPaths(StringList& bundlePaths, const StringList& sharePaths);

	BundlePtr getBundle(const string& bundleToken) { RLock l (cs); return bundleQueue.findBundle(bundleToken); }
	BundlePtr findBundle(const TTHValue& tth);
	bool checkPBDReply(HintedUser& aUser, const TTHValue& aTTH, string& _bundleToken, bool& _notify, bool& _add, const string& remoteBundle);
	void addFinishedNotify(HintedUser& aUser, const TTHValue& aTTH, const string& remoteBundle);
	void updatePBD(const HintedUser& aUser, const TTHValue& aTTH);
	void removeBundleNotify(const UserPtr& aUser, const string& bundleToken);
	void setBundlePriority(const string& bundleToken, QueueItemBase::Priority p) noexcept;
	void setBundlePriority(BundlePtr& aBundle, QueueItemBase::Priority p, bool isAuto=false, bool isQIChange=false) noexcept;
	void setBundleAutoPriority(const string& bundleToken, bool isQIChange=false) noexcept;
	void removeBundleSource(const string& bundleToken, const UserPtr& aUser) noexcept;
	void removeBundleSource(BundlePtr aBundle, const UserPtr& aUser) noexcept;
	void sendRemovePBD(const HintedUser& aUser, const string& aRemoteToken);
	void getBundleInfo(const string& aSource, BundleList& retBundles, int& finishedFiles, int& fileBundles) { 
		RLock l (cs); 
		bundleQueue.getInfo(aSource, retBundles, finishedFiles, fileBundles); 
	}
	void handleBundleUpdate(const string& bundleToken);

	void removeDir(const string aSource, const BundleList& sourceBundles, bool removeFinished);
	bool moveBundleFile(QueueItemPtr& qs, const string& aTarget, bool movingSingleItems) noexcept;

	void setBundlePriorities(const string& aSource, const BundleList& sourceBundles, QueueItemBase::Priority p, bool autoPrio=false);
	void calculateBundlePriorities(bool verbose);
	void searchBundle(BundlePtr& aBundle, bool manual);

	int getBundleItemCount(const BundlePtr& aBundle) const noexcept;
	int getFinishedItemCount(const BundlePtr& aBundle) const noexcept;
	int getDirItemCount(const BundlePtr& aBundle, const string& aDir) const noexcept;

	/** Move the target location of a queued item. Running items are silently ignored */
	void moveFiles(const StringPairList& sourceTargetList) noexcept;
	int isFileQueued(const TTHValue& aTTH, const string& aFile) { RLock l (cs); return fileQueue.isFileQueued(aTTH, aFile); }
	
	bool dropSource(Download* d);

	int64_t getUserQueuedSize(const UserPtr& u);

	bool isChunkDownloaded(const TTHValue& tth, int64_t startPos, int64_t& bytes, int64_t& fileSize_, string& tempTarget);
	string getBundlePath(const string& aBundleToken) const;
	
	GETSET(uint64_t, lastSave, LastSave);
	GETSET(uint64_t, lastAutoPrio, LastAutoPrio);

	enum { MOVER_LIMIT = 10*1024*1024 };
	class FileMover : public Thread {
	public:
		enum Tasks {
			MOVE_FILE,
			REMOVE_DIR
		};

		FileMover() { }
		virtual ~FileMover() { join(); }

		void moveFile(const string& source, const string& target, QueueItemPtr aBundle);
		void removeDir(const string& aDir);
		virtual int run();
	private:

		static atomic_flag active;
		TaskQueue tasks;
	} mover;

	class Rechecker : public Thread {

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

	/** QueueItems by target and TTH */
	FileQueue fileQueue;

	/** Bundles by target */
	BundleQueue bundleQueue;

	void shareBundle(const string& aName);
	void runAltSearch();


	void lockRead() noexcept { cs.lock_shared(); }
	void unlockRead() noexcept { cs.unlock_shared(); }

	void setMatchers();
private:
	friend class QueueLoader;
	friend class Singleton<QueueManager>;
	
	QueueManager();
	~QueueManager();
	
	mutable SharedMutex cs;

	Socket udp;

	/** QueueItems by user */
	UserQueue userQueue;
	/** File lists not to delete */
	StringList protectedFileLists;
	/** Sanity check for the target filename */
	static string checkTarget(const string& aTarget, bool checkExsistence, BundlePtr aBundle = NULL) throw(QueueException, FileException);
	/** Add a source to an existing queue item */
	bool addSource(QueueItemPtr& qi, const HintedUser& aUser, Flags::MaskType addBad, const string& aRemotePath, bool newBundle=false, bool checkTLS=true) throw(QueueException, FileException);
	 
	void matchTTHList(const string& name, const HintedUser& user, int flags);

	void addBundleUpdate(const BundlePtr& aBundle);

	void addFinishedItem(const TTHValue& tth, BundlePtr& aBundle, const string& aTarget, time_t aSize, int64_t aFinished);

	void load(const SimpleXML& aXml);

	//always use forceThreading if this is called from within a lock and it's being used for bundle items
	void moveFile(const string& source, const string& target, QueueItemPtr q = nullptr, bool forceThreading = false);
	static void moveFile_(const string& source, const string& target, QueueItemPtr& q);

	void handleMovedBundleItem(QueueItemPtr& q);

	void moveStuckFile(QueueItemPtr& qi);
	void rechecked(QueueItemPtr& qi);
	void onFileHashed(const string& fname, const TTHValue& root, bool failed);
	void hashBundle(BundlePtr& aBundle);
	bool scanBundle(BundlePtr& aBundle);
	void checkBundleHashed(BundlePtr aBundle);
	void onBundleStatusChanged(BundlePtr& aBundle, AutoSearch::Status aStatus);
	void onBundleRemoved(BundlePtr& aBundle, bool finished);

	bool replaceFinishedItem(QueueItemPtr qi);

	void removeSource(QueueItemPtr& qi, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;

	string getListPath(const HintedUser& user);

	StringMatch highPrioFiles;
	StringMatch skipList;

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	
	// SearchManagerListener
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;
	
	// HashManagerListener
	void on(HashManagerListener::TTHDone, const string& fname, const TTHValue& root) noexcept { onFileHashed(fname, root, false); }
	void on(HashManagerListener::HashFailed, const string& fname) noexcept { onFileHashed(fname, TTHValue(Util::emptyString), true); }

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool wasOffline) noexcept;
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept;

	DelayedEvents<string> delayEvents;
};

} // namespace dcpp

#endif // !defined(QUEUE_MANAGER_H)