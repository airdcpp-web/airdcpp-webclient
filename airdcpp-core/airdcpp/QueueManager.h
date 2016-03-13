/*
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#include "QueueManagerListener.h"
#include "SearchManagerListener.h"
#include "ClientManagerListener.h"
#include "DownloadManagerListener.h"
#include "TimerManager.h"

#include "BundleQueue.h"
#include "ClientManager.h"
#include "DelayedEvents.h"
#include "DupeType.h"
#include "Exception.h"
#include "File.h"
#include "FileQueue.h"
#include "HashBloom.h"
#include "HashManagerListener.h"
#include "MerkleTree.h"
#include "QueueItem.h"
#include "ShareManagerListener.h"
#include "Singleton.h"
#include "Socket.h"
#include "StringMatch.h"
#include "TargetUtil.h"
#include "TaskQueue.h"
#include "UserQueue.h"

// For Boost 1.60
#if defined (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wredeclared-class-member"
#endif
#include <boost/bimap.hpp>
#include <boost/bimap/unordered_multiset_of.hpp>
#if defined (__clang__)
#pragma clang diagnostic pop
#endif


namespace dcpp {

namespace bimaps = boost::bimaps;

class HashedFile;
class UserConnection;
class QueueLoader;

class QueueManager : public Singleton<QueueManager>, public Speaker<QueueManagerListener>, private TimerManagerListener, 
	private SearchManagerListener, private ClientManagerListener, private HashManagerListener, private ShareManagerListener
{
public:
	// Add all queued TTHs in the supplied bloom filter
	void getBloom(HashBloom& bloom) const noexcept;

	// Get the total number of queued bundle files
	size_t getQueuedBundleFiles() const noexcept;

	// Check if there are downloaded bytes (running downloads or finished segments) for the specified file
	bool hasDownloadedBytes(const string& aTarget) throw(QueueException);

	// Get the subdirectories and total file count of a bundle
	void getBundleContent(const BundlePtr& aBundle, size_t& files_, size_t& directories_) const noexcept;

	// Get the total queued bytes
	uint64_t getTotalQueueSize() const noexcept { return fileQueue.getTotalQueueSize(); }

	/** Add a user's filelist to the queue. */
	QueueItemPtr addList(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString, BundlePtr aBundle=nullptr) throw(QueueException, FileException);

	/** Add an item that is opened in the client or with an external program */
	/** Files that are viewed in the client should be added from ViewFileManager */
	QueueItemPtr addOpenedItem(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, bool aIsClientView, bool aIsText = true) throw(QueueException, FileException);

	/** Readd a source that was removed */
	void readdQISource(const string& target, const HintedUser& aUser) throw(QueueException);
	void readdBundleSource(BundlePtr aBundle, const HintedUser& aUser) noexcept;

	// Change bundle to use sequential order (instead of random order)
	void onUseSeqOrder(BundlePtr& aBundle) noexcept;

	/** Add a directory to the queue (downloads filelist and matches the directory). */
	void matchListing(const DirectoryListing& dl, int& matches, int& newFiles, BundleList& bundles) noexcept;

	QueueItemPtr findFile(QueueToken aToken) const noexcept { RLock l(cs); return fileQueue.findFile(aToken); }

	// Removes the file from queue (and alternatively the target if the file is finished)
	template<typename T>
	bool removeFile(const T& aID, bool removeData = false) noexcept {
		QueueItemPtr qi = nullptr;
		{
			RLock l(cs);
			qi = fileQueue.findFile(aID);
		}

		if (qi) {
			removeQI(qi, removeData);
			return true;
		}

		return false;
	}

	// Return all finished non-failed bundles
	// Returns the number of bundles that were removed
	int removeFinishedBundles() noexcept;

	// Remove source from the specified file
	void removeFileSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;

	// Remove source from all files. excludeF can be used to filter certain files from removal.
	// Returns the number of files from which the source was removed
	int removeSource(const UserPtr& aUser, Flags::MaskType reason, std::function<bool (const QueueItemPtr&) > excludeF = nullptr) noexcept;

	// Set priority for all bundles
	// Won't affect bundles that are added later
	// Use DEFAULT priority to enable auto priority
	void setPriority(QueueItemBase::Priority p) noexcept;

	// Set priority for the file.
	// Use DEFAULT priority to enable auto priority
	void setQIPriority(const string& aTarget, QueueItemBase::Priority p) noexcept;

	// Set priority for the file.
	// keepAutoPrio should be used only when performing auto priorization.
	// Use DEFAULT priority to enable auto priority
	void setQIPriority(QueueItemPtr& qi, QueueItemBase::Priority p, bool keepAutoPrio=false) noexcept;

	// Toggle autoprio for the file
	void setQIAutoPriority(const string& aTarget) noexcept;

	// Get real paths for the specified tth
	StringList getTargets(const TTHValue& tth) noexcept;

	// Toggle the state of slow speed disconnecting for the given bundle.
	void toggleSlowDisconnectBundle(QueueToken aBundleToken) noexcept;

	// Get temp path for the specified target file
	string getTempTarget(const string& aTarget) noexcept;

	// Set the maximum number of segments for the specified target
	void setSegments(const string& aTarget, uint8_t aSegments) noexcept;

	bool isFinished(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->isFinished(); }
	bool isWaiting(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->isWaiting(); }

	uint64_t getDownloadedBytes(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->getDownloadedBytes(); }
	uint64_t getSecondsLeft(const QueueItemPtr& qi) const noexcept{ RLock l(cs); return qi->getSecondsLeft(); }
	uint64_t getAverageSpeed(const QueueItemPtr& qi) const noexcept{ RLock l(cs); return qi->getAverageSpeed(); }

	QueueItem::SourceList getSources(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->getSources(); }
	QueueItem::SourceList getBadSources(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->getBadSources(); }

	Bundle::SourceList getBundleSources(const BundlePtr& b) const noexcept { RLock l(cs); return b->getSources(); }
	Bundle::SourceList getBadBundleSources(const BundlePtr& b) const noexcept { RLock l(cs); return b->getBadSources(); }

	size_t getSourcesCount(const QueueItemPtr& qi) const noexcept { RLock l(cs); return qi->getSources().size(); }
	void getChunksVisualisation(const QueueItemPtr& qi, vector<Segment>& running, vector<Segment>& downloaded, vector<Segment>& done) const noexcept { RLock l(cs); qi->getChunksVisualisation(running, downloaded, done); }


	// Get information about the next valid file in the queue
	// Used for displaying initial information for a transfer before the connection has been established and the real download is created
	bool getQueueInfo(const HintedUser& aUser, string& aTarget, int64_t& aSize, int& aFlags, QueueToken& bundleToken) noexcept;

	// Check if a download can be started for the specified user
	// 
	// lastError_ will contain the last error why a file can't be started (not cleared if a download is found afterwards)
	// TODO: FINISH
	bool startDownload(const UserPtr& aUser, const QueueTokenSet& runningBundles, const OrderedStringSet& onlineHubs,
		QueueItemBase::DownloadType aType, int64_t aLastSpeed, string& lastError_) noexcept;

	// The same thing but only used before any connect requests
	// newUrl can be changed if the download is for a filelist from a different hub
	// lastError_ will contain the last error why a file can't be started (not cleared if a download is found afterwards)
	// hasDownload will be set to true if there are any files queued from the user
	// TODO: FINISH
	pair<QueueItem::DownloadType, bool> startDownload(const UserPtr& aUser, string& hubUrl, QueueItemBase::DownloadType aType, QueueToken& bundleToken,
		bool& allowUrlChange, bool& hasDownload, string& lastError_) noexcept;

	// Creates new download for the specified user
	// This won't check various download limits so startDownload should be called first
	// newUrl can be changed if the download is for a filelist from a different hub
	// lastError_ will contain the last error why a file can't be started (not cleared if a download is found afterwards)
	Download* getDownload(UserConnection& aSource, const QueueTokenSet& runningBundles, const OrderedStringSet& onlineHubs, string& lastError_, string& newUrl, QueueItemBase::DownloadType aType) noexcept;

	// Handle an ended transfer
	// finished should be true if the file/segment was finished successfully (false if disconnected/failed). Always false for finished trees.
	// noAccess should be true if the transfer failed because there was no access to the file.
	// rotateQueue will put current bundle file at the end of the transfer source's user queue (e.g. there's a problem with the local target and other files should be tried next).
	// HashException will thrown only for tree transfers that could not be stored in the hash database.
	void putDownload(Download* aDownload, bool finished, bool noAccess=false, bool rotateQueue=false) throw(HashException);
	

	void loadQueue(function<void (float)> progressF) noexcept;

	// Force will force bundle to be saved even when it's not dirty (not recommended as it may take a long time with huge queues)
	void saveQueue(bool force) noexcept;
	void shutdown() noexcept;

	void noDeleteFileList(const string& path) noexcept;

	// Being called after the skiplist/high prio pattern has been changed 
	void setMatchers() noexcept;

	SharedMutex& getCS() { return cs; }
	// Locking must be handled by the caller
	const Bundle::TokenBundleMap& getBundles() const { return bundleQueue.getBundles(); }
	// Locking must be handled by the caller
	const QueueItem::StringMap& getFileQueue() const { return fileQueue.getPathQueue(); }

	// Create a directory bundle with the supplied target path and files
	// 
	// aDate is the original date of the bundle (usually the modify date from source user)
	// QueueException will be thrown only if the source is invalid (source can be nullptr though)
	// errorMsg_ will contain errors related to queueing the files
	// nullptr can be returned if no files could be queued
	BundlePtr createDirectoryBundle(const string& aTarget, const HintedUser& aUser, BundleFileInfo::List& aFiles, 
		QueueItemBase::Priority aPrio, time_t aDate, string& errorMsg_) throw(QueueException);

	// Create a file bundle with the supplied target path
	// 
	// aDate is the original date of the bundle (usually the modify date from source user)
	// Source can be nullptr
	// All errors will be thrown
	// May return nullptr if the file is already active in queue or when adding 0-byte files
	BundlePtr createFileBundle(const string& aTarget, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, time_t aDate, 
		Flags::MaskType aFlags = 0, QueueItemBase::Priority aPrio = QueueItem::DEFAULT) throw(QueueException, FileException, DupeException);

	bool removeBundle(QueueToken aBundleToken, bool removeFinishedFiles) noexcept;
	void removeBundle(BundlePtr& aBundle, bool removeFinishedFiles) noexcept;

	// Find a bundle by token
	BundlePtr findBundle(QueueToken aBundleToken) const noexcept { RLock l (cs); return bundleQueue.findBundle(aBundleToken); }

	// Find a bundle containing the specified TTH
	BundlePtr findBundle(const TTHValue& tth) const noexcept;


	/* Partial bundle sharing */
	bool checkPBDReply(HintedUser& aUser, const TTHValue& aTTH, string& _bundleToken, bool& _notify, bool& _add, const string& remoteBundle) noexcept;
	void addFinishedNotify(HintedUser& aUser, const TTHValue& aTTH, const string& remoteBundle) noexcept;
	void updatePBD(const HintedUser& aUser, const TTHValue& aTTH) noexcept;

	// Remove user from a notify list of the local bundle
	void removeBundleNotify(const UserPtr& aUser, QueueToken aBundleToken) noexcept;

	void sendRemovePBD(const HintedUser& aUser, const string& aRemoteToken) noexcept;
	bool getSearchInfo(const string& aTarget, TTHValue& tth_, int64_t size_) noexcept;
	bool handlePartialSearch(const UserPtr& aUser, const TTHValue& tth, PartsInfo& _outPartsInfo, string& _bundle, bool& _reply, bool& _add) noexcept;
	bool handlePartialResult(const HintedUser& aUser, const TTHValue& tth, const QueueItem::PartialSource& partialSource, PartsInfo& outPartialInfo) noexcept;

	// Queue a TTH list from the user containing the supplied TTH
	void addBundleTTHList(const HintedUser& aUser, const string& aRemoteBundleToken, const TTHValue& tth) throw(QueueException);
	MemoryInputStream* generateTTHList(QueueToken aBundleToken, bool isInSharingHub, BundlePtr& bundle_) throw(QueueException);

	//Bundle download failed due to Ex. disk full
	void bundleDownloadFailed(BundlePtr& aBundle, const string& aError);

	/* Priorities */
	// Use DEFAULT priority to enable auto priority
	void setBundlePriority(QueueToken aBundleToken, QueueItemBase::Priority p) noexcept;

	// Set new priority for the specified bundle
	// keepAutoPrio should be used only when performing auto priorization.
	// Use DEFAULT priority to enable auto priority
	void setBundlePriority(BundlePtr& aBundle, QueueItemBase::Priority p, bool aKeepAutoPrio=false) noexcept;

	// Toggle autoprio state for the bundle
	void toggleBundleAutoPriority(QueueToken aBundleToken) noexcept;
	void toggleBundleAutoPriority(BundlePtr& aBundle) noexcept;

	// Perform autopriorization for applicable bundles
	// verbose is only used for debugging purposes to print the points for each bundle
	void calculateBundlePriorities(bool verbose) noexcept;


	void removeBundleSource(QueueToken aBundleToken, const UserPtr& aUser, Flags::MaskType reason) noexcept;
	void removeBundleSource(BundlePtr aBundle, const UserPtr& aUser, Flags::MaskType reason) noexcept;

	// Get source infos for the specified user
	void getSourceInfo(const UserPtr& aUser, Bundle::SourceBundleList& aSources, Bundle::SourceBundleList& aBad) const noexcept;

	// Check if the source is slow enough for slow speed disconnecting
	bool checkDropSlowSource(Download* d) noexcept;


	// Disconnect source user according to the slow speed disconnect mode
	void handleSlowDisconnect(const UserPtr& aUser, const string& aTarget, const BundlePtr& aBundle) noexcept;

	// Search bundle for alternatives on the background
	void searchBundleAlternates(BundlePtr& aBundle, bool aIsManualSearch, uint64_t aTick = GET_TICK()) noexcept;

	int getUnfinishedItemCount(const BundlePtr& aBundle) const noexcept;
	int getFinishedItemCount(const BundlePtr& aBundle) const noexcept;


	// Check if there are finished chuncks for the TTH
	// Gets various information about the actual file and the length of downloaded segment
	// Used for partial file sharing checks
	bool isChunkDownloaded(const TTHValue& tth, int64_t startPos, int64_t& bytes, int64_t& fileSize_, string& tempTarget) noexcept;

	DupeType isFileQueued(const TTHValue& aTTH) const noexcept { RLock l(cs); return fileQueue.isFileQueued(aTTH); }

	// Get real path of the bundle
	string getBundlePath(QueueToken aBundleToken) const noexcept;

	// Return dupe information about the directory
	DupeType isDirQueued(const string& aDir, int64_t aSize) const noexcept;

	// Get all real paths of the directory
	// You may also give a path in NMDC format and the relevant 
	// directory (+ possible subdirectories) are detected automatically
	StringList getDirPaths(const string& aDir) const noexcept;

	// Get the amount of queued bytes for each mountpoint (takes reserved space into account as well)
	void getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const TargetUtil::VolumeSet& volumes) const noexcept { 
		RLock l(cs); 
		bundleQueue.getDiskInfo(dirMap, volumes); 
	}

	// Get the paths of all unfinished bundles
	void getUnfinishedPaths(StringList& bundles) noexcept;

	// Get the paths of all unfinished bundles
	// Scans all finished bundles inside the directories being refreshed and queues succeeded for hashing
	void checkRefreshPaths(StringList& bundlePaths_, RefreshPathList& refreshPaths_) noexcept;

	// Set size for a file list its size is known
	void setFileListSize(const string& path, int64_t newSize) noexcept;


	// Attempt to add a bundle in share
	// Share scanning will be skipped if skipScan is true
	// Blocking call
	void shareBundle(BundlePtr aBundle, bool skipScan) noexcept;

	// Returns true if the bundle passes the scan for missing/extra files
	// Blocking call
	bool scanBundle(BundlePtr& aBundle) noexcept;

	// Performs recheck for the supplied files. Recheck will be done in the calling thread.
	// The integrity of all finished segments will be verified and SFV will be validated for finished files
	// The file will be paused if running
	void recheckFiles(QueueItemList aQL) noexcept;

	// Performs recheck for the supplied bundle. Recheck will be done in the calling thread.
	// The integrity of all finished segments will be verified and SFV will be validated for finished files
	// The bundle will be paused if running
	void recheckBundle(QueueToken aBundleToken) noexcept;
