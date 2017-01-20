/*
 * Copyright (C) 2011-2016 AirDC++ Project
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

#include "HintedUser.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "QueueItem.h"
#include "User.h"

#include "QueueItemBase.h"

namespace dcpp {

using std::string;

#define DIR_BUNDLE_VERSION "2"
#define FILE_BUNDLE_VERSION "2"

class Bundle : public QueueItemBase, public intrusive_ptr_base<Bundle> {
public:
	enum BundleFlags {
		/** Flags for scheduled actions */
		FLAG_UPDATE_SIZE			= 0x01,
		FLAG_SCHEDULE_SEARCH		= 0x04,
		/** Autodrop slow sources is enabled for this bundle */
		FLAG_AUTODROP				= 0x400,
	};

	enum Status {
		STATUS_NEW, // not added in queue yet
		STATUS_QUEUED,
		STATUS_DOWNLOAD_ERROR,
		STATUS_RECHECK,
		STATUS_DOWNLOADED, // no queued files
		STATUS_VALIDATION_RUNNING, // the bundle is being validated by the completion hooks
		STATUS_VALIDATION_ERROR, // hook validation failed (see the error pointer for more information)
		STATUS_COMPLETED, // no validation errors, ready for sharing
		STATUS_SHARED
	};

	class BundleSource : public Flags {
	public:
		BundleSource(const HintedUser& aUser, int64_t aSize, Flags::MaskType aFlags = 0) : user(aUser), size(aSize), Flags(aFlags) { }

		bool operator==(const UserPtr& aUser) const noexcept { return user == aUser; }

		GETSET(HintedUser, user, User);
		int64_t size;
		int files = 1;
	};

	class HasStatus {
	public:
		HasStatus(Status aStatus) : status(aStatus) { }
		bool operator()(const BundlePtr& aBundle) const noexcept { return aBundle->getStatus() == status; }
	private:
		Status status;
	};

	struct StatusOrder {
		bool operator()(const BundlePtr& left, const BundlePtr& right) const noexcept {
			return left->getStatus() > right->getStatus();
		}
	};

	struct Hash {
		size_t operator()(const BundlePtr& x) const noexcept { return hash<QueueToken>()(x->getToken()); }
	};

	struct SortOrder {
		bool operator()(const BundlePtr& left, const BundlePtr& right) const noexcept {
			if (left->getPriority() == right->getPriority()) {
				return left->getTimeAdded() < right->getTimeAdded();
			} else {
				return left->getPriority() > right->getPriority();
			}
		}
	};

	typedef unordered_map<QueueToken, BundlePtr> TokenMap;
	typedef unordered_set<BundlePtr, Bundle::Hash> Set;

	typedef unordered_map<UserPtr, uint16_t, User::Hash> UserIntMap;
	typedef vector<BundleSource> SourceList;
	typedef vector<pair<BundlePtr, BundleSource>> SourceBundleList;

	typedef pair<HintedUser, string> UserBundlePair;
	typedef vector<UserBundlePair> FinishedNotifyList;

	typedef multimap<double, BundlePtr> SourceSpeedMapB;
	typedef multimap<double, QueueItemPtr> SourceSpeedMapQI;


	Bundle(const string& target, time_t added, Priority aPriority, time_t aDirDate=0, QueueToken aToken = 0, bool aDirty = true, bool isFileBundle = false) noexcept;
	Bundle(QueueItemPtr& qi, time_t aBundleDate, QueueToken aToken = 0, bool aDirty = true) noexcept;
	~Bundle() noexcept;

	IGETSET(ActionHookErrorPtr, hookError, HookError, nullptr);
	GETSET(string, error, Error);

	IGETSET(Status, status, Status, STATUS_NEW);
	IGETSET(time_t, bundleDate, BundleDate, 0);				// the file/directory modify date picked from the remote filelist when the bundle has been queued
	IGETSET(uint64_t, start, Start, 0);						// time that is being reset every time when a waiting the bundle gets running downloads
	IGETSET(time_t, lastSearch, LastSearch, 0);				// last time when the bundle was searched for
	IGETSET(bool, seqOrder, SeqOrder, false);				// using an alphabetical downloading order for files (not enabled by default for fresh bundles)

	IGETSET(bool, singleUser, SingleUser, true);		// the bundle is downloaded from a single user (may have multiple connections)

	IGETSET(int64_t, actual, Actual, 0); 
	IGETSET(int64_t, speed, Speed, 0);					// the speed calculated on every second in downloadmanager
	IGETSET(bool, addedByAutoSearch, AddedByAutoSearch, false);		// the bundle was added by auto search
	IGETSET(time_t, resumeTime, ResumeTime, 0);						//Time for bundle to be resumed when paused for x

	GETSET(QueueItemList, queueItems, QueueItems);
	GETSET(QueueItemList, finishedFiles, FinishedFiles);
	GETSET(SourceList, badSources, BadSources);
	GETSET(SourceList, sources, Sources);

	QueueItemList& getFinishedFiles() { return finishedFiles; }
	QueueItemList& getQueueItems() { return queueItems; }

	const FinishedNotifyList& getFinishedNotifications() const noexcept  { return finishedNotifications; }

	/* Misc */
	bool isFileBundle() const noexcept { return fileBundle; }

	int64_t getDownloadedBytes() const noexcept { return currentDownloaded + finishedSegments; }
	int64_t getSecondsLeft() const noexcept;

	const string& getTarget() const noexcept { return target; }
	string getName() const noexcept;

	string getXmlFilePath() const noexcept;
	void deleteXmlFile() noexcept;

	void setDirty() noexcept;
	bool getDirty() const noexcept;
	bool checkRecent() noexcept;
	bool isRecent() const noexcept { return recent; }

	/* QueueManager */
	static bool isFailedStatus(Status aStatus) noexcept;
	bool isFailed() const noexcept;

	// Throws on errors
	void save();

	void addQueue(QueueItemPtr& qi) noexcept;
	void removeQueue(QueueItemPtr& qi, bool aFinished) noexcept;

	void getDirQIs(const string& aDir, QueueItemList& ql) const noexcept;

	void addFinishedItem(QueueItemPtr& qi, bool finished) noexcept;
	void removeFinishedItem(QueueItemPtr& qi) noexcept;
	void finishBundle() noexcept;

	// All files have been downloaded and moved to the final destination
	// Unsafe
	bool filesCompleted() const noexcept;

	// All bundles files have finished downloading
	// Safe
	bool isDownloaded() const noexcept;

	// All bundles files have finished downloading and all validation hooks have completed
	// Safe
	bool isCompleted() const noexcept;

	void clearFinishedNotifications(FinishedNotifyList& fnl) noexcept;
	bool isFinishedNotified(const UserPtr& aUser) const noexcept;
	void addFinishedNotify(HintedUser& aUser, const string& remoteBundle) noexcept;
	void removeFinishedNotify(const UserPtr& aUser) noexcept;

	QueueItemPtr findQI(const string& aTarget) const noexcept;
	int countOnlineUsers() const noexcept;

	Priority calculateProgressPriority() const noexcept;
	multimap<QueueItemPtr, pair<int64_t, double>> getQIBalanceMaps() noexcept;
	pair<int64_t, double> getPrioInfo() noexcept;

	void increaseSize(int64_t aSize) noexcept;
	void decreaseSize(int64_t aSize) noexcept;

	void addFinishedSegment(int64_t aSize) noexcept;
	void removeFinishedSegment(int64_t aSize) noexcept;

	string getStatusString() const noexcept;

	/* DownloadManager */
	int countConnections() const noexcept;
	const UserIntMap& getRunningUsers() const noexcept { return runningUsers; }

	bool addRunningUser(const UserConnection* aSource) noexcept;
	bool removeRunningUser(const UserConnection* aSource, bool sendRemove) noexcept;
	void setUserMode(bool setSingleUser) noexcept;

	void sendSizeUpdate() noexcept;

	void addDownload(Download* d) noexcept;
	void removeDownload(Download* d) noexcept;

	bool allowAutoSearch() const noexcept;

	bool onDownloadTick(vector<pair<CID, AdcCommand>>& UBNList) noexcept;

	void setDownloadedBytes(int64_t aSize) noexcept;

	/* Sources*/
	void getSourceUsers(HintedUserList& l) const noexcept;
	bool isSource(const UserPtr& aUser) const noexcept;
	bool isBadSource(const UserPtr& aUser) const noexcept;

	/** All queue items indexed by user */
	void addUserQueue(QueueItemPtr& qi) noexcept;
	bool addUserQueue(QueueItemPtr& qi, const HintedUser& aUser, bool isBad = false) noexcept;
	QueueItemPtr getNextQI(const UserPtr& aUser, const OrderedStringSet& onlineHubs, string& aLastError, Priority minPrio, int64_t wantedSize, int64_t lastSpeed, QueueItemBase::DownloadType aType, bool allowOverlap) noexcept;
	void getItems(const UserPtr& aUser, QueueItemList& ql) const noexcept;

	void removeUserQueue(QueueItemPtr& qi) noexcept;
	bool removeUserQueue(QueueItemPtr& qi, const UserPtr& aUser, Flags::MaskType reason) noexcept;

	//moves the file back in userqueue for the given user (only within the same priority)
	void rotateUserQueue(QueueItemPtr& qi, const UserPtr& aUser) noexcept;
	bool isEmpty() const noexcept { return queueItems.empty() && finishedFiles.empty(); }
private:
	int64_t lastSpeed = 0; // the speed sent on last time to UBN sources
	int64_t lastDownloaded = 0; // the progress percent sent on last time to UBN sources

	int64_t finishedSegments = 0;
	int64_t currentDownloaded = 0; //total downloaded for the running downloads
	bool fileBundle = false;
	bool dirty = false;
	bool recent = false;

	/** QueueItems by priority and user (this is where the download order is determined) */
	unordered_map<UserPtr, deque<QueueItemPtr>, User::Hash> userQueue[static_cast<int>(Priority::LAST)];
	/** Currently running downloads, a QueueItem is always either here or in the userQueue */
	unordered_map<UserPtr, QueueItemList, User::Hash> runningItems;

	UserIntMap runningUsers;					// running users and their connections cached
	HintedUserList uploadReports;				// sources receiving UBN notifications (running only)
	FinishedNotifyList finishedNotifications;	// partial bundle sharing sources (mapped to their local tokens)
};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLE_H_ */
