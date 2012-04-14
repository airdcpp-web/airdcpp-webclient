/*
 * Copyright (C) 2011 AirDC++ Project
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
	void addBundleItem(QueueItemPtr qi, BundlePtr aBundle);
	void removeBundleItem(QueueItemPtr qi, bool finished);

	void addFinishedItem(QueueItemPtr qi, BundlePtr aBundle);
	void removeFinishedItem(QueueItemPtr qi);

	void add(BundlePtr aBundle);

	void getInfo(const string& aSource, BundleList& retBundles, int& finishedFiles, int& fileBundles);
	BundlePtr find(const string& bundleToken);
	BundlePtr findDir(const string& aPath);
	BundlePtr getMergeBundle(const string& aTarget);
	void getSubBundles(const string& aTarget, BundleList& retBundles);

	int getRecentIntervalMs();
	int getPrioSum();
	BundlePtr findRecent();
	BundlePtr findAutoSearch();
	BundlePtr findSearchBundle(uint64_t aTick, bool force=false);
	int64_t recalculateSearchTimes(bool aRecent, bool prioChange);

	Bundle::StringBundleMap& getBundles() { return bundles; }
	void move(BundlePtr aBundle, const string& newTarget);
	void remove(BundlePtr aBundle);

	void getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const StringSet& volumes);

	void addSearchPrio(BundlePtr aBundle);
	void removeSearchPrio(BundlePtr aBundle);

	void saveQueue(bool force) noexcept;
private:
	/** Bundles by priority (low-highest, for auto search) */
	vector<BundlePtr> prioSearchQueue[Bundle::LAST];
	deque<BundlePtr> recentSearchQueue;

	/** Bundles by release directory */	
	Bundle::StringBundleMap bundleDirs;
	/** Bundles by token */
	Bundle::StringBundleMap bundles;

	/** Next bundle search */
	uint64_t nextSearch;
	/** Next recent bundle search */
	uint64_t nextRecentSearch;

	//temp stats
	int highestSel, highSel, normalSel, lowSel, calculations;
};

} // namespace dcpp

#endif // !defined(BUNDLE_QUEUE_H)
