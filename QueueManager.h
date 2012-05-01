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
#include "Socket.h"

#include "QueueManagerListener.h"
#include "SearchManagerListener.h"
#include "ClientManagerListener.h"
#include "DownloadManagerListener.h"
#include "LogManager.h"
#include "pme.h"
#include "HashManager.h"
#include "TargetUtil.h"

#include "BundleQueue.h"
#include "FileQueue.h"
#include "UserQueue.h"

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
	bool allowAdd(const string& aTarget, const TTHValue& root) throw(QueueException, FileException);
	/** Add a file to the queue. */
	void add(const string& aTarget, int64_t aSize, const TTHValue& root, const HintedUser& aUser,
		Flags::MaskType aFlags = 0, bool addBad = true, QueueItem::Priority aPrio = QueueItem::DEFAULT, BundlePtr aBundle=NULL) throw(QueueException, FileException);
		/** Add a user's filelist to the queue. */
	void addList(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString) throw(QueueException, FileException);
	void addListDir(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString) throw(QueueException, FileException);

	/** Readd a source that was removed */
	void readdQISource(const string& target, const HintedUser& aUser) throw(QueueException);
	void readdBundleSource(BundlePtr aBundle, const HintedUser& aUser) throw(QueueException);
	void onChangeDownloadOrder();
	void onUseSeqOrder(BundlePtr aBundle);

	/** Add a directory to the queue (downloads filelist and matches the directory). */
	void addDirectory(const string& aDir, const HintedUser& aUser, const string& aTarget, 
		QueueItem::Priority p = QueueItem::DEFAULT, bool useFullList = false) noexcept;
	void matchListing(const DirectoryListing& dl, int& matches, int& newFiles, BundleList& bundles) noexcept;

	void removeQI(QueueItemPtr qi, bool moved = false) noexcept;
	void remove(const string aTarget) noexcept;
	void removeSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;
	void removeSource(const UserPtr& aUser, Flags::MaskType reason) noexcept;

	void recheck(const string& aTarget);

	void setQIPriority(const string& aTarget, QueueItem::Priority p) noexcept;
	void setQIPriority(QueueItemPtr qi, QueueItem::Priority p, bool isAP=false, bool isBundleChange=false) noexcept;
	void setQIAutoPriority(const string& aTarget, bool ap, bool isBundleChange=false) noexcept;

	StringList getTargets(const TTHValue& tth);
	const QueueItem::StringMap& getQueue() noexcept { RLock l (cs); return fileQueue.getQueue(); } ;
	//const QueueItem::StringMap& lockQueue() noexcept { cs.lock(); return fileQueue.getQueue(); } ;
	//void unlockQueue() noexcept { cs.unlock(); }
	void onSlowDisconnect(const string& aToken);
	bool getAutoDrop(const string& aToken);

	string getTempTarget(const string& aTarget);

	QueueItem::SourceList getSources(const QueueItemPtr qi) const { RLock l(cs); return qi->getSources(); }
	QueueItem::SourceList getBadSources(const QueueItemPtr qi) const { RLock l(cs); return qi->getBadSources(); }
	size_t getSourcesCount(const QueueItemPtr qi) const { RLock l(cs); return qi->getSources().size(); }
	vector<Segment> getChunksVisualisation(const QueueItemPtr qi, int type) const { RLock l(cs); return qi->getChunksVisualisation(type); }

	bool getQueueInfo(const UserPtr& aUser, string& aTarget, int64_t& aSize, int& aFlags, string& bundleToken) noexcept;
	Download* getDownload(UserConnection& aSource, string& aMessage, bool smallSlot) noexcept;
	void putDownload(Download* aDownload, bool finished, bool reportFinish = true) noexcept;
	
	/** @return The highest priority download the user has, PAUSED may also mean no downloads */
	QueueItem::Priority hasDownload(const UserPtr& aUser, bool smallSlot, string& bundleToken) noexcept;
	
	void loadQueue() noexcept;
	void saveQueue(bool force) noexcept;

	void noDeleteFileList(const string& path);
	
	bool getTTH(const string& name, TTHValue& tth) noexcept;
	bool handlePartialSearch(const UserPtr& aUser, const TTHValue& tth, PartsInfo& _outPartsInfo, string& _bundle, bool& _reply, bool& _add);
	bool handlePartialResult(const HintedUser& aUser, const TTHValue& tth, const QueueItem::PartialSource& partialSource, PartsInfo& outPartialInfo);
	void addBundleTTHList(const HintedUser& aUser, const string& bundle, const TTHValue& tth);
	MemoryInputStream* generateTTHList(const string& bundleToken, bool isInSharingHub);

	//merging, adding, deletion
	bool addBundle(BundlePtr aBundle, bool loading = false);
	void readdBundle(BundlePtr aBundle);
	void connectBundleSources(BundlePtr aBundle);
	void mergeBundle(BundlePtr targetBundle, BundlePtr sourceBundle);
	void mergeFileBundles(BundlePtr aBundle);
	void moveBundle(const string& aTarget, BundlePtr sourceBundle, bool moveFinished);
	void splitBundle(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished);
	int changeBundleTarget(BundlePtr aBundle, const string& newTarget);
	void moveFileBundle(BundlePtr aBundle, const string& aTarget) noexcept;
	void removeBundleItem(QueueItemPtr qi, bool finished, bool moved = false);
	void moveBundleItem(QueueItemPtr qi, BundlePtr targetBundle, bool fireAdded);
	void moveBundleItems(const QueueItemList& ql, BundlePtr targetBundle, bool fireAdded);
	void moveBundleItems(BundlePtr sourceBundle, BundlePtr targetBundle, bool fireAdded);
	void removeBundle(BundlePtr aBundle, bool finished, bool removeFinished, bool moved = false);
	uint8_t isDirQueued(const string& aDir);
	tstring getDirPath(const string& aDir);
	void getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const StringSet& volumes) { RLock l (cs); bundleQueue.getDiskInfo(dirMap, volumes); }
	void getUnfinishedPaths(StringList& bundles);
	void getForbiddenPaths(StringList& bundles, const StringPairList& paths);

	BundlePtr getBundle(const string& bundleToken) { RLock l (cs); return bundleQueue.find(bundleToken); }
	BundlePtr findBundle(const TTHValue& tth);
	bool checkPBDReply(HintedUser& aUser, const TTHValue& aTTH, string& _bundleToken, bool& _notify, bool& _add, const string& remoteBundle);
	void addFinishedNotify(HintedUser& aUser, const TTHValue& aTTH, const string& remoteBundle);
	void updatePBD(const HintedUser& aUser, const TTHValue& aTTH);
	void removeBundleNotify(const UserPtr& aUser, const string& bundleToken);
	void setBundlePriority(const string& bundleToken, Bundle::Priority p) noexcept;
	void setBundlePriority(BundlePtr aBundle, Bundle::Priority p, bool isAuto=false, bool isQIChange=false) noexcept;
	void setBundleAutoPriority(const string& bundleToken, bool isQIChange=false) noexcept;
	void getBundleSources(BundlePtr aBundle, Bundle::SourceInfoList& sources, Bundle::SourceInfoList& badSources) noexcept;
	void removeBundleSource(const string& bundleToken, const UserPtr& aUser) noexcept;
	void removeBundleSource(BundlePtr aBundle, const UserPtr& aUser) noexcept;
	void removeBundleSources(BundlePtr aBundle) noexcept;
	void getBundleInfo(const string& aSource, BundleList& retBundles, int& finishedFiles, int& fileBundles) { 
		RLock l (cs); 
		bundleQueue.getInfo(aSource, retBundles, finishedFiles, fileBundles); 
	}
	void handleBundleUpdate(const string& bundleToken);

	void removeDir(const string aSource, const BundleList& sourceBundles, bool removeFinished);
	bool move(QueueItemPtr qs, const string& aTarget) noexcept;

	void setBundlePriorities(const string& aSource, const BundleList& sourceBundles, Bundle::Priority p, bool autoPrio=false);
	void calculateBundlePriorities(bool verbose);
	void searchBundle(BundlePtr aBundle, bool manual);

	int getBundleItemCount(const BundlePtr aBundle) noexcept;
	int getFinishedItemCount(const BundlePtr aBundle) noexcept;
	int getDirItemCount(const BundlePtr aBundle, const string& aDir) noexcept;

	/** Move the target location of a queued item. Running items are silently ignored */
	void move(const StringPairList& sourceTargetList) noexcept;
	int isFileQueued(const TTHValue& aTTH, const string& aFile) { RLock l (cs); return fileQueue.isFileQueued(aTTH, aFile); }
	
	bool dropSource(Download* d);

	bool isChunkDownloaded(const TTHValue& tth, int64_t startPos, int64_t& bytes, string& tempTarget);
	
	GETSET(uint64_t, lastSave, LastSave);
	GETSET(uint64_t, lastAutoPrio, LastAutoPrio);

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

	/** QueueItems by target and TTH */
	FileQueue fileQueue;

	/** Bundles by target */
	BundleQueue bundleQueue;

	void shareBundle(const string& aName);
	void runAltSearch();
