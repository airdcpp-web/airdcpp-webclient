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

	updateSearchMode(aBundle);
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

DupeType BundleQueue::isDirQueued(const string& aPath, int64_t aSize) const noexcept {
	PathInfoList infos;
	findRemoteDirs(aPath, infos);

	if (infos.empty())
		return DUPE_NONE;

	const auto& pathInfo = *infos.front();

	auto fullDupe = pathInfo.size == aSize;
	if (pathInfo.queuedFiles == 0) {
		return fullDupe ? DUPE_FINISHED_FULL : DUPE_FINISHED_PARTIAL;
	}

	return fullDupe ? DUPE_QUEUE_FULL : DUPE_QUEUE_PARTIAL;
}

size_t BundleQueue::getDirectoryCount(const BundlePtr& aBundle) const noexcept {
	auto i = bundlePaths.find(aBundle);
	if (i == bundlePaths.end()) {
		return 0;
	}

	return i->second.size();
}

StringList BundleQueue::getDirPaths(const string& aPath) const noexcept {
	PathInfoList infos;
	findRemoteDirs(aPath, infos);

	StringList ret;
	for (const auto& p : infos) {
		ret.push_back(p->path);
	}

	return ret;
}

void BundleQueue::findRemoteDirs(const string& aPath, PathInfoList& pathInfos_) const noexcept {
	// Get the last meaningful directory to look up
	auto dirNameInfo = AirUtil::getDirName(aPath, '\\');
	auto directories = dirNameMap.equal_range(dirNameInfo.first);
	if (directories.first == directories.second)
		return;

	// Go through all directories with this name
	for (auto s = directories.first; s != directories.second; ++s) {
		if (dirNameInfo.second != string::npos) {
			// Confirm that we have the subdirectory as well
			auto subDir = getSubDirectoryInfo(aPath.substr(dirNameInfo.second), s->second.bundle);
			if (subDir) {
				pathInfos_.push_back(subDir);
			}
		} else {
			pathInfos_.push_back(&s->second);
		}
	}
}

const BundleQueue::PathInfo* BundleQueue::getSubDirectoryInfo(const string& aSubPath, const BundlePtr& aBundle) const noexcept {
	auto paths = bundlePaths.find(aBundle);
	if (paths != bundlePaths.end()) {
		for (const auto& p : paths->second) {
			auto pos = AirUtil::compareFromEnd(p->path, aSubPath, '\\');
			if (pos == 0) {
				return p;
			}
		}
	}

	return nullptr;
}

BundlePtr BundleQueue::getMergeBundle(const string& aTarget) const noexcept {
	/* Returns directory bundles that are in sub or parent dirs (or in the same location), in which we can merge to */
	for(auto& compareBundle: bundles | map_values) {
		dcassert(!AirUtil::isSub(aTarget, compareBundle->getTarget()));
		if (compareBundle->isFileBundle()) {
			if (!aTarget.empty() && aTarget.back() != PATH_SEPARATOR && aTarget == compareBundle->getTarget())
				return compareBundle;
		} else if (AirUtil::isParentOrExact(aTarget, compareBundle->getTarget())) {
			return compareBundle;
		}
	}
	return nullptr;
}

void BundleQueue::getSubBundles(const string& aTarget, BundleList& retBundles) const noexcept {
	/* Returns bundles that are inside aTarget */
	for(auto& compareBundle: bundles | map_values) {
		if (AirUtil::isSub(compareBundle->getTarget(), aTarget)) {
			retBundles.push_back(compareBundle);
		}
	}
}

void BundleQueue::getSearchItems(const BundlePtr& aBundle, map<string, QueueItemPtr>& searchItems_, bool aManualSearch) const noexcept {
	if (aBundle->getQueueItems().empty())
		return;

	if (aBundle->isFileBundle() || aBundle->getQueueItems().size() == 1) {
		searchItems_.emplace(Util::emptyString, aBundle->getQueueItems().front());
		return;
	}

	auto paths = bundlePaths.find(aBundle);
	if (paths == bundlePaths.end()) {
		return;
	}

	QueueItemPtr searchItem = nullptr;
	for (const auto& i : paths->second) {
		auto dir = AirUtil::getReleaseDir(i->path, false);

		// Don't add the same directory twice
		if (searchItems_.find(dir) != searchItems_.end()) {
			continue;
		}

		// Get all queued files inside this directory
		QueueItemList ql;
		aBundle->getDirQIs(dir, ql);

		if (ql.empty()) {
			continue;
		}

		size_t s = 0;
		searchItem = nullptr;

		//do a few guesses to get a random item
		while (s <= ql.size()) {
			const auto& q = ql[ql.size() == 1 ? 0 : Util::rand(ql.size() - 1)];
			if (q->isPausedPrio() && !aManualSearch) {
				s++;
				continue;
			}
			if (q->isRunning() || (q->isPausedPrio())) {
				//it's ok but see if we can find better one
				searchItem = q;
			} else {
				searchItem = q;
				break;
			}
			s++;
		}

		if (searchItem) {
			searchItems_.emplace(dir, searchItem);
		}
	}
}

