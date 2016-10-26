/*
 * Copyright (C) 2011-2016 AirDC++ Project
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

#include "stdinc.h"

#include <boost/range/numeric.hpp>
#include <boost/range/algorithm/count_if.hpp>

#include "AirUtil.h"
#include "BundleQueue.h"
#include "LogManager.h"
#include "QueueItem.h"
#include "SettingsManager.h"
#include "TimerManager.h"

namespace dcpp {

using boost::range::find_if;

BundleQueue::BundleQueue() { }

BundleQueue::~BundleQueue() { }

size_t BundleQueue::getTotalFiles() const noexcept {
	return boost::accumulate(bundles | map_values, (size_t)0, [](size_t old, const BundlePtr& b) { return old + b->getQueueItems().size() + b->getFinishedFiles().size(); });
}

void BundleQueue::addBundle(BundlePtr& aBundle) noexcept {
	bundles[aBundle->getToken()] = aBundle;

	if (aBundle->isFinished()) {
		aBundle->setStatus(Bundle::STATUS_FINISHED);
		return;
	}

	aBundle->setStatus(Bundle::STATUS_QUEUED);
	aBundle->setDownloadedBytes(0); //sets to downloaded segments

	addSearchPrio(aBundle);
}

void BundleQueue::getSourceInfo(const UserPtr& aUser, Bundle::SourceBundleList& aSources, Bundle::SourceBundleList& aBad) const noexcept {
	for(auto& b: bundles | map_values) {
		const auto& sources = b->getSources();
		auto s = find(sources.begin(), sources.end(), aUser);
		if (s != sources.end())
			aSources.emplace_back(b, *s);

		const auto& badSources = b->getBadSources();
		auto bs = find(badSources.begin(), badSources.end(), aUser);
		if (bs != badSources.end())
			aBad.emplace_back(b, *bs);
	}
}

BundlePtr BundleQueue::findBundle(QueueToken bundleToken) const noexcept {
	auto i = bundles.find(bundleToken);
	return i != bundles.end() ? i->second : nullptr;
}

DupeType BundleQueue::isNmdcDirQueued(const string& aPath, int64_t aSize) const noexcept {
	PathInfoPtrList infos;
	findNmdcDirs(aPath, infos);

	if (infos.empty())
		return DUPE_NONE;

	const auto& pathInfo = *infos.front();

	return pathInfo.toDupeType(aSize);
}

DupeType BundleQueue::PathInfo::toDupeType(int64_t aSize) const noexcept {
	auto fullDupe = size == aSize;
	if (queuedFiles == 0) {
		return fullDupe ? DUPE_FINISHED_FULL : DUPE_FINISHED_PARTIAL;
	}

	return fullDupe ? DUPE_QUEUE_FULL : DUPE_QUEUE_PARTIAL;
}

BundlePtr BundleQueue::findBundle(const string& aPath) const noexcept {
	auto pathInfos = getPathInfos(aPath);
	if (!pathInfos || (*pathInfos).empty()) {
		return nullptr;
	}

	return (*pathInfos).front()->bundle;
}

const BundleQueue::PathInfo::List* BundleQueue::getPathInfos(const string& aBundlePath) const noexcept {
	auto i = bundlePaths.find(const_cast<string*>(&aBundlePath));
	if (i == bundlePaths.end()) {
		return nullptr;
	}

	return &i->second;
}

size_t BundleQueue::getDirectoryCount(const BundlePtr& aBundle) const noexcept {
	auto pathInfos = getPathInfos(aBundle->getTarget());
	if (!pathInfos) {
		return 0;
	}

	return (*pathInfos).size();
}

StringList BundleQueue::getNmdcDirPaths(const string& aPath) const noexcept {
	PathInfoPtrList infos;
	findNmdcDirs(aPath, infos);

	StringList ret;
	for (const auto& p : infos) {
		ret.push_back(p->path);
	}

	return ret;
}

void BundleQueue::findNmdcDirs(const string& aPath, PathInfoPtrList& pathInfos_) const noexcept {
	// Get the last meaningful directory to look up
	auto dirNameInfo = AirUtil::getDirName(aPath, NMDC_SEPARATOR);
	auto directories = dirNameMap.equal_range(dirNameInfo.first);
	if (directories.first == directories.second)
		return;

	// Go through all directories with this name
	for (auto s = directories.first; s != directories.second; ++s) {
		if (dirNameInfo.second != string::npos) {
			// Confirm that we have the subdirectory as well
			auto subDir = getNmdcSubDirectoryInfo(aPath.substr(dirNameInfo.second), s->second.bundle);
			if (subDir) {
				pathInfos_.push_back(subDir);
			}
		} else {
			pathInfos_.push_back(&s->second);
		}
	}
}

const BundleQueue::PathInfo* BundleQueue::getNmdcSubDirectoryInfo(const string& aSubPath, const BundlePtr& aBundle) const noexcept {
	auto pathInfos = getPathInfos(aBundle->getTarget());
	if (pathInfos) {
		for (const auto& p : *pathInfos) {
			auto pos = AirUtil::compareFromEnd(p->path, aSubPath, NMDC_SEPARATOR);
			if (pos == 0) {
				return p;
			}
		}
	}

	return nullptr;
}

BundlePtr BundleQueue::getMergeBundle(const string& aTarget) const noexcept {
	// Returns a bundle that is in a parent directory (or in the same location), in which we can merge to
	// File bundles with the exact target will be returned as well

	// In case it's a file bundle
	auto filePath = Util::getFilePath(aTarget);

	for(const auto& compareBundle: bundles | map_values) {
		dcassert(aTarget.back() != PATH_SEPARATOR || !AirUtil::isSubLocal(compareBundle->getTarget(), filePath));

		if (compareBundle->isFileBundle()) {
			// Adding the same file again?
			if (Util::stricmp(aTarget, compareBundle->getTarget()) == 0) {
				return compareBundle;
			}
		} else if (AirUtil::isParentOrExactLocal(compareBundle->getTarget(), filePath)) {
			return compareBundle;
		}
	}
	return nullptr;
}

void BundleQueue::getSubBundles(const string& aTarget, BundleList& retBundles) const noexcept {
	/* Returns bundles that are inside aTarget */
	for(const auto& compareBundle: bundles | map_values) {
		if (AirUtil::isSubLocal(compareBundle->getTarget(), aTarget)) {
			retBundles.push_back(compareBundle);
		}
	}
}