private:
	friend class QueueLoader;
	friend class Singleton<QueueManager>;
	
	QueueManager();
	~QueueManager();
	
	mutable SharedMutex cs;

	Socket udp;

	/** QueueItems by user */
	UserQueue userQueue;
	/** Directories queued for downloading */
	unordered_multimap<UserPtr, DirectoryItemPtr, User::Hash> directories;
	/** File lists not to delete */
	StringList protectedFileLists;
	/** Sanity check for the target filename */
	static string checkTarget(const string& aTarget, bool checkExsistence, BundlePtr aBundle = NULL) throw(QueueException, FileException);
	/** Add a source to an existing queue item */
	bool addSource(QueueItemPtr qi, const HintedUser& aUser, Flags::MaskType addBad, bool newBundle=false) throw(QueueException, FileException);
	 
	void processList(const string& name, const HintedUser& user, const string& path, int flags);
	void matchTTHList(const string& name, const HintedUser& user, int flags);

	void addBundleUpdate(const BundlePtr aBundle);
	void sendPBD(HintedUser& aUser, const TTHValue& tth, const string& bundleToken);

	void addFinishedItem(const TTHValue& tth, BundlePtr aBundle, const string& aTarget, time_t aSize, int64_t aFinished);

	typedef vector<pair<string, uint64_t>> bundleTickMap;
	bundleTickMap bundleUpdates;

	void load(const SimpleXML& aXml);
	void moveFile(const string& source, const string& target, BundlePtr aBundle = nullptr);
	static void moveFile_(const string& source, const string& target, BundlePtr aBundle);
	void moveStuckFile(QueueItemPtr qi);
	void rechecked(QueueItemPtr qi);
	void onFileHashed(const string& fname, const TTHValue& root, bool failed);
	void hashBundle(BundlePtr aBundle);
	void bundleHashed(BundlePtr aBundle);

	void removeSource(QueueItemPtr qi, const UserPtr& aUser, Flags::MaskType reason, bool removeConn = true) noexcept;

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
	void on(DownloadManagerListener::BundleTick, const BundleList& tickBundles, uint64_t aTick) noexcept;


	template<class T>
	void calculateBalancedPriorities(vector<pair<T, uint8_t>>& priorities, multimap<T, pair<int64_t, double>>& speedSourceMap, bool verbose) {
		if (speedSourceMap.empty())
			return;

		//scale the priorization maps
		double factorSpeed=0, factorSource=0;
		double max = max_element(speedSourceMap.begin(), speedSourceMap.end())->second.first;
		if (max > 0) {
			factorSpeed = 100 / max;
		}

		max = max_element(speedSourceMap.begin(), speedSourceMap.end())->second.second;
		if (max > 0) {
			factorSource = 100 / max;
		}

		multimap<int, T> finalMap;
		int uniqueValues = 0;
		for (auto i = speedSourceMap.begin(); i != speedSourceMap.end(); ++i) {
			auto points = (i->second.first * factorSpeed) + (i->second.second * factorSource);
			if (finalMap.find(points) == finalMap.end()) {
				uniqueValues++;
			}
			finalMap.insert(make_pair(points, i->first));
		}

		int prioGroup = 1;
		if (uniqueValues <= 1) {
			if (verbose) {
				LogManager::getInstance()->message("Not enough items with unique points to perform the priotization!", LogManager::LOG_INFO);
			}
			return;
		} else if (uniqueValues > 2) {
			prioGroup = uniqueValues / 3;
		}

		if (verbose) {
			LogManager::getInstance()->message("Unique values: " + Util::toString(uniqueValues) + " prioGroup size: " + Util::toString(prioGroup), LogManager::LOG_INFO);
		}


		//priority to set (4-2, high-low)
		int8_t prio = 4;

		//counters for analyzing identical points
		int lastPoints = 999;
		int prioSet=0;

		for (auto i = finalMap.begin(); i != finalMap.end(); ++i) {
			if (lastPoints==i->first) {
				if (verbose) {
					LogManager::getInstance()->message(i->second->getTarget() + " points: " + Util::toString(i->first) + " setting prio " + AirUtil::getPrioText(prio), LogManager::LOG_INFO);
				}

				if(i->second->getPriority() != prio)
					priorities.push_back(make_pair(i->second, prio));

				//don't increase the prio if two items have identical points
				if (prioSet < prioGroup) {
					prioSet++;
				}
			} else {
				if (prioSet == prioGroup && prio != 2) {
					prio--;
					prioSet=0;
				} 

				if (verbose) {
					LogManager::getInstance()->message(i->second->getTarget() + " points: " + Util::toString(i->first) + " setting prio " + AirUtil::getPrioText(prio), LogManager::LOG_INFO);
				}

				if(i->second->getPriority() != prio)
					priorities.push_back(make_pair(i->second, prio));

				prioSet++;
				lastPoints=i->first;
			}
		}
	}
};

} // namespace dcpp

#endif // !defined(QUEUE_MANAGER_H)

/**
 * @file
 * $Id: QueueManager.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
