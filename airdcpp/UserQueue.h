/*
 * Copyright (C) 2011-2024 AirDC++ Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
	void addQI(const QueueItemPtr& qi) noexcept;
	void addQI(const QueueItemPtr& qi, const HintedUser& aUser, bool aIsBadSource = false) noexcept;
	void getUserQIs(const UserPtr& aUser, QueueItemList& ql) noexcept;

	QueueItemPtr getNext(const QueueDownloadQuery& aQuery, string& lastError_, bool& hasDownload_, bool aAllowOverlap = false) noexcept;
	QueueItemPtr getNextPrioQI(const QueueDownloadQuery& aQuery, string& lastError_, bool aAllowOverlap) noexcept;
	QueueItemPtr getNextBundleQI(const QueueDownloadQuery& aQuery, string& lastError_, bool& hasDownload, bool aAllowOverlap) noexcept;

	void addDownload(const QueueItemPtr& qi, Download* d) noexcept;
	void removeDownload(const QueueItemPtr& qi, const string& aToken) noexcept;

	void removeQI(const QueueItemPtr& qi, bool removeRunning = true) noexcept;
	void removeQI(const QueueItemPtr& qi, const UserPtr& aUser, bool removeRunning = true, Flags::MaskType reason = 0) noexcept;
	void setQIPriority(const QueueItemPtr& qi, Priority p) noexcept;

	void addBundle(const BundlePtr& aBundle, const UserPtr& aUser) noexcept;
	void removeBundle(const BundlePtr& aBundle, const UserPtr& aUser) noexcept;
	void setBundlePriority(const BundlePtr& aBundle, Priority p) noexcept;

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