template<class ContainerT>
ContainerT pickRandomItems(const ContainerT& aItems, size_t aMaxCount) noexcept {
	ContainerT ret, selectableItems = aItems;

	while (ret.size() < aMaxCount && !selectableItems.empty()) {
		auto pos = selectableItems.begin();
		auto rand = Util::rand(selectableItems.size());
		advance(pos, rand);

		ret.insert(*pos);

		selectableItems.erase(pos);
	}

	return ret;
}

QueueItemList BundleQueue::getSearchItems(const BundlePtr& aBundle) const noexcept {
	if (aBundle->getQueueItems().size() <= 1) {
		return aBundle->getQueueItems();
	}

	// File bundles shouldn't come here
	QueueItemList searchItems;
	auto pathInfos = getPathInfos(aBundle->getTarget());
	if (!pathInfos) {
		return searchItems;
	}

	{
		// Get the main directories inside this bundle
		// We'll choose a single search item from each main directory later
		// This helps with getting best coverage for complex bundles that aren't
		// shared with same structure by most users

		StringSet mainBundlePaths;
		for (const auto& pathInfo : *pathInfos) {
			if (pathInfo->queuedFiles == 0) {
				continue;
			}

			mainBundlePaths.insert(AirUtil::getReleaseDirLocal(pathInfo->path, false));
		}


		auto searchPaths = pickRandomItems(mainBundlePaths, 5);

		for (const auto& path : searchPaths) {
			QueueItemList ql;

			// Get all queued files inside this directory
			// This doesn't scale so well for large bundles but shouldn't cause issues with maximum of 5 paths
			aBundle->getDirQIs(path, ql);

			auto searchItem = QueueItem::pickSearchItem(ql);

			// We'll also get search items for parent directories that have no files directly inside them
			// so we need to filter duplicate items as well
			if (searchItem && find_if(searchItems, QueueItem::HashComp(searchItem->getTTH())) == searchItems.end()) {
				searchItems.push_back(searchItem);
			}
		}
	}

#if 0
	StringList targets;
	for (const auto& qi : searchItems) {
		targets.push_back(qi->getTarget());
	}

	LogManager::getInstance()->message("Search items from bundle " + aBundle->getName() + ": " + Util::listToString(targets), LogMessage::SEV_INFO);
#endif

	return searchItems;
}

BundleQueue::PathInfo* BundleQueue::addPathInfo(const string& aPath, const BundlePtr& aBundle) noexcept {
	auto info = &dirNameMap.emplace(Util::getLastDir(aPath), PathInfo(aPath, aBundle))->second;
	bundlePaths[const_cast<string*>(&aBundle->getTarget())].insert_sorted(info);
	return info;
}

