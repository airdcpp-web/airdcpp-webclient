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

#include "stdinc.h"

#include <boost/range/numeric.hpp>
#include <boost/range/adaptor/map.hpp>

#include <airdcpp/queue/BundleQueue.h>
#include <airdcpp/util/DupeUtil.h>
#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/queue/QueueItem.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/core/timer/TimerManager.h>
#include <airdcpp/util/ValueGenerator.h>

namespace dcpp {

using ranges::find_if;

BundleQueue::BundleQueue() : searchQueue(SettingsManager::BUNDLE_SEARCH_TIME) { }

BundleQueue::~BundleQueue() { }

size_t BundleQueue::getTotalFiles() const noexcept {
	return boost::accumulate(bundles | boost::adaptors::map_values, (size_t)0, [](size_t old, const BundlePtr& b) { return old + b->getQueueItems().size() + b->getFinishedFiles().size(); });
}

void BundleQueue::addBundle(const BundlePtr& aBundle) noexcept {
	bundles[aBundle->getToken()] = aBundle;

	if (aBundle->filesCompleted()) {
		aBundle->setStatus(Bundle::STATUS_COMPLETED);
		return;
	}

	aBundle->setStatus(Bundle::STATUS_QUEUED);
	aBundle->setDownloadedBytes(0); //sets to downloaded segments

	searchQueue.addSearchPrio(aBundle);
}

void BundleQueue::getSourceInfo(const UserPtr& aUser, Bundle::SourceBundleList& aSources, Bundle::SourceBundleList& aBad) const noexcept {
	for(auto& b: bundles | views::values) {
		const auto& sources = b->getSources();
		if (auto s = find(sources.begin(), sources.end(), aUser); s != sources.end())
			aSources.emplace_back(b, *s);

		const auto& badSources = b->getBadSources();
		auto bs = find(badSources.begin(), badSources.end(), aUser);
		if (bs != badSources.end())
			aBad.emplace_back(b, *bs);
	}
}

BundlePtr BundleQueue::findBundle(QueueToken aBundleToken) const noexcept {
	auto i = bundles.find(aBundleToken);
	return i != bundles.end() ? i->second : nullptr;
}

BundlePtr BundleQueue::isLocalDirectoryQueued(const string& aPath) const noexcept {
	auto pathInfo = findLocalDirectoryPathInfo(aPath);
	if (!pathInfo) {
		return nullptr;
	}

	return pathInfo->bundle;
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

DupeType BundleQueue::getAdcDirectoryDupe(const string& aPath, int64_t aSize) const noexcept {
	PathInfoPtrList infos;
	findAdcDirectoryPathInfos(aPath, infos);

	if (infos.empty()) {
		return DUPE_NONE;
	}

	const auto& pathInfo = *infos.front();

	return pathInfo.toDupeType(aSize);
}

StringList BundleQueue::getAdcDirectoryDupePaths(const string& aAdcPath) const noexcept {
	PathInfoPtrList infos;
	findAdcDirectoryPathInfos(aAdcPath, infos);

	StringList ret;
	for (const auto& p : infos) {
		ret.push_back(p->path);
	}

	return ret;
}

void BundleQueue::findAdcDirectoryPathInfos(const string& aAdcPath, PathInfoPtrList& pathInfos_) const noexcept {
	// Get the last meaningful directory to look up
	auto [dirName, subdirStart] = DupeUtil::getAdcDirectoryName(aAdcPath);
	auto directories = dirNameMap.equal_range(dirName);

	// Go through all directories with this name
	for (const auto& pathInfo: directories | pair_to_range | views::values) {
		if (subdirStart != string::npos) {
			// Confirm that we have the subdirectory as well
			auto subDir = getAdcSubDirectoryInfo(aAdcPath.substr(subdirStart), pathInfo.bundle);
			if (subDir) {
				pathInfos_.push_back(subDir);
			}
		} else {
			pathInfos_.push_back(&pathInfo);
		}
	}
}

const BundleQueue::PathInfo* BundleQueue::findLocalDirectoryPathInfo(const string& aRealPath) const noexcept {
	// Get the last meaningful directory to look up
	auto [dirName, _] = DupeUtil::getLocalDirectoryName(aRealPath);
	auto directories = dirNameMap.equal_range(dirName);

	for (const auto& pathInfo : directories | pair_to_range | views::values) {
		auto pathInfos = getPathInfos(pathInfo.bundle->getTarget());

		// Find exact match
		auto i = ranges::find_if(*pathInfos, [&aRealPath](const PathInfo* aInfo) {
			return Util::stricmp(aInfo->path, aRealPath) == 0;
		});

		if (i != pathInfos->end()) {
			return *i;
		}
	}

	return nullptr;
}

const BundleQueue::PathInfo* BundleQueue::getAdcSubDirectoryInfo(const string& aSubPath, const BundlePtr& aBundle) const noexcept {
	if (auto pathInfos = getPathInfos(aBundle->getTarget())) {
		for (const auto& pathInfo : *pathInfos) {
			auto pos = PathUtil::compareFromEndAdc(pathInfo->path, aSubPath);
			if (pos == 0) {
				return pathInfo;
			}
		}
	}

	return nullptr;
}

BundlePtr BundleQueue::getMergeBundle(const string& aTarget) const noexcept {
	// Returns a bundle that is in a parent directory (or in the same location), in which we can merge to
	// File bundles with the exact target will be returned as well

	// In case it's a file bundle
	auto filePath = PathUtil::getFilePath(aTarget);

	for(const auto& compareBundle: bundles | views::values) {
		dcassert(!PathUtil::isDirectoryPath(aTarget) || !PathUtil::isSubLocal(compareBundle->getTarget(), filePath));

		if (compareBundle->isFileBundle()) {
			// Adding the same file again?
			if (Util::stricmp(aTarget, compareBundle->getTarget()) == 0) {
				return compareBundle;
			}
		} else if (PathUtil::isParentOrExactLocal(compareBundle->getTarget(), filePath)) {
			return compareBundle;
		}
	}
	return nullptr;
}

void BundleQueue::getSubBundles(const string& aTarget, BundleList& retBundles_) const noexcept {
	/* Returns bundles that are inside aTarget */
	for(const auto& compareBundle: bundles | views::values) {
		if (PathUtil::isSubLocal(compareBundle->getTarget(), aTarget)) {
			retBundles_.push_back(compareBundle);
		}
	}
}

template<class ContainerT>
ContainerT pickRandomItems(const ContainerT& aItems, size_t aMaxCount) noexcept {
	ContainerT ret, selectableItems = aItems;

	while (ret.size() < aMaxCount && !selectableItems.empty()) {
		auto pos = selectableItems.begin();
		auto rand = ValueGenerator::rand(0, static_cast<uint32_t>(selectableItems.size() - 1));
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

			mainBundlePaths.insert(DupeUtil::getReleaseDirLocal(pathInfo->path, false));
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
			if (searchItem && ranges::find_if(searchItems, QueueItem::HashComp(searchItem->getTTH())) == searchItems.end()) {
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
	auto info = &dirNameMap.emplace(PathUtil::getLastDir(aPath), PathInfo(aPath, aBundle))->second;
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

	auto nameRange = dirNameMap.equal_range(PathUtil::getLastDir(aPathInfo->path));
	auto i = ranges::find(nameRange | pair_to_range | views::values, *aPathInfo).base();
	if (i != nameRange.second) {
		dirNameMap.erase(i);
	}
}

void BundleQueue::forEachPath(const BundlePtr& aBundle, const string& aFilePath, const PathInfoHandler& aHandler) noexcept {
	auto currentPath = PathUtil::getFilePath(aFilePath);
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

		currentPath = PathUtil::getParentDir(currentPath);
	}
}

void BundleQueue::addBundleItem(const QueueItemPtr& aQI, const BundlePtr& aBundle) noexcept {
	dcassert(!aQI->getBundle());
	aBundle->addQueue(aQI);
	aQI->setBundle(aBundle);

	if (!aBundle->isFileBundle()) {
		forEachPath(aBundle, aQI->getTarget(), [&](PathInfo& aInfo) {
			if (aQI->isDownloaded()) {
				aInfo.finishedFiles++;
			} else {
				aInfo.queuedFiles++;
			}

			aInfo.size += aQI->getSize();
		});
	}

	if (!aQI->isDownloaded()) {
		queueSize += aQI->getSize();
	}
}

void BundleQueue::removeBundleItem(const QueueItemPtr& aQI, bool aDownloadFinished) noexcept {
	dcassert(aQI->getBundle());
	aQI->getBundle()->removeQueue(aQI, aDownloadFinished);

	if (!aQI->getBundle()->isFileBundle()) {
		forEachPath(aQI->getBundle(), aQI->getTarget(), [&](PathInfo& aInfo) {
			if (aQI->isDownloaded()) {
				aInfo.finishedFiles--;
			} else {
				aInfo.queuedFiles--;
			}

			if (!aDownloadFinished) {
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

	if (aDownloadFinished || !aQI->isDownloaded()) {
		queueSize -= aQI->getSize();
		dcassert(queueSize >= 0);
	}
}

void BundleQueue::removeBundle(const BundlePtr& aBundle) noexcept{
	if (aBundle->getStatus() == Bundle::STATUS_NEW) {
		return;
	}

	{
		auto infoPtr = getPathInfos(aBundle->getTarget());
		if (infoPtr) {
			auto pathInfos = *infoPtr; // copy is required here
			for (const auto& p : pathInfos) {
				removePathInfo(p);
			}
		}
	}

	dcassert(aBundle->getFinishedFiles().empty());
	dcassert(aBundle->getQueueItems().empty());

	searchQueue.removeSearchPrio(aBundle);
	bundles.erase(aBundle->getToken());

	dcassert(bundlePaths.size() == static_cast<size_t>(ranges::count_if(bundles | views::values, [](const BundlePtr& b) { return !b->isFileBundle(); })));

	aBundle->deleteXmlFile();
}

void BundleQueue::saveQueue(bool aForce) noexcept {
	for (const auto& b: bundles | views::values) {
		if (b->getDirty() || aForce) {
			try {
				b->save();
			} catch(FileException& e) {
				LogManager::getInstance()->message(STRING_F(SAVE_FAILED_X, b->getName() % e.getError()), LogMessage::SEV_ERROR, STRING(SETTINGS));
			}
		}
	}
}

} //dcpp