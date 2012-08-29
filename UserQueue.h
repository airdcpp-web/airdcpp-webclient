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

#ifndef DCPLUSPLUS_DCPP_USER_QUEUE_H
#define DCPLUSPLUS_DCPP_USER_QUEUE_H

#include "forward.h"
#include "typedefs.h"
#include "HintedUser.h"
#include "QueueItem.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

/** All queue items indexed by user (this is a cache for the FileQueue really...) */
class UserQueue {
public:
	void addQI(QueueItemPtr qi, bool newBundle=false);
	void addQI(QueueItemPtr qi, const HintedUser& aUser, bool newBundle=false);
	void getUserQIs(const UserPtr& aUser, QueueItemList& ql);
	QueueItemPtr getNext(const HintedUser& aUser, QueueItem::Priority minPrio = QueueItem::LOWEST, int64_t wantedSize = 0, int64_t lastSpeed = 0, bool smallSlot=false, bool allowOverlap=false);
	QueueItemPtr getNextPrioQI(const HintedUser& aUser, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap);
	QueueItemPtr getNextBundleQI(const HintedUser& aUser, Bundle::Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap);
	QueueItemList getRunning(const UserPtr& aUser);
	void addDownload(QueueItemPtr qi, Download* d);
	void removeDownload(QueueItemPtr qi, const UserPtr& d, const string& token = Util::emptyString);

	void removeQI(QueueItemPtr qi, bool removeRunning = true, bool fireSources = false);
	void removeQI(QueueItemPtr qi, const UserPtr& aUser, bool removeRunning=true, bool addBad=false, bool fireSources=false);
	void setQIPriority(QueueItemPtr qi, QueueItem::Priority p);

	void addBundle(BundlePtr aBundle, const UserPtr& aUser);
	void removeBundle(BundlePtr aBundle, const UserPtr& aUser);
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

} // namespace dcpp

#endif // !defined(USER_QUEUE_H)
