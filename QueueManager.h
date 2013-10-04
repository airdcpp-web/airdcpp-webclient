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

#ifdef _MSC_VER
# pragma warning(disable: 4512) // assignment operator could not be generated (bimap)
#endif

#include "concurrency.h"

#include "QueueManagerListener.h"
#include "SearchManagerListener.h"
#include "ClientManagerListener.h"
#include "DownloadManagerListener.h"
#include "TimerManager.h"

#include "BundleQueue.h"
#include "ClientManager.h"
#include "DelayedEvents.h"
#include "DirectoryListing.h"
#include "Exception.h"
#include "File.h"
#include "FileQueue.h"
#include "HashBloom.h"
#include "HashedFile.h"
#include "HashManager.h"
#include "MerkleTree.h"
#include "QueueItem.h"
#include "Singleton.h"
#include "Socket.h"
#include "StringMatch.h"
#include "TargetUtil.h"
#include "TaskQueue.h"
#include "User.h"
#include "UserQueue.h"

#include <boost/bimap.hpp>
#include <boost/bimap/unordered_multiset_of.hpp>


namespace dcpp {

namespace bimaps = boost::bimaps;

STANDARD_EXCEPTION(QueueException);
STANDARD_EXCEPTION(DupeException);

class UserConnection;

class ConnectionQueueItem;
class QueueLoader;

class QueueManager : public Singleton<QueueManager>, public Speaker<QueueManagerListener>, private TimerManagerListener, 
	private SearchManagerListener, private ClientManagerListener, private HashManagerListener
{
public:
	void getBloom(HashBloom& bloom) const noexcept;
	size_t getQueuedBundleFiles() const noexcept;
	bool hasDownloadedBytes(const string& aTarget) throw(QueueException);
	uint64_t getTotalQueueSize() const noexcept { return fileQueue.getTotalQueueSize(); }

	/** Add a user's filelist to the queue. */
	void addList(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString, BundlePtr aBundle=nullptr) throw(QueueException, FileException);

	/** Add an item that is opened in the client or with an external program */
	void addOpenedItem(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUse, bool isClientView) throw(QueueException, FileException);

	/** Readd a source that was removed */
	void readdQISource(const string& target, const HintedUser& aUser) throw(QueueException);
	void readdBundleSource(BundlePtr aBundle, const HintedUser& aUser) noexcept;
	void onUseSeqOrder(BundlePtr& aBundle) noexcept;

	/** Add a directory to the queue (downloads filelist and matches the directory). */
	void matchListing(const DirectoryListing& dl, int& matches, int& newFiles, BundleList& bundles) noexcept;

	void removeFile(const string aTarget) noexcept;
	void removeFileSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;
	void removeSource(const UserPtr& aUser, Flags::MaskType reason, std::function<bool (const QueueItemPtr&) > excludeF = nullptr) noexcept;

	void recheck(const string& aTarget);

	void setQIPriority(const string& aTarget, QueueItemBase::Priority p) noexcept;
	void setQIPriority(QueueItemPtr& qi, QueueItemBase::Priority p, bool isAP=false) noexcept;
	void setQIAutoPriority(const string& aTarget) noexcept;

	StringList getTargets(const TTHValue& tth) noexcept;
	void readLockedOperation(const function<void (const QueueItem::StringMap&)>& currentQueue);

	void onSlowDisconnect(const string& aToken) noexcept;

	string getTempTarget(const string& aTarget) noexcept;
	void setSegments(const string& aTarget, uint8_t aSegments) noexcept;

	bool isFinished(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->isFinished(); }
	bool isWaiting(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->isWaiting(); }
	uint64_t getDownloadedBytes(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->getDownloadedBytes(); }

	QueueItem::SourceList getSources(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->getSources(); }
	QueueItem::SourceList getBadSources(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->getBadSources(); }

	Bundle::SourceList getBundleSources(const BundlePtr& b) const noexcept { RLock l(cs); return b->getSources(); }
	Bundle::SourceList getBadBundleSources(const BundlePtr& b) const noexcept { RLock l(cs); return b->getBadSources(); }

	size_t getSourcesCount(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->getSources().size(); }
	void getChunksVisualisation(const QueueItemPtr& qi, vector<Segment>& running, vector<Segment>& downloaded, vector<Segment>& done) const { RLock l(cs); qi->getChunksVisualisation(running, downloaded, done); }

	bool getQueueInfo(const HintedUser& aUser, string& aTarget, int64_t& aSize, int& aFlags, string& bundleToken) noexcept;
	Download* getDownload(UserConnection& aSource, const StringSet& runningBundles, const OrderedStringSet& onlineHubs, string& aMessage, string& newUrl, QueueItemBase::DownloadType aType) noexcept;
	void putDownload(Download* aDownload, bool finished, bool noAccess=false, bool rotateQueue=false) throw(HashException);
	
	bool startDownload(const UserPtr& aUser, const StringSet& runningBundles, const OrderedStringSet& onlineHubs, QueueItemBase::DownloadType aType) noexcept;
	/** The same thing but only used before any connect requests */
	pair<QueueItem::DownloadType, bool> startDownload(const UserPtr& aUser, string& hubUrl, QueueItemBase::DownloadType aType, string& bundleToken, bool& allowUrlChange, bool& hasDownload) noexcept;
	
	void loadQueue(function<void (float)> progressF) noexcept;
	void saveQueue(bool force) noexcept;

	void noDeleteFileList(const string& path);

	//merging, adding, deletion
	BundlePtr createDirectoryBundle(const string& aTarget, const HintedUser& aUser, BundleFileList& aFiles, QueueItemBase::Priority aPrio, time_t aDate, string& errorMsg_) throw(QueueException, FileException);
	BundlePtr createFileBundle(const string& aTarget, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, time_t aDate, Flags::MaskType aFlags = 0, QueueItemBase::Priority aPrio = QueueItem::DEFAULT) throw(QueueException, FileException);
	void moveBundleDir(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished) noexcept;
	void removeBundle(BundlePtr& aBundle, bool finished, bool removeFinished, bool moved = false) noexcept;


	BundlePtr findBundle(const string& bundleToken) const noexcept { RLock l (cs); return bundleQueue.findBundle(bundleToken); }
	BundlePtr findBundle(const TTHValue& tth) const noexcept;

	/* Partial bundle sharing */
	bool checkPBDReply(HintedUser& aUser, const TTHValue& aTTH, string& _bundleToken, bool& _notify, bool& _add, const string& remoteBundle) noexcept;
	void addFinishedNotify(HintedUser& aUser, const TTHValue& aTTH, const string& remoteBundle) noexcept;
	void updatePBD(const HintedUser& aUser, const TTHValue& aTTH) noexcept;
	void removeBundleNotify(const UserPtr& aUser, const string& bundleToken) noexcept;
	void sendRemovePBD(const HintedUser& aUser, const string& aRemoteToken) noexcept;
	bool getSearchInfo(const string& aTarget, TTHValue& tth_, int64_t size_) noexcept;
	bool handlePartialSearch(const UserPtr& aUser, const TTHValue& tth, PartsInfo& _outPartsInfo, string& _bundle, bool& _reply, bool& _add) noexcept;
	bool handlePartialResult(const HintedUser& aUser, const TTHValue& tth, const QueueItem::PartialSource& partialSource, PartsInfo& outPartialInfo) noexcept;
	void addBundleTTHList(const HintedUser& aUser, const string& bundle, const TTHValue& tth) throw(QueueException);
	MemoryInputStream* generateTTHList(const string& bundleToken, bool isInSharingHub) throw(QueueException);


	/* Priorities */
	void setBundlePriority(const string& bundleToken, QueueItemBase::Priority p) noexcept;
	void setBundlePriority(BundlePtr& aBundle, QueueItemBase::Priority p, bool isAuto=false) noexcept;
	void setBundleAutoPriority(const string& bundleToken) noexcept;
	void calculateBundlePriorities(bool verbose) noexcept;

	void removeBundleSource(const string& bundleToken, const UserPtr& aUser, Flags::MaskType reason) noexcept;
	void removeBundleSource(BundlePtr aBundle, const UserPtr& aUser, Flags::MaskType reason) noexcept;

	void handleSlowDisconnect(const UserPtr& aUser, const string& aTarget, const BundlePtr& aBundle) noexcept;

	/** Move the target location of a queued item. Running items are silently ignored */
	void moveFiles(const StringPairList& sourceTargetList) noexcept;
	void removeDir(const string aSource, const BundleList& sourceBundles, bool removeFinished) noexcept;

	void searchBundle(BundlePtr& aBundle, bool manual) noexcept;

	/* Info collecting */
	void getBundleInfo(const string& aSource, BundleList& retBundles, int& finishedFiles, int& fileBundles) const noexcept {
		RLock l (cs); 
		bundleQueue.getInfo(aSource, retBundles, finishedFiles, fileBundles); 
	}
	int getBundleItemCount(const BundlePtr& aBundle) const noexcept;
	int getFinishedItemCount(const BundlePtr& aBundle) const noexcept;
	void getDirItems(const BundlePtr& aBundle, const string& aDir, QueueItemList& aItems) const noexcept;
	void getSourceInfo(const UserPtr& aUser, Bundle::SourceBundleList& aSources, Bundle::SourceBundleList& aBad) const noexcept;

	int isFileQueued(const TTHValue& aTTH) const noexcept { RLock l(cs); return fileQueue.isFileQueued(aTTH); }
	
	bool dropSource(Download* d) noexcept;

	bool isChunkDownloaded(const TTHValue& tth, int64_t startPos, int64_t& bytes, int64_t& fileSize_, string& tempTarget) noexcept;
	string getBundlePath(const string& aBundleToken) const noexcept;
	uint8_t isDirQueued(const string& aDir) const noexcept;
	StringList getDirPaths(const string& aDir) const noexcept;

	void getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const TargetUtil::VolumeSet& volumes) const noexcept { RLock l(cs); bundleQueue.getDiskInfo(dirMap, volumes); }
	void getUnfinishedPaths(StringList& bundles) noexcept;
	void checkRefreshPaths(StringList& bundlePaths, StringList& refreshPaths) noexcept;
	