void BundleQueue::updateSearchMode(const BundlePtr& aBundle) const noexcept {
	auto paths = bundlePaths.find(aBundle);
	if (paths == bundlePaths.end()) {
		return;
	}

	StringSet mainDirectories;
	for (const auto& i : paths->second) {
		mainDirectories.insert(AirUtil::getReleaseDir(i->path, false));
	}

	aBundle->setSimpleMatching(mainDirectories.size() <= 4);
}

void BundleQueue::removePathInfo(const PathInfo* aPathInfo) noexcept {
	auto& pathInfos = bundlePaths[aPathInfo->bundle];
	pathInfos.erase(find(pathInfos, aPathInfo));
	if (pathInfos.empty()) {
		bundlePaths.erase(aPathInfo->bundle);
	}

	auto nameRange = dirNameMap.equal_range(Util::getLastDir(aPathInfo->path));
	auto i = boost::find(nameRange | map_values, aPathInfo).base();
	if (i != nameRange.second) {
		dirNameMap.erase(i);
	}
}

void BundleQueue::forEachPath(const BundlePtr& aBundle, const string& aPath, PathInfoHandler&& aHandler) noexcept {
	auto currentPath = Util::getFilePath(aPath);
	auto& pathInfos = bundlePaths[aBundle];

	while (true) {
		dcassert(currentPath.find(aBundle->getTarget()) != string::npos);

		auto infoIter = find_if(pathInfos, [&](const PathInfo* aInfo) { return aInfo->path == currentPath; });

		PathInfo* info;

		// New pathinfo?
		if (infoIter == pathInfos.end()) {
			info = &dirNameMap.emplace(Util::getLastDir(currentPath), PathInfo(currentPath, aBundle))->second;
			pathInfos.push_back(info);
		} else {
			info = *infoIter;
		}

		aHandler(*info);

		// Empty pathinfo?
		if (info->finishedFiles == 0 && info->queuedFiles == 0) {
			dcassert(info->size == 0);
			removePathInfo(info);
		}

		if (currentPath == aBundle->getTarget()) {
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

void BundleQueue::removeBundleItem(QueueItemPtr& aQI, bool finished) noexcept {
	dcassert(aQI->getBundle());
	aQI->getBundle()->removeQueue(aQI, finished);

	if (!aQI->getBundle()->isFileBundle()) {
		forEachPath(aQI->getBundle(), aQI->getTarget(), [&](PathInfo& aInfo) {
			if (aQI->isFinished()) {
				aInfo.finishedFiles--;
			} else {
				aInfo.queuedFiles--;
			}
			aInfo.size -= aQI->getSize();
		});
	}
}

void BundleQueue::removeBundle(BundlePtr& aBundle) noexcept{
	if (aBundle->getStatus() == Bundle::STATUS_NEW) {
		return;
	}

	auto paths = bundlePaths.find(aBundle);
	if (paths != bundlePaths.end()) {
		auto pathInfos = paths->second;
		for (const auto& p : pathInfos) {
			removePathInfo(p);
		}
	}

	dcassert(aBundle->getFinishedFiles().empty());
	dcassert(aBundle->getQueueItems().empty());

	removeSearchPrio(aBundle);
	bundles.erase(aBundle->getToken());

	dcassert(bundlePaths.size() == bundles.size());

	aBundle->deleteXmlFile();
}

void BundleQueue::getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const TargetUtil::VolumeSet& volumes) const noexcept{
	for(const auto& b: bundles | map_values) {
		string mountPath = TargetUtil::getMountPath(b->getTarget(), volumes);
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

void BundleQueue::saveQueue(bool force) noexcept {
	for(auto& b: bundles | map_values) {
		if (b->getDirty() || force) {
			try {
				b->save();
			} catch(FileException& e) {
				LogManager::getInstance()->message(STRING_F(SAVE_FAILED_X, b->getName() % e.getError()), LogMessage::SEV_ERROR);
			}
		}
	}
}

} //dcpp