/*
 * Copyright (C) 2011-2013 AirDC++ Project
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

#include "HashValue.h"
#include "TigerHash.h"
#include "HintedUser.h"
#include "Pointer.h"
#include "QueueItem.h"
#include "User.h"

#include "QueueItemBase.h"

namespace dcpp {

using std::string;

struct BundleFileInfo {
	BundleFileInfo(const string& aFile, const TTHValue& aTTH, int64_t aSize, time_t aDate = 0, QueueItemBase::Priority aPrio = QueueItemBase::DEFAULT) : file(aFile), tth(aTTH), size(aSize), prio(aPrio), date(aDate) { }

	string file;
	TTHValue tth;
	int64_t size;
	QueueItemBase::Priority prio;
	time_t date;
};

class Bundle : public QueueItemBase, public intrusive_ptr_base<Bundle> {
public:
	enum Flags {
		/** Flags for scheduled actions */
		FLAG_UPDATE_SIZE			= 0x01,
		FLAG_UPDATE_NAME			= 0x02,
		FLAG_SCHEDULE_SEARCH		= 0x04,
		/** Autodrop slow sources is enabled for this bundle */
		FLAG_AUTODROP				= 0x400
	};

	enum Status {
		STATUS_NEW, // not added in queue yet
		STATUS_QUEUED,
		STATUS_FINISHED,
		STATUS_FAILED_MISSING,
		STATUS_FAILED_EXTRAS,
		STATUS_HASHING,
		STATUS_HASH_FAILED,
		STATUS_SHARED
	};

	struct BundleSource {
		BundleSource(const HintedUser& aUser, int64_t aSize) : user(aUser), size(aSize), files(1) { }

		bool operator==(const UserPtr& aUser) const { return user == aUser; }

		int64_t size;
		int files;
		HintedUser user;
	};

	class HasStatus {
	public:
		HasStatus(Status aStatus) : status(aStatus) { }
		bool operator()(const BundlePtr& aBundle) { return aBundle->getStatus() == status; }
	private:
		Status status;
	};

	struct StatusOrder {
		bool operator()(const BundlePtr& left, const BundlePtr& right) const {
			return left->getStatus() > right->getStatus();
		}
	};

	struct Hash {
		size_t operator()(const BundlePtr& x) const { return hash<string>()(x->getToken()); }
	};

	struct SortOrder {
		bool operator()(const BundlePtr& left, const BundlePtr& right) const {
			if (left->getPriority() == right->getPriority()) {
				return left->getAdded() < right->getAdded();
			} else {
				return left->getPriority() > right->getPriority();
			}
		}
	};

	typedef unordered_map<string, BundlePtr> StringBundleMap;
	typedef unordered_multimap<string, pair<string, BundlePtr>, noCaseStringHash, noCaseStringEq> BundleDirMap;
	typedef vector<pair<string, BundlePtr>> StringBundleList;

	typedef unordered_map<UserPtr, uint16_t, User::Hash> UserIntMap;
	typedef vector<BundleSource> SourceList;
	typedef vector<pair<BundlePtr, BundleSource>> SourceBundleList;

	typedef pair<HintedUser, string> UserBundlePair;
	typedef vector<UserBundlePair> FinishedNotifyList;
	typedef unordered_map<string, pair<uint32_t, uint32_t>> DirMap;

	typedef multimap<double, BundlePtr> SourceSpeedMapB;
	typedef multimap<double, QueueItemPtr> SourceSpeedMapQI;


	Bundle(const string& target, time_t added, Priority aPriority, time_t aDirDate=0, const string& aToken = Util::emptyString, bool aDirty = true, bool isFileBundle = false) noexcept;
	Bundle(QueueItemPtr& qi, const string& aToken = Util::emptyString, bool aDirty = true) noexcept;
	~Bundle();

	GETSET(Status, status, Status);
	GETSET(string, token, Token);
	GETSET(uint64_t, start, Start);
	GETSET(time_t, lastSearch, LastSearch);			// last time when the bundle was searched for
	GETSET(time_t, dirDate, DirDate);				// the directory modify date picked from the remote filelist when the bundle has been queued
	GETSET(bool, simpleMatching, SimpleMatching);	// the directory structure is simple enough for matching partial lists with subdirs cut from the path
	GETSET(bool, seqOrder, SeqOrder);				// using an alphabetical downloading order for files (not enabled by default for fresh bundles)

	GETSET(uint16_t, running, Running);				// number of running users
	GETSET(uint64_t, bundleBegin, BundleBegin);		// time that is being reset every time when a waiting the bundle gets running downloads (GUI purposes really)
	GETSET(bool, singleUser, SingleUser);			// the bundle is downloaded from a single user (may have multiple connections)

	GETSET(int64_t, actual, Actual); 
	GETSET(int64_t, speed, Speed);					// the speed calculated on every second in downloadmanager
	//GETSET(int, transferFlags, TransferFlags);		// combined transfer flags checked from running downloads

	GETSET(int64_t, lastSpeed, LastSpeed); // the speed sent on last time to UBN sources
	GETSET(double, lastDownloaded, LastDownloaded); // the progress percent sent on last time to UBN sources


	GETSET(FinishedNotifyList, finishedNotifications, FinishedNotifications);	// partial bundle sharing sources (mapped to their local tokens)
	GETSET(UserIntMap, runningUsers, RunningUsers);			// running users and their connections cached
	GETSET(QueueItemList, queueItems, QueueItems);
	GETSET(QueueItemList, finishedFiles, FinishedFiles);
	GETSET(HintedUserList, uploadReports, UploadReports);	 // sources receiving UBN notifications (running only)
	GETSET(DirMap, bundleDirs, BundleDirs);
	GETSET(SourceList, badSources, BadSources);
	GETSET(SourceList, sources, Sources);

	UserIntMap& getRunningUsers() { return runningUsers; }
	FinishedNotifyList& getNotifiedUsers() { return finishedNotifications; }
	QueueItemList& getFinishedFiles() { return finishedFiles; }
	HintedUserList& getUploadReports() { return uploadReports; }
	QueueItemList& getQueueItems() { return queueItems; }
	DirMap& getBundleDirs() { return bundleDirs; }
	SourceList& getBundleSources() { return sources; }
	SourceList& getBadSources() { return badSources; }

	/* Misc */
	bool isFileBundle() const { return fileBundle;}

	int64_t getDownloadedBytes() const { return currentDownloaded+finishedSegments; }
	int64_t getSecondsLeft() const;

	string getTarget() { return target; }
	string getName() const;

	string getBundleFile() const;
	void deleteBundleFile();

	void setDirty();
	bool getDirty() const;
	bool checkRecent();
	bool isRecent() const { return recent; }

	tstring getBundleText();

	/* QueueManager */
	bool isFailed() const;
	void save();
	bool removeQueue(QueueItemPtr& qi, bool finished) noexcept;
	bool addQueue(QueueItemPtr& qi) noexcept;

	void getDirQIs(const string& aDir, QueueItemList& ql) const noexcept;

	bool addFinishedItem(QueueItemPtr& qi, bool finished) noexcept;
	bool removeFinishedItem(QueueItemPtr& qi) noexcept;
	void finishBundle() noexcept;
	bool allowHash() const;

	void clearFinishedNotifications(FinishedNotifyList& fnl) noexcept;
	bool isFinishedNotified(const UserPtr& aUser) const noexcept;
	void addFinishedNotify(HintedUser& aUser, const string& remoteBundle) noexcept;
	void removeFinishedNotify(const UserPtr& aUser) noexcept;

	pair<uint32_t, uint32_t> getPathInfo(const string& aDir) const noexcept;
	string getMatchPath(const string& aRemoteFile, const string& aLocalFile, bool nmdc) const noexcept;
	QueueItemPtr findQI(const string& aTarget) const noexcept;
	int countOnlineUsers() const noexcept;

	Priority calculateProgressPriority() const;
	multimap<QueueItemPtr, pair<int64_t, double>> getQIBalanceMaps() noexcept;
	pair<int64_t, double> getPrioInfo() noexcept;

	void increaseSize(int64_t aSize);
	void decreaseSize(int64_t aSize);

	void setTarget(const string& aTarget);

	void addFinishedSegment(int64_t aSize) noexcept;
	void removeDownloadedSegment(int64_t aSize);

	/* DownloadManager */
	bool addRunningUser(const UserConnection* aSource) noexcept;
	bool removeRunningUser(const UserConnection* aSource, bool sendRemove) noexcept;
	void setBundleMode(bool setSingleUser) noexcept;

	void sendSizeNameUpdate() noexcept;

	void addDownload(Download* d) noexcept;
	void removeDownload(Download* d) noexcept;

	void getSearchItems(map<string, QueueItemPtr>& searches, bool manual) const noexcept;
	void updateSearchMode();
	bool allowAutoSearch() const;

	bool onDownloadTick(vector<pair<CID, AdcCommand>>& UBNList) noexcept;

	void setDownloadedBytes(int64_t aSize) noexcept;

	void increaseRunning() { running++; }
	void decreaseRunning() { running--; }

	/* Sources*/
	void getSources(HintedUserList& l) const noexcept;
	bool isSource(const UserPtr& aUser) const noexcept;
	bool isBadSource(const UserPtr& aUser) const noexcept;
	bool isFinished() const { return queueItems.empty(); }

	/** All queue items indexed by user */
	void addUserQueue(QueueItemPtr& qi) noexcept;
	bool addUserQueue(QueueItemPtr& qi, const HintedUser& aUser, bool isBad = false) noexcept;
	QueueItemPtr getNextQI(const UserPtr& aUser, const OrderedStringSet& onlineHubs, string& aLastError, Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap) noexcept;
	void getItems(const UserPtr& aUser, QueueItemList& ql) const noexcept;

	void removeUserQueue(QueueItemPtr& qi) noexcept;
	bool removeUserQueue(QueueItemPtr& qi, const UserPtr& aUser, bool addBad) noexcept;

	//moves the file back in userqueue for the given user (only within the same priority)
	void rotateUserQueue(QueueItemPtr& qi, const UserPtr& aUser) noexcept;
private:
	int64_t finishedSegments;
	int64_t currentDownloaded; //total downloaded for the running downloads
	bool fileBundle;
	bool dirty;
	bool recent;

	/** QueueItems by priority and user (this is where the download order is determined) */
	unordered_map<UserPtr, deque<QueueItemPtr>, User::Hash> userQueue[LAST];
	/** Currently running downloads, a QueueItem is always either here or in the userQueue */
	unordered_map<UserPtr, QueueItemList, User::Hash> runningItems;
};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLE_H_ */