	GETSET(uint64_t, lastSave, LastSave);
	GETSET(uint64_t, lastAutoPrio, LastAutoPrio);

	class FileMover : public Thread {
	public:
		enum Tasks {
			MOVE_FILE,
			REMOVE_DIR,
			SHUTDOWN
		};

		FileMover();
		virtual ~FileMover();

		void moveFile(const string& source, const string& target, QueueItemPtr aBundle);
		void removeDir(const string& aDir);
		void shutdown();
		virtual int run();
	private:

		Semaphore s;
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

	void shareBundle(const string& aName) noexcept;
	void runAltSearch() noexcept;

	RLock lockRead() { return RLock(cs); }

	void setMatchers() noexcept;
	void shutdown();
private:
	friend class QueueLoader;
	friend class Singleton<QueueManager>;
	
	QueueManager();
	~QueueManager();
	
	mutable SharedMutex cs;

	Socket udp;

	/** QueueItems by target and TTH */
	FileQueue fileQueue;

	/** Bundles by target */
	BundleQueue bundleQueue;

	/** QueueItems by user */
	UserQueue userQueue;

	/** File lists not to delete */
	StringList protectedFileLists;

	task_group tasks;

	void connectBundleSources(BundlePtr& aBundle) noexcept;
	bool allowStartQI(const QueueItemPtr& aQI, const StringSet& runningBundles, bool mcn = false) noexcept;

