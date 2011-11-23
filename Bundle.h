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

/**
 * A bundle is a set of related files that can be searched for by a single hash value.
 *
 * The hash is defined as follows:
 * For each file in the set, ordered by name (byte-order, not linguistic), except those specially marked,
 * compute the compute the hash. Then calculate the combined hash value by passing the concatenated hashes
 * of each file through the hash function.
 */

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
		FLAG_UPDATE_SINGLEUSER		= 0x04,
		FLAG_SET_WAITING			= 0x08,
		FLAG_HASH_FAILED			= 0x16,
	};


	struct Hash {
		size_t operator()(const BundlePtr x) const { return hash<string>()(x->getToken()); }
	};

	bool operator==(const BundlePtr aBundle) const {
		return token == aBundle->getToken();
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
	typedef unordered_map<CID, string> CIDStringList;
	typedef unordered_map<UserPtr, uint16_t, User::Hash> UserIntMap;
	typedef unordered_map<TTHValue, string> FinishedItemMap;
	typedef pair<HintedUser, uint32_t> UserRunningPair;
	typedef vector<UserRunningPair> SourceIntList;
	typedef unordered_map<string, QueueItemList> DirMap;



	Bundle(const string& target, bool fileBundle, time_t added) : target(target), fileBundle(fileBundle), token(Util::toString(Util::rand())), size(0), downloaded(0), speed(0), lastSpeed(0), 
		running(0), lastPercent(0), singleUser(true), priority(DEFAULT), autoPriority(true), dirty(true), added(added), dirDate(0), simpleMatching(true), recent(false), bytesDownloaded(0),
		hashed(0) { }

	~Bundle();

	GETSET(string, token, Token);
	GETSET(uint16_t, running, Running);
	GETSET(uint64_t, start, Start);
	GETSET(int64_t, size, Size);
	GETSET(int64_t, downloaded, Downloaded);
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

	GETSET(CIDStringList, notifiedUsers, NotifiedUsers);
	GETSET(UserIntMap, runningUsers, RunningUsers);
	GETSET(QueueItemList, queueItems, QueueItems);
	GETSET(QueueItemList, finishedFiles, FinishedFiles);
	GETSET(HintedUserList, uploadReports, UploadReports);
	GETSET(DownloadList, downloads, Downloads);
	GETSET(DirMap, bundleDirs, BundleDirs);
	GETSET(SourceIntList, sources, Sources);

	UserIntMap& getRunningUsers() { return runningUsers; }
	CIDStringList& getNotifiedUsers() { return notifiedUsers; }
	QueueItemList& getFinishedFiles() { return finishedFiles; }
	HintedUserList& getUploadReports() { return uploadReports; }
	QueueItemList& getQueueItems() { return queueItems; }
	DownloadList& getDownloads() { return downloads; }
	DirMap& getBundleDirs() { return bundleDirs; }
	SourceIntList& getBundleSources() { return sources; }
	int getHashed() { return hashed; }

	uint64_t getDownloadedBytes() const { return bytesDownloaded; }
	QueueItem* findQI(const string& aTarget) const;
	Bundle::Priority calculateAutoPriority() const;
	size_t countOnlineUsers() const;

	void removeQueue(QueueItem* qi);

	bool getFileBundle() {
		return fileBundle;
	}

	void increaseHashed() {
		hashed++;
	}

	void increaseSize(int64_t aSize) {
		size += aSize;
	}

	void decreaseSize(int64_t aSize) {
		size -= aSize;
	}

	void increaseDownloadedBytes(int64_t aSize) {
		bytesDownloaded = aSize + downloaded;
	}

	void increaseDownloaded(int64_t aSize) {
		downloaded += aSize;
	}

	void decreaseDownloaded(int64_t aSize) {
		downloaded -= aSize;
	}

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

	/** All queue items indexed by user */
	void getQISources(HintedUserList& l);
	bool isSource(const UserPtr& aUser);
	void getDownloadsQI(DownloadList& l);
	QueueItemList getItems(const UserPtr& aUser) const;
	void addUserQueue(QueueItem* qi);
	bool addUserQueue(QueueItem* qi, const HintedUser& aUser);
	QueueItemPtr getNextQI(const UserPtr& aUser, string aLastError, Priority minPrio = LOWEST, int64_t wantedSize = 0, int64_t lastSpeed = 0, bool smallSlot=false);
	QueueItemList getRunningQIs(const UserPtr& aUser);
	bool addDownload(Download* d);
	int removeDownload(const string& token);

	void removeUserQueue(QueueItem* qi, bool removeRunning = true);
	bool removeUserQueue(QueueItem* qi, const UserPtr& aUser, bool removeRunning = true);

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
