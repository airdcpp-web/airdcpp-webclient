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

#include "boost/unordered_map.hpp"

namespace dcpp {

/* Stores the queue bundle lists and the bundle search queue */

class BundleQueue {
public:
	BundleQueue();
	~BundleQueue();
	void addBundleItem(QueueItem* qi, BundlePtr aBundle);
	void removeBundleItem(QueueItem* qi, bool finished);

	void add(BundlePtr aBundle);

	void getInfo(const string& aSource, BundleList& retBundles, int& finishedFiles, int& fileBundles);
	BundlePtr find(const string& bundleToken);
	BundlePtr findDir(const string& aPath);
	BundlePtr getMergeBundle(const string& aTarget);
	void getMergeBundles(const string& aTarget, BundleList& retBundles);

	int getRecentSize() { return (int)recentSearchQueue.size(); }
	int getPrioSum(int& prioBundles);
	BundlePtr findRecent();
	BundlePtr findAutoSearch();
	BundlePtr findSearchBundle(uint64_t aTick, bool force=false);
	int64_t recalculateSearchTimes(BundlePtr aBundle, bool prioChange);

	Bundle::StringBundleMap& getBundles() { return bundles; }
	void move(BundlePtr aBundle, const string& newTarget);
	void remove(BundlePtr aBundle, bool finished);

	void getDiskInfo(map<string, pair<string, int64_t>>& dirMap, const StringSet& volumes);

	void addSearchPrio(BundlePtr aBundle);
	void removeSearchPrio(BundlePtr aBundle);

	void getAutoPrioMap(bool verbose, multimap<int, BundlePtr>& finalMap, int& uniqueValues);
	void saveQueue(bool force) noexcept;
private:
	/** Bundles by priority (low-highest, for auto search) */
	deque<BundlePtr> prioSearchQueue[Bundle::LAST];
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
