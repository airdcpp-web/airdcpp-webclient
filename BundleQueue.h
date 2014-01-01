/*
 * Copyright (C) 2011-2014 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_BUNDLE_QUEUE_H
#define DCPLUSPLUS_DCPP_BUNDLE_QUEUE_H

#include "forward.h"
#include "typedefs.h"
#include "HintedUser.h"
#include "Bundle.h"
#include "TargetUtil.h"

namespace dcpp {

/* Stores the queue bundle lists and the bundle search queue */

class BundleQueue {
public:
	BundleQueue();
	~BundleQueue();
	void addBundleItem(QueueItemPtr& qi, BundlePtr& aBundle) noexcept;
	void removeBundleItem(QueueItemPtr& qi, bool finished) noexcept;

	size_t getTotalFiles() const noexcept;

	void addFinishedItem(QueueItemPtr& qi, BundlePtr& aBundle) noexcept;
	void removeFinishedItem(QueueItemPtr& qi) noexcept;

	void addBundle(BundlePtr& aBundle) noexcept;

	void getInfo(const string& aPath, BundleList& retBundles, int& finishedFiles, int& fileBundles) const noexcept;
	BundlePtr findBundle(const string& bundleToken) const noexcept;
	BundlePtr getMergeBundle(const string& aTarget) const noexcept;
	void getSubBundles(const string& aTarget, BundleList& retBundles) const noexcept;

	int getRecentIntervalMs() const noexcept;
	int getPrioSum() const noexcept;
	BundlePtr findRecent() noexcept;
	BundlePtr findAutoSearch() noexcept;
	BundlePtr findSearchBundle(uint64_t aTick, bool force = false) noexcept;
	int64_t recalculateSearchTimes(bool aRecent, bool prioChange) noexcept;

	void moveBundle(BundlePtr& aBundle, const string& newTarget) noexcept;
	void removeBundle(BundlePtr& aBundle) noexcept;

	void getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const TargetUtil::VolumeSet& volumes) const noexcept;

	void addSearchPrio(BundlePtr& aBundle) noexcept;
	void removeSearchPrio(BundlePtr& aBundle) noexcept;

	void saveQueue(bool force) noexcept;


	void addDirectory(const string& aPath, BundlePtr& aBundle) noexcept;
	void removeDirectory(const string& aPath) noexcept;
	Bundle::BundleDirMap::iterator findLocalDir(const string& aPath) noexcept;
	void findRemoteDirs(const string& aPath, Bundle::StringBundleList& paths_) const noexcept;

	void getSourceInfo(const UserPtr& aUser, Bundle::SourceBundleList& aSources, Bundle::SourceBundleList& aBad) const noexcept;

	Bundle::StringBundleMap& getBundles() { return bundles; }
	const Bundle::StringBundleMap& getBundles() const { return bundles; }
private:
	/** Bundles by priority (low-highest, for auto search) */
	vector<BundlePtr> prioSearchQueue[Bundle::LAST];
	deque<BundlePtr> recentSearchQueue;

	/** Bundles by release directory */	
	Bundle::BundleDirMap bundleDirs;
	/** Bundles by token */
	Bundle::StringBundleMap bundles;

	/** Next bundle search */
	uint64_t nextSearch = 0;
	/** Next recent bundle search */
	uint64_t nextRecentSearch = 0;
};

} // namespace dcpp

#endif // !defined(BUNDLE_QUEUE_H)
