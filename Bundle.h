/*
 * Copyright (C) 2011-2012 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_BUNDLE_H_
#define DCPLUSPLUS_DCPP_BUNDLE_H_

#include <string>
#include <set>

#include "Flags.h"
#include "Pointer.h"
#include "QueueItem.h"
#include "forward.h"
#include "User.h"
#include "GetSet.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

using std::string;

class Bundle : public Flags, public intrusive_ptr_base<Bundle> {
public:

	enum Priority {
		DEFAULT = -1,
		PAUSED = 0,
		LOWEST,
		LOW,
		NORMAL,
		HIGH,
		HIGHEST,
		LAST
	};

	enum Flags {
		/** Flags for scheduled actions */
		FLAG_UPDATE_SIZE			= 0x01,
		FLAG_UPDATE_NAME			= 0x02,
		FLAG_SCHEDULE_SEARCH		= 0x04,
		/** The bundle is currently being hashed */
		FLAG_HASH					= 0x40,
		/** Missing/extra files have been found or it has failed to hash */
		FLAG_SHARING_FAILED			= 0x100,
		/** Not added into bundleQueue yet */
		FLAG_NEW					= 0x200,
		/** Autodrop slow sources is enabled for this bundle */
		FLAG_AUTODROP				= 0x400
	};

	enum SourceInfo {
		SOURCE_USER					= 0,
		SOURCE_SIZE					= 1,
		SOURCE_FILES				= 2,
	};

	struct Hash {
		size_t operator()(const BundlePtr x) const { return hash<string>()(x->getToken()); }
	};

	struct SortOrder {
		bool operator()(const BundlePtr left, const BundlePtr right) const {
			if (left->getPriority() == right->getPriority()) {
				return left->getAdded() < right->getAdded();
			} else {
				return left->getPriority() > right->getPriority();
			}
		}
	};

	typedef boost::unordered_map<string, BundlePtr> StringBundleMap;
	typedef vector<pair<string, BundlePtr>> StringBundleList;

	typedef boost::unordered_map<UserPtr, uint16_t, User::Hash> UserIntMap;
	typedef tuple<HintedUser, uint64_t, uint32_t> SourceTuple;
	typedef vector<SourceTuple> SourceInfoList;
	typedef pair<HintedUser, string> UserBundlePair;
	typedef vector<UserBundlePair> FinishedNotifyList;
	typedef boost::unordered_map<string, pair<uint32_t, uint32_t>> DirMap;


	typedef vector<pair<QueueItemPtr, int8_t>> PrioList;
	typedef multimap<double, BundlePtr> SourceSpeedMapB;
	typedef multimap<double, QueueItemPtr> SourceSpeedMapQI;


	Bundle(const string& target, time_t added, Priority aPriority, time_t aDirDate=0) noexcept;
	Bundle(QueueItemPtr qi, const string& aToken = Util::toString(Util::rand())) noexcept;
	~Bundle();

	GETSET(string, token, Token);
	GETSET(uint16_t, running, Running);
	GETSET(uint64_t, start, Start);
	GETSET(int64_t, size, Size);
	GETSET(int64_t, speed, Speed);
	GETSET(int64_t, actual, Actual);
	GETSET(int64_t, lastSpeed, LastSpeed);
	GETSET(double, lastDownloaded, LastDownloaded);
	GETSET(Priority, priority, Priority);
	GETSET(bool, autoPriority, AutoPriority);
	GETSET(time_t, added, Added);
	GETSET(time_t, dirDate, DirDate);
	GETSET(bool, singleUser, SingleUser);
	GETSET(bool, simpleMatching, SimpleMatching);
	GETSET(bool, seqOrder, SeqOrder);

	GETSET(FinishedNotifyList, finishedNotifications, FinishedNotifications);
	GETSET(UserIntMap, runningUsers, RunningUsers);
	GETSET(QueueItemList, queueItems, QueueItems);
	GETSET(QueueItemList, finishedFiles, FinishedFiles);
	GETSET(HintedUserList, uploadReports, UploadReports);
	GETSET(DownloadList, downloads, Downloads);
	GETSET(DirMap, bundleDirs, BundleDirs);
	GETSET(SourceInfoList, badSources, BadSources);
	GETSET(SourceInfoList, sources, Sources);

	UserIntMap& getRunningUsers() { return runningUsers; }
	FinishedNotifyList& getNotifiedUsers() { return finishedNotifications; }
	QueueItemList& getFinishedFiles() { return finishedFiles; }
	HintedUserList& getUploadReports() { return uploadReports; }
	QueueItemList& getQueueItems() { return queueItems; }
	DownloadList& getDownloads() { return downloads; }
	DirMap& getBundleDirs() { return bundleDirs; }
	SourceInfoList& getBundleSources() { return sources; }
	SourceInfoList& getBadSources() { return badSources; }

	/* Misc */
	bool isFileBundle() { return fileBundle;}

	int64_t getDownloadedBytes() const { return currentDownloaded+finishedSegments; }
	int64_t getSecondsLeft();

	string getTarget() { return target; }
	string getName();

	string getBundleFile();
	void deleteBundleFile();

	void setDirty(bool dirty);
	bool getDirty() { return dirty; }
	bool checkRecent();
	bool isRecent() { return recent; }

	tstring getBundleText();

	/* QueueManager */
	void save();
	bool removeQueue(QueueItemPtr qi, bool finished) noexcept;
	bool addQueue(QueueItemPtr qi) noexcept;

	void getDirQIs(const string& aDir, QueueItemList& ql) noexcept;
	void getDownloadsQI(DownloadList& l) noexcept;

	bool addFinishedItem(QueueItemPtr qi, bool finished) noexcept;
	bool removeFinishedItem(QueueItemPtr qi) noexcept;
	void finishBundle() noexcept;

	void sendRemovePBD(const UserPtr& aUser) noexcept;
	bool isFinishedNotified(const UserPtr& aUser) noexcept;
	void addFinishedNotify(HintedUser& aUser, const string& remoteBundle) noexcept;
	void removeFinishedNotify(const UserPtr& aUser) noexcept;

	pair<string, pair<uint32_t, uint32_t>> getDirByRelease(const string& aDir) noexcept;
	string getMatchPath(const string& aRemoteFile, const string& aLocalFile, bool nmdc) noexcept;
	QueueItemPtr findQI(const string& aTarget) const noexcept;
	size_t countOnlineUsers() const noexcept;

	Priority calculateProgressPriority() const;
	multimap<QueueItemPtr, pair<int64_t, double>> getQIBalanceMaps() noexcept;
	pair<int64_t, double> getPrioInfo() noexcept;

	void increaseSize(int64_t aSize) { size += aSize; }
	void decreaseSize(int64_t aSize) { size -= aSize; }

	bool allFilesHashed();

	void setTarget(const string& aTarget);

	int64_t getDiskUse(bool countAll);

	void addSegment(int64_t aSize, bool downloaded) noexcept;
	void removeDownloadedSegment(int64_t aSize);

	/* DownloadManager */
	bool addRunningUser(const UserConnection* aSource) noexcept;
	bool removeRunningUser(const UserConnection* aSource, bool sendRemove) noexcept;
	void setBundleMode(bool setSingleUser) noexcept;

	void sendSizeNameUpdate() noexcept;

	void addDownload(Download* d) noexcept;
	void removeDownload(Download* d) noexcept;

	void getTTHList(OutputStream& tthList) noexcept;
	void getSearchItems(StringPairList& searches, bool manual) noexcept;
	void updateSearchMode();
	bool allowAutoSearch();

	bool onDownloadTick(vector<pair<CID, AdcCommand>>& UBNList) noexcept;
	string formatDownloaded(int64_t aBytes);

	void setDownloadedBytes(int64_t aSize) noexcept;

	void increaseRunning() { running++; }
	void decreaseRunning() { running--; }

	/* Sources*/
	void getSources(HintedUserList& l) noexcept;
	bool isSource(const UserPtr& aUser) noexcept;
	bool isBadSource(const UserPtr& aUser) noexcept;
	bool isFinished() { return queueItems.empty(); }
	void removeBadSource(const HintedUser& aUser) noexcept;

	/** All queue items indexed by user */
	void addUserQueue(QueueItemPtr qi) noexcept;
	bool addUserQueue(QueueItemPtr qi, const HintedUser& aUser) noexcept;
	QueueItemPtr getNextQI(const UserPtr& aUser, string aLastError, Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap) noexcept;
	QueueItemList getRunningQIs(const UserPtr& aUser) noexcept;
	void getItems(const UserPtr& aUser, QueueItemList& ql) noexcept;

	void removeUserQueue(QueueItemPtr qi) noexcept;
	bool removeUserQueue(QueueItemPtr qi, const UserPtr& aUser, bool addBad) noexcept;

	boost::unordered_map<UserPtr, QueueItemList, User::Hash>& getList(size_t i)  { return userQueue[i]; }
	boost::unordered_map<UserPtr, QueueItemList, User::Hash>& getRunningMap()  { return runningItems; }
private:
	int64_t finishedSegments;
	int64_t currentDownloaded; //total downloaded for the running downloads
	string target;
	bool fileBundle;
	bool dirty;
	bool recent;

	/** QueueItems by priority and user (this is where the download order is determined) */
	boost::unordered_map<UserPtr, QueueItemList, User::Hash> userQueue[LAST];
	/** Currently running downloads, a QueueItem is always either here or in the userQueue */
	boost::unordered_map<UserPtr, QueueItemList, User::Hash> runningItems;
};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLE_H_ */
