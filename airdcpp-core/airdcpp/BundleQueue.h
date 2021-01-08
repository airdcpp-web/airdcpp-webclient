/*
 * Copyright (C) 2011-2021 AirDC++ Project
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

#include "Bundle.h"
#include "DupeType.h"
#include "HintedUser.h"
#include "PrioritySearchQueue.h"
#include "SortedVector.h"

namespace dcpp {

/* Stores the queue bundle lists and the bundle search queue */

class BundleQueue : public PrioritySearchQueue<BundlePtr> {
public:
	struct PathInfo {
		PathInfo(const string& aPath, const BundlePtr& aBundle) noexcept : path(aPath), bundle(aBundle) {  }
		struct Path {
			const string& operator()(const PathInfo* a) const { return a->path; }
		};

		typedef SortedVector<PathInfo*, std::vector, string, Compare, Path> List;

		bool operator==(const PathInfo* aInfo) const noexcept { return this == aInfo; }

		size_t queuedFiles = 0;
		size_t finishedFiles = 0;

		int64_t size = 0;

		const string path;
		const BundlePtr bundle;

		DupeType toDupeType(int64_t aSize) const noexcept;
	};

	typedef vector<const PathInfo*> PathInfoPtrList;
	typedef unordered_multimap<string, PathInfo, noCaseStringHash, noCaseStringEq> DirectoryNameMap;
	typedef unordered_map<string*, PathInfo::List, noCaseStringHash, noCaseStringEq> PathInfoMap;

	BundleQueue();
	~BundleQueue();
	void addBundleItem(const QueueItemPtr& aQI, const BundlePtr& aBundle) noexcept;
	void removeBundleItem(const QueueItemPtr& aQI, bool aFinished) noexcept;

	size_t getTotalFiles() const noexcept;

	void addBundle(const BundlePtr& aBundle) noexcept;

	BundlePtr findBundle(QueueToken bundleToken) const noexcept;
	BundlePtr findBundle(const string& aPath) const noexcept;

	BundlePtr getMergeBundle(const string& aTarget) const noexcept;
	void getSubBundles(const string& aTarget, BundleList& retBundles_) const noexcept;

	void removeBundle(const BundlePtr& aBundle) noexcept;

	void saveQueue(bool aForce) noexcept;
	QueueItemList getSearchItems(const BundlePtr& aBundle) const noexcept;

	DupeType isAdcDirectoryQueued(const string& aPath, int64_t aSize) const noexcept;
	BundlePtr isLocalDirectoryQueued(const string& aPath) const noexcept;

	StringList getAdcDirectoryPaths(const string& aDirName) const noexcept;
	size_t getDirectoryCount(const BundlePtr& aBundle) const noexcept;

	void getSourceInfo(const UserPtr& aUser, Bundle::SourceBundleList& aSources, Bundle::SourceBundleList& aBad) const noexcept;

	Bundle::TokenMap& getBundles() { return bundles; }
	const Bundle::TokenMap& getBundles() const { return bundles; }

	int64_t getTotalQueueSize() const noexcept { return queueSize; }
private:
	void findAdcDirectoryPathInfos(const string& aAdcPath, PathInfoPtrList& pathInfos_) const noexcept;
	const PathInfo* findLocalDirectoryPathInfo(const string& aRealPath) const noexcept;

	const PathInfo* getAdcSubDirectoryInfo(const string& aSubPath, const BundlePtr& aBundle) const noexcept;

	// Get path infos by bundle path
	const PathInfo::List* getPathInfos(const string& aBundlePath) const noexcept;

	typedef function<void(PathInfo&)> PathInfoHandler;

	// Goes through each directory and stops after the bundle target was handled
	void forEachPath(const BundlePtr& aBundle, const string& aPath, PathInfoHandler&& aHandler) noexcept;

	PathInfo* addPathInfo(const string& aPath, const BundlePtr& aBundle) noexcept;
	void removePathInfo(const PathInfo* aPathInfo) noexcept;

	// PathInfos by directory name
	DirectoryNameMap dirNameMap;

	// PathInfos by bundle path
	PathInfoMap bundlePaths;

	// Bundles by token
	Bundle::TokenMap bundles;

	int64_t queueSize = 0;
};

} // namespace dcpp

#endif // !defined(BUNDLE_QUEUE_H)