private:
	IGETSET(uint64_t, lastSave, LastSave, 0);
	IGETSET(uint64_t, lastAutoPrio, LastAutoPrio, 0);

	DispatcherQueue tasks;

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

	bool recheckFileImpl(const string& aPath, bool isBundleCheck, int64_t& failedBytes_) noexcept;
	void handleFailedRecheckItems(const QueueItemList& ql) noexcept;

	void connectBundleSources(BundlePtr& aBundle) noexcept;
	bool allowStartQI(const QueueItemPtr& aQI, const QueueTokenSet& runningBundles, string& lastError_, bool mcn = false) noexcept;

	void removeBundleItem(QueueItemPtr& qi, bool finished) noexcept;
	void addLoadedBundle(BundlePtr& aBundle) noexcept;
	bool addBundle(BundlePtr& aBundle, const string& aTarget, int filesAdded) noexcept;
	void readdBundle(BundlePtr& aBundle) noexcept;
	void removeBundleLists(BundlePtr& aBundle) noexcept;

	void removeQI(QueueItemPtr& qi, bool removeData = false) noexcept;

	void handleBundleUpdate(QueueToken aBundleToken) noexcept;

	/** Get a bundle for adding new items in queue (a new one or existing)  */
	BundlePtr getBundle(const string& aTarget, QueueItemBase::Priority aPrio, time_t aDate, bool isFileBundle) noexcept;

	/** Add a file to the queue (returns the item and whether it didn't exist before) */
	bool addFile(const string& aTarget, int64_t aSize, const TTHValue& root, const HintedUser& aUser, Flags::MaskType aFlags, bool addBad, QueueItemBase::Priority aPrio, bool& wantConnection, BundlePtr& aBundle) throw(QueueException, FileException);

	/** Check that we can download from this user */
	void checkSource(const HintedUser& aUser) const throw(QueueException);

	/** Check that we can download from this user */
	void validateBundleFile(const string& aBundleDir, string& aBundleFile, const TTHValue& aTTH, QueueItemBase::Priority& priority_) const throw(QueueException, FileException, DupeException);

	/** Sanity check for the target filename */
	//static string checkTargetPath(const string& aTarget) throw(QueueException, FileException);
	static string checkTarget(const string& toValidate, const string& aParentDir=Util::emptyString) throw(QueueException, FileException);
	static string formatBundleTarget(const string& aPath, time_t aRemoteDate) noexcept;

	/** Add a source to an existing queue item */
	bool addSource(QueueItemPtr& qi, const HintedUser& aUser, Flags::MaskType addBad, bool newBundle=false, bool checkTLS=true) throw(QueueException, FileException);
	 
	void matchTTHList(const string& name, const HintedUser& user, int flags) noexcept;

	void addBundleUpdate(const BundlePtr& aBundle) noexcept;

	void moveFinishedFile(const string& source, const string& target, const QueueItemPtr& aQI) noexcept;
	void moveFinishedFileImpl(const string& source, const string& target, QueueItemPtr q) noexcept;

	void handleMovedBundleItem(QueueItemPtr& q) noexcept;
	bool checkBundleFinished(BundlePtr& aBundle) noexcept;

	unordered_map<string, SearchResultList> searchResults;
	void pickMatch(QueueItemPtr qi) noexcept;
	void matchBundle(QueueItemPtr& aQI, const SearchResultPtr& aResult) noexcept;

	void onFileHashed(const string& aPath, HashedFile& aFileInfo, bool failed) noexcept;
	void hashBundle(BundlePtr& aBundle) noexcept;
	void checkBundleHashed(BundlePtr& aBundle) noexcept;
	void setBundleStatus(BundlePtr aBundle, Bundle::Status newStatus) noexcept;

	/* Returns true if an item can be replaces */
	bool replaceItem(QueueItemPtr& qi, int64_t aSize, const TTHValue& aTTH) throw(FileException, QueueException);

	void removeFileSource(QueueItemPtr& qi, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;

	string getListPath(const HintedUser& user) const noexcept;

	void fileFinished(const QueueItemPtr aQi, const HintedUser& aUser, int64_t aSpeed, const string& aDir) noexcept;

	StringMatch highPrioFiles;
	StringMatch skipList;

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	
	// SearchManagerListener
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;
	
	// HashManagerListener
	void on(HashManagerListener::FileHashed, const string& aPath, HashedFile& fi) noexcept { onFileHashed(aPath, fi, false); }
	void on(HashManagerListener::FileFailed, const string& aPath, HashedFile& fi) noexcept { onFileHashed(aPath, fi, true); }

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool wasOffline) noexcept;
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept;

	// ShareManagerListener
	void on(ShareManagerListener::DirectoriesRefreshed, uint8_t, const RefreshPathList& aPaths) noexcept;
	void on(ShareLoaded) noexcept;
	void onPathRefreshed(const string& aPath, bool startup) noexcept;

	DelayedEvents<QueueToken> delayEvents;

	typedef boost::bimap<bimaps::unordered_multiset_of<QueueToken>, bimaps::unordered_multiset_of<string>> TokenStringMultiBiMap;
	TokenStringMultiBiMap matchLists;
};

} // namespace dcpp

#endif // !defined(QUEUE_MANAGER_H)
