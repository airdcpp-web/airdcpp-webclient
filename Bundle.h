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

#ifndef DCPLUSPLUS_DCPP_BUNDLE_H_
#define DCPLUSPLUS_DCPP_BUNDLE_H_

#include <string>
#include <set>

#include "Flags.h"
#include "Pointer.h"
#include "QueueItem.h"
#include "forward.h"
#include "User.h"

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
		FLAG_UPDATE_SIZE			= 0x01,
		FLAG_UPDATE_NAME			= 0x02,
		FLAG_HASH					= 0x04,
		FLAG_HASH_FAILED			= 0x08,
		FLAG_SCAN_FAILED			= 0x10
	};


	struct Hash {
		size_t operator()(const BundlePtr x) const { return hash<string>()(x->getToken()); }
	};

	bool operator==(const BundlePtr aBundle) const {
		return compare(token, aBundle->getToken()) == 0;
	}

	struct SortOrder {
		bool operator()(const BundlePtr left, const BundlePtr right) const {
			if (left->getPriority() == right->getPriority()) {
				return left->getAdded() < right->getAdded();
			} else {
				return left->getPriority() > right->getPriority();
			}
		}
	};

	typedef QueueItem* Ptr;
	typedef unordered_map<UserPtr, uint16_t, User::Hash> UserIntMap;
	typedef pair<HintedUser, uint32_t> UserRunningPair;
	typedef vector<UserRunningPair> SourceIntList;
	typedef pair<HintedUser, string> UserBundlePair;
	typedef vector<UserBundlePair> FinishedNotifyList;
	typedef unordered_map<string, QueueItemList> DirMap;
	typedef unordered_map<string, BundlePtr> BundleTokenMap;


	typedef vector<pair<QueueItem*, int8_t>> PrioList;
	typedef multimap<double, BundlePtr> SourceSpeedMapB;
	typedef multimap<double, QueueItem*> SourceSpeedMapQI;


	Bundle(const string& target, bool fileBundle, time_t added) : target(target), fileBundle(fileBundle), token(Util::toString(Util::rand())), size(0), downloadedSegments(0), speed(0), lastSpeed(0), 
		running(0), lastPercent(0), singleUser(true), priority(DEFAULT), autoPriority(true), dirty(true), added(added), dirDate(0), simpleMatching(true), recent(false), bytesDownloaded(0),
		hashed(0) { }

	~Bundle();

	GETSET(string, token, Token);
	GETSET(uint16_t, running, Running);
	GETSET(uint64_t, start, Start);
	GETSET(int64_t, size, Size);
	GETSET(int64_t, downloadedSegments, DownloadedSegments);
	GETSET(int64_t, speed, Speed);
	GETSET(int64_t, actual, Actual);
	GETSET(int64_t, lastSpeed, LastSpeed);
	GETSET(double, lastPercent, LastPercent);
	GETSET(Priority, priority, Priority);
	GETSET(bool, autoPriority, AutoPriority);
	GETSET(time_t, added, Added);
	GETSET(time_t, dirDate, DirDate);
	GETSET(bool, singleUser, SingleUser);
	GETSET(bool, simpleMatching, SimpleMatching);
	GETSET(bool, recent, Recent);

	GETSET(FinishedNotifyList, finishedNotifications, FinishedNotifications);
	GETSET(UserIntMap, runningUsers, RunningUsers);
	GETSET(QueueItemList, queueItems, QueueItems);
	GETSET(QueueItemList, finishedFiles, FinishedFiles);
	GETSET(HintedUserList, uploadReports, UploadReports);
	GETSET(DownloadList, downloads, Downloads);
	GETSET(DirMap, bundleDirs, BundleDirs);
	GETSET(SourceIntList, badSources, BadSources);
	GETSET(SourceIntList, sources, Sources);

	UserIntMap& getRunningUsers() { return runningUsers; }
	FinishedNotifyList& getNotifiedUsers() { return finishedNotifications; }
	QueueItemList& getFinishedFiles() { return finishedFiles; }
	HintedUserList& getUploadReports() { return uploadReports; }
	QueueItemList& getQueueItems() { return queueItems; }
	DownloadList& getDownloads() { return downloads; }
	DirMap& getBundleDirs() { return bundleDirs; }
	SourceIntList& getBundleSources() { return sources; }
	int getHashed() { return hashed; }

	uint64_t getDownloadedBytes() const { return bytesDownloaded; }
	QueueItem* findQI(const string& aTarget) const;
	size_t countOnlineUsers() const;

	void removeQueue(QueueItem* qi);
	void addQueue(QueueItem* qi);

	bool getFileBundle() {
		return fileBundle;
	}

	void resetHashed() { hashed = 0; }
	void increaseHashed() {
		hashed++;
	}

	void increaseSize(int64_t aSize) {
		size += aSize;
	}

	void decreaseSize(int64_t aSize) {
		size -= aSize;
	}

	void setDownloadedBytes(int64_t aSize);
	void addDownloadedSegment(int64_t aSize);
	void removeDownloadedSegment(int64_t aSize);

	void increaseRunning() {
		running++;
	}

	void decreaseRunning() {
		running--;
	}

	uint64_t getSecondsLeft();


	string getTarget() {
		return target;
	}

	string getBundleFile();

	void setTarget(string targetNew) {
		target =  targetNew;
	}

	string getName();
	void setDirty(bool enable);

	bool getDirty() {
		return dirty;
	}

	tstring getBundleText();
	bool isFinishedNotified(const UserPtr& aUser);
	void addFinishedNotify(HintedUser& aUser, const string& remoteBundle);
	void removeFinishedNotify(const UserPtr& aUser);

	/** All queue items indexed by user */
	void getQISources(HintedUserList& l);
	bool isSource(const UserPtr& aUser);
	bool isSource(const CID& cid);
	bool isBadSource(const UserPtr& aUser);
	bool isFinished() { return queueItems.empty(); }
	void getDownloadsQI(DownloadList& l);
	QueueItemList getItems(const UserPtr& aUser) const;
	void addUserQueue(QueueItem* qi);
	bool addUserQueue(QueueItem* qi, const HintedUser& aUser);
	QueueItemPtr getNextQI(const UserPtr& aUser, string aLastError, Priority minPrio = LOWEST, int64_t wantedSize = 0, int64_t lastSpeed = 0, bool smallSlot=false);
	QueueItemList getRunningQIs(const UserPtr& aUser);
	void addDownload(Download* d);
	void removeDownload(const string& token);

	void removeBadSource(const HintedUser& aUser);

	Priority calculateProgressPriority() const;

	void getQIBalanceMaps(SourceSpeedMapQI& speedMap, SourceSpeedMapQI& sourceMap);
	void getBundleBalanceMaps(SourceSpeedMapB& speedMap, SourceSpeedMapB& sourceMap);

	void calculateBalancedPriorities(PrioList& priorities, SourceSpeedMapQI& speeds, SourceSpeedMapQI& sources, bool verbose);

	void removeUserQueue(QueueItem* qi);
	bool removeUserQueue(QueueItem* qi, const UserPtr& aUser, bool addBad);

	boost::unordered_map<UserPtr, QueueItemList, User::Hash>& getList(size_t i)  { return userQueue[i]; }
	boost::unordered_map<UserPtr, QueueItemList, User::Hash>& getRunningMap()  { return runningItems; }
private:
	uint64_t bytesDownloaded;
	string target;
	bool fileBundle;
	bool dirty;
	int hashed;

	/** QueueItems by priority and user (this is where the download order is determined) */
	boost::unordered_map<UserPtr, QueueItemList, User::Hash> userQueue[LAST];
	/** Currently running downloads, a QueueItem is always either here or in the userQueue */
	boost::unordered_map<UserPtr, QueueItemList, User::Hash> runningItems;
};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLE_H_ */
