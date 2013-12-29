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

#ifndef DCPLUSPLUS_DCPP_USER_QUEUE_H
#define DCPLUSPLUS_DCPP_USER_QUEUE_H

#include "forward.h"
#include "typedefs.h"
#include "HintedUser.h"
#include "QueueItem.h"

namespace dcpp {

/** All queue items indexed by user (this is a cache for the FileQueue really...) */
class UserQueue {
public:
	void addQI(QueueItemPtr& qi, bool newBundle = false) noexcept;
	void addQI(QueueItemPtr& qi, const HintedUser& aUser, bool newBundle = false, bool isBadSource = false) noexcept;
	void getUserQIs(const UserPtr& aUser, QueueItemList& ql) noexcept;

	QueueItemPtr getNext(const UserPtr& aUser, const StringSet& runningBundles, const OrderedStringSet& onlineHubs, string& lastError_, bool& hasDownload,
		QueueItemBase::Priority minPrio = QueueItem::LOWEST, int64_t wantedSize = 0, int64_t lastSpeed = 0, QueueItemBase::DownloadType aType = QueueItem::TYPE_ANY, bool allowOverlap = false) noexcept;
	QueueItemPtr getNextPrioQI(const UserPtr& aUser, const OrderedStringSet& onlineHubs, int64_t wantedSize, int64_t lastSpeed, 
		QueueItemBase::DownloadType aType, bool allowOverlap, string& lastError_) noexcept;
	QueueItemPtr getNextBundleQI(const UserPtr& aUser, const StringSet& runningBundles, const OrderedStringSet& onlineHubs, 
		QueueItemBase::Priority minPrio, int64_t wantedSize, int64_t lastSpeed, QueueItemBase::DownloadType aType, 
		bool allowOverlap, string& lastError_, bool& hasDownload) noexcept;

	void addDownload(QueueItemPtr& qi, Download* d) noexcept;
	void removeDownload(QueueItemPtr& qi, const string& aToken) noexcept;

	void removeQI(QueueItemPtr& qi, bool removeRunning = true, bool fireSources = false) noexcept;
	void removeQI(QueueItemPtr& qi, const UserPtr& aUser, bool removeRunning = true, bool addBad = false, bool fireSources = false) noexcept;
	void setQIPriority(QueueItemPtr& qi, QueueItemBase::Priority p) noexcept;

	void addBundle(BundlePtr& aBundle, const UserPtr& aUser) noexcept;
	void removeBundle(BundlePtr& aBundle, const UserPtr& aUser) noexcept;
	void setBundlePriority(BundlePtr& aBundle, QueueItemBase::Priority p) noexcept;

	unordered_map<UserPtr, BundleList, User::Hash>& getBundleList()  { return userBundleQueue; }
	unordered_map<UserPtr, QueueItemList, User::Hash>& getPrioList()  { return userPrioQueue; }
private:
	/** Bundles by priority and user (this is where the download order is determined) */
	unordered_map<UserPtr, BundleList, User::Hash> userBundleQueue;
	/** High priority QueueItems by user (this is where the download order is determined) */
	unordered_map<UserPtr, QueueItemList, User::Hash> userPrioQueue;
};

} // namespace dcpp

#endif // !defined(USER_QUEUE_H)