void BundleQueue::removePathInfo(const PathInfo* aPathInfo) noexcept {
	auto bundleTarget = const_cast<string*>(&aPathInfo->bundle->getTarget());

	auto& pathInfos = bundlePaths[bundleTarget];
	pathInfos.erase_key(aPathInfo->path);
	if (pathInfos.empty()) {
		bundlePaths.erase(bundleTarget);
	}

	auto nameRange = dirNameMap.equal_range(Util::getLastDir(aPathInfo->path));
	auto i = boost::find(nameRange | map_values, aPathInfo).base();
	if (i != nameRange.second) {
		dirNameMap.erase(i);
	}
}

void BundleQueue::forEachPath(const BundlePtr& aBundle, const string& aFilePath, PathInfoHandler&& aHandler) noexcept {
	auto currentPath = Util::getFilePath(aFilePath);
	auto& pathInfos = bundlePaths[const_cast<string*>(&aBundle->getTarget())];

	while (true) {
		dcassert(currentPath.find(aBundle->getTarget()) != string::npos);

		// TODO: make this case insensitive
		auto infoIter = pathInfos.find(currentPath);

		PathInfo* info;

		// New pathinfo?
		if (infoIter == pathInfos.end()) {
			info = addPathInfo(currentPath, aBundle);
		} else {
			info = *infoIter;
		}

		aHandler(*info);

		// Empty pathinfo?
		if (info->finishedFiles == 0 && info->queuedFiles == 0) {
			dcassert(info->size == 0);
			removePathInfo(info);
		}

		if (currentPath.length() == aBundle->getTarget().length()) {
			break;
		}

		currentPath = Util::getParentDir(currentPath);
	}
}

void BundleQueue::addBundleItem(QueueItemPtr& aQI, BundlePtr& aBundle) noexcept {
	dcassert(!aQI->getBundle());
	aBundle->addQueue(aQI);

	if (!aBundle->isFileBundle()) {
		forEachPath(aBundle, aQI->getTarget(), [&](PathInfo& aInfo) {
			if (aQI->isFinished()) {
				aInfo.finishedFiles++;
			} else {
				aInfo.queuedFiles++;
			}

			aInfo.size += aQI->getSize();
		});
	}
}

void BundleQueue::removeBundleItem(QueueItemPtr& aQI, bool aFinished) noexcept {
	dcassert(aQI->getBundle());
	aQI->getBundle()->removeQueue(aQI, aFinished);

	if (!aQI->getBundle()->isFileBundle()) {
		forEachPath(aQI->getBundle(), aQI->getTarget(), [&](PathInfo& aInfo) {
			if (aQI->isFinished()) {
				aInfo.finishedFiles--;
			} else {
				aInfo.queuedFiles--;
			}

			if (!aFinished) {
				aInfo.size -= aQI->getSize();
			}

#ifdef _DEBUG
			if (aInfo.queuedFiles == 0 && aInfo.finishedFiles == 0) {
				dcassert(aInfo.size == 0);
			}

			dcassert(aInfo.size >= 0 && aInfo.finishedFiles >= 0 && aInfo.queuedFiles >= 0);
#endif
		});
	}
}

void BundleQueue::removeBundle(BundlePtr& aBundle) noexcept{
	if (aBundle->getStatus() == Bundle::STATUS_NEW) {
		return;
	}

	{
		auto infoPtr = getPathInfos(aBundle->getTarget());
		if (infoPtr) {
			auto pathInfos = *infoPtr;
			for (const auto& p : pathInfos) {
				removePathInfo(p);
			}
		}
	}

	dcassert(aBundle->getFinishedFiles().empty());
	dcassert(aBundle->getQueueItems().empty());

	removeSearchPrio(aBundle);
	bundles.erase(aBundle->getToken());

	dcassert(bundlePaths.size() == static_cast<size_t>(boost::count_if(bundles | map_values, [](const BundlePtr& b) { return !b->isFileBundle(); })));

	aBundle->deleteXmlFile();
}

void BundleQueue::getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const TargetUtil::VolumeSet& aVolumes) const noexcept{
	for(const auto& b: bundles | map_values) {
		string mountPath = TargetUtil::getMountPath(b->getTarget(), aVolumes);
		if (!mountPath.empty()) {
			auto s = dirMap.find(mountPath);
			if (s != dirMap.end()) {
				for(const auto& q: b->getQueueItems()) {
					if (q->getDownloadedBytes() == 0) {
						s->second.addQueued(q->getSize());
					}
				}
			}
		}
	}
}

void BundleQueue::saveQueue(bool aForce) noexcept {
	for(auto& b: bundles | map_values) {
		if (b->getDirty() || aForce) {
			try {
				b->save();
			} catch(FileException& e) {
				LogManager::getInstance()->message(STRING_F(SAVE_FAILED_X, b->getName() % e.getError()), LogMessage::SEV_ERROR);
			}
		}
	}
}

} //dcpp