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

#ifndef DCPLUSPLUS_DCPP_BUNDLE_QUEUE_H
#define DCPLUSPLUS_DCPP_BUNDLE_QUEUE_H

#include "forward.h"
#include "typedefs.h"
#include "HintedUser.h"
#include "Bundle.h"
#include "TargetUtil.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

/* Stores the queue bundle lists and the bundle search queue */

class BundleQueue {
public:
	BundleQueue();
	~BundleQueue();
	void addBundleItem(QueueItemPtr& qi, BundlePtr aBundle);
	void removeBundleItem(QueueItemPtr& qi, bool finished);

	size_t getTotalFiles() const;

	void addFinishedItem(QueueItemPtr& qi, BundlePtr aBundle);
	void removeFinishedItem(QueueItemPtr& qi);

	void addBundle(BundlePtr& aBundle);

	void getInfo(const string& aPath, BundleList& retBundles, int& finishedFiles, int& fileBundles) const;
	BundlePtr findBundle(const string& bundleToken) const;
	BundlePtr getMergeBundle(const string& aTarget) const;
	void getSubBundles(const string& aTarget, BundleList& retBundles) const;

	int getRecentIntervalMs() const;
	int getPrioSum() const;
	BundlePtr findRecent();
	BundlePtr findAutoSearch();
	BundlePtr findSearchBundle(uint64_t aTick, bool force=false);
	int64_t recalculateSearchTimes(bool aRecent, bool prioChange);

	Bundle::StringBundleMap& getBundles() { return bundles; }
	void moveBundle(BundlePtr& aBundle, const string& newTarget);
	void removeBundle(BundlePtr& aBundle);

	void getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const TargetUtil::VolumeSet& volumes) const;

	void addSearchPrio(BundlePtr& aBundle);
	void removeSearchPrio(BundlePtr& aBundle);

	void saveQueue(bool force) noexcept;


	void addDirectory(const string& aPath, BundlePtr aBundle);
	void removeDirectory(const string& aPath);
	Bundle::BundleDirMap::iterator findLocalDir(const string& aPath);
	pair<string, BundlePtr> findRemoteDir(const string& aPath) const;

	void getSourceInfo(const UserPtr& aUser, Bundle::SourceBundleList& aSources, Bundle::SourceBundleList& aBad) const;
private:
	/** Bundles by priority (low-highest, for auto search) */
	vector<BundlePtr> prioSearchQueue[Bundle::LAST];
	deque<BundlePtr> recentSearchQueue;

	/** Bundles by release directory */	
	Bundle::BundleDirMap bundleDirs;
	/** Bundles by token */
	Bundle::StringBundleMap bundles;

	/** Next bundle search */
	uint64_t nextSearch;
	/** Next recent bundle search */
	uint64_t nextRecentSearch;
};

} // namespace dcpp

#endif // !defined(BUNDLE_QUEUE_H)