	int changeBundleTarget(BundlePtr& aBundle, const string& newTarget) noexcept;
	void removeBundleItem(QueueItemPtr& qi, bool finished, bool moved = false) noexcept;
	void moveBundleItem(QueueItemPtr qi, BundlePtr& targetBundle) noexcept; //don't use reference here!
	void addLoadedBundle(BundlePtr& aBundle) noexcept;
	bool addBundle(BundlePtr& aBundle, const string& aTarget, int filesAdded, bool moving = false) noexcept;
	void readdBundle(BundlePtr& aBundle) noexcept;

	bool changeTarget(QueueItemPtr& qs, const string& aTarget) noexcept;
	void removeQI(QueueItemPtr& qi, bool noFiring = false) noexcept;

	void handleBundleUpdate(const string& bundleToken) noexcept;

	/** Get a bundle for adding new items in queue (a new one or existing)  */
	BundlePtr getBundle(const string& aTarget, QueueItemBase::Priority aPrio, time_t aDate, bool isFileBundle) noexcept;

	/** Add a file to the queue (returns the item and whether it didn't exist before) */
	bool addFile(const string& aTarget, int64_t aSize, const TTHValue& root, const HintedUser& aUser, Flags::MaskType aFlags, bool addBad, QueueItemBase::Priority aPrio, bool& wantConnection, BundlePtr& aBundle) throw(QueueException, FileException);

	/** Check that we can download from this user */
	void checkSource(const HintedUser& aUser) const throw(QueueException);

	/** Check that we can download from this user */
	void validateBundleFile(const string& aBundleDir, string& aBundleFile, const TTHValue& aTTH, QueueItemBase::Priority& aPrio) const throw(QueueException);

	/** Sanity check for the target filename */
	//static string checkTargetPath(const string& aTarget) throw(QueueException, FileException);
	static string checkTarget(const string& toValidate, const string& aParentDir=Util::emptyString) throw(QueueException, FileException);
	static string formatBundleTarget(const string& aPath, time_t aRemoteDate) noexcept;

	/** Add a source to an existing queue item */
	bool addSource(QueueItemPtr& qi, const HintedUser& aUser, Flags::MaskType addBad, bool newBundle=false, bool checkTLS=true) throw(QueueException, FileException);

	/** Add a source to an existing queue item */
	void mergeFinishedItems(const string& aSource, const string& aTarget, BundlePtr& aSourceBundle, BundlePtr& aTargetBundle, bool moveFiles) noexcept;
	 
	void matchTTHList(const string& name, const HintedUser& user, int flags) noexcept;

	void addBundleUpdate(const BundlePtr& aBundle) noexcept;

	void addFinishedItem(const TTHValue& tth, BundlePtr& aBundle, const string& aTarget, int64_t aSize, time_t aFinished) noexcept;

	void load(const SimpleXML& aXml);

	static void moveFile_(const string& source, const string& target, QueueItemPtr& q);

	void handleMovedBundleItem(QueueItemPtr& q) noexcept;
	void checkBundleFinished(BundlePtr& aBundle, bool isPrivate) noexcept;

	unordered_map<string, SearchResultList> searchResults;
	void pickMatch(QueueItemPtr qi) noexcept;
	void matchBundle(QueueItemPtr& aQI, const SearchResultPtr& aResult) noexcept;

	void moveStuckFile(QueueItemPtr& qi);
	void rechecked(QueueItemPtr& qi);
	void onFileHashed(const string& aPath, HashedFile& aFileInfo, bool failed) noexcept;
	void hashBundle(BundlePtr& aBundle) noexcept;
	bool scanBundle(BundlePtr& aBundle) noexcept;
	void checkBundleHashed(BundlePtr& aBundle) noexcept;
	void setBundleStatus(BundlePtr& aBundle, Bundle::Status newStatus) noexcept;
	void removeFinishedBundle(BundlePtr& aBundle) noexcept;

	/* Returns true if an item can be replaces */
	bool replaceItem(QueueItemPtr& qi, int64_t aSize, const TTHValue& aTTH) throw(FileException, QueueException);

	void removeFileSource(QueueItemPtr& qi, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;

	string getListPath(const HintedUser& user) const noexcept;

	StringMatch highPrioFiles;
	StringMatch skipList;

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	
	// SearchManagerListener
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;
	
	// HashManagerListener
	void on(HashManagerListener::TTHDone, const string& aPath, HashedFile& fi) noexcept { onFileHashed(aPath, fi, false); }
	void on(HashManagerListener::HashFailed, const string& aPath, HashedFile& fi) noexcept { onFileHashed(aPath, fi, true); }

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool wasOffline) noexcept;
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept;

	DelayedEvents<string> delayEvents;

	typedef boost::bimap<bimaps::unordered_multiset_of<string>, bimaps::unordered_multiset_of<string>> StringMultiBiMap;
	StringMultiBiMap matchLists;
};

} // namespace dcpp

#endif // !defined(QUEUE_MANAGER_H)