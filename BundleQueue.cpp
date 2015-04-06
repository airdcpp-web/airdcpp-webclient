/*
 * Copyright (C) 2011-2015 AirDC++ Project
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
	return boost::accumulate(bundles | map_values, (size_t)0, [](int64_t old, const BundlePtr& b) { return old + b->getQueueItems().size() + b->getFinishedFiles().size(); });
}

void BundleQueue::addBundle(BundlePtr& aBundle) noexcept {
	bundles[aBundle->getToken()] = aBundle;
	//check if we need to insert the root bundle dir
	if (!aBundle->isFileBundle()) {
		if (findLocalDir(aBundle->getTarget()) == bundleDirs.end()) {
			addDirectory(aBundle->getTarget(), aBundle);
		}
	}

	if (aBundle->isFinished()) {
		aBundle->setStatus(Bundle::STATUS_FINISHED);
		return;
	}

	aBundle->setStatus(Bundle::STATUS_QUEUED);
	aBundle->setDownloadedBytes(0); //sets to downloaded segments
	aBundle->updateSearchMode();

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

BundlePtr BundleQueue::findBundle(const string& bundleToken) const noexcept {
	auto i = bundles.find(bundleToken);
	return i != bundles.end() ? i->second : nullptr;
}

void BundleQueue::findRemoteDirs(const string& aPath, Bundle::StringBundleList& paths_) const noexcept {
	if (aPath.size() < 3)
		return;

	//get the last directory, we might need the position later with subdirs
	string remoteDir = aPath;
	if (remoteDir.back() == '\\')
		remoteDir.pop_back();

	auto pos = remoteDir.rfind("\\");
	if (pos != string::npos)
		remoteDir = remoteDir.substr(pos+1);

	auto directories = bundleDirs.equal_range(remoteDir);
	if (directories.first == directories.second)
		return;

	//check the parents for dirs like CD1 to prevent false matches
	if (boost::regex_match(remoteDir, AirUtil::subDirRegPlain) && pos != string::npos) {
		string::size_type i, j;
		remoteDir = "\\" + aPath;

		for(auto s = directories.first; s != directories.second; ++s) {
			//start matching from the parent dir, as we know the last one already
			i = pos;
			string curDir = s->second.first;

			for(;;) {
				j = remoteDir.find_last_of("\\", i);
				if(j == string::npos || (int)(curDir.length() - (remoteDir.length() - j)) < 0) //also check if it goes out of scope for the local dir
					break;
				if(Util::stricmp(remoteDir.substr(j+1, i-j), curDir.substr(curDir.length() - (remoteDir.length()-j)+1, i-j)) == 0) {
					if (!boost::regex_match(remoteDir.substr(j+1, i-j), AirUtil::subDirRegPlain)) { //another subdir? don't break in that case
						paths_.emplace_back(s->second);
						break;
					}
				} else {
					//this is something different... continue to next match
					break;
				}
				i = j - 1;
			}
		}
	} else {
		// copy all found directories
		for (auto s = directories.first; s != directories.second; ++s)
			paths_.emplace_back(s->second);
	}
}

BundlePtr BundleQueue::getMergeBundle(const string& aTarget) const noexcept {
	/* Returns directory bundles that are in sub or parent dirs (or in the same location), in which we can merge to */
	for(auto& compareBundle: bundles | map_values) {
		if (compareBundle->isFileBundle()) {
			if (!aTarget.empty() && aTarget.back() != PATH_SEPARATOR && aTarget == compareBundle->getTarget())
				return compareBundle;
		} else if (AirUtil::isSub(aTarget, compareBundle->getTarget()) || AirUtil::isParentOrExact(aTarget, compareBundle->getTarget())) {
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

void BundleQueue::addBundleItem(QueueItemPtr& qi, BundlePtr& aBundle) noexcept {
	dcassert(!qi->getBundle());
	if (aBundle->addQueue(qi) && !aBundle->isFileBundle()) {
		addDirectory(qi->getFilePath(), aBundle);
	}
}

void BundleQueue::removeBundleItem(QueueItemPtr& qi, bool finished) noexcept {
	dcassert(qi->getBundle());
	auto isFileBundle = qi->getBundle()->isFileBundle();
	if (qi->getBundle()->removeQueue(qi, finished) && !finished && !isFileBundle) {
		removeDirectory(qi->getFilePath());
	}
}

void BundleQueue::addDirectory(const string& aPath, BundlePtr& aBundle) noexcept {
	bundleDirs.emplace(Util::getLastDir(aPath), make_pair(aPath, aBundle));
}

void BundleQueue::removeDirectory(const string& aPath) noexcept {
	auto p = findLocalDir(aPath);
	if (p != bundleDirs.end()) {
		bundleDirs.erase(p);
	}
}

Bundle::BundleDirMap::iterator BundleQueue::findLocalDir(const string& aPath) noexcept {
	auto bdr = bundleDirs.equal_range(Util::getLastDir(aPath));
	auto s = find_if(bdr | map_values, CompareFirst<string, BundlePtr>(aPath));
	return s.base() != bdr.second ? s.base() : bundleDirs.end();
}

void BundleQueue::removeBundle(BundlePtr& aBundle) noexcept{
	if (aBundle->getStatus() == Bundle::STATUS_NEW) {
		return;
	}

	for(const auto& d: aBundle->getBundleDirs() | map_keys) {
		removeDirectory(d);
	}

	removeDirectory(aBundle->getTarget());

	dcassert(aBundle->getFinishedFiles().empty());
	dcassert(aBundle->getQueueItems().empty());

	removeSearchPrio(aBundle);
	bundles.erase(aBundle->getToken());

	aBundle->deleteBundleFile();
}

void BundleQueue::moveBundle(BundlePtr& aBundle, const string& newTarget) noexcept {
	//remove the old release dir
	removeDirectory(aBundle->getTarget());

	aBundle->setTarget(newTarget);

	//add new
	addDirectory(newTarget, aBundle);
}

void BundleQueue::getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const TargetUtil::VolumeSet& volumes) const noexcept{
	string tempVol;
	bool useSingleTempDir = !SETTING(DCTMP_STORE_DESTINATION) && SETTING(TEMP_DOWNLOAD_DIRECTORY).find("%[targetdrive]") == string::npos;
	if (useSingleTempDir) {
		tempVol = TargetUtil::getMountPath(SETTING(TEMP_DOWNLOAD_DIRECTORY), volumes);
	}

	for(const auto& b: bundles | map_values) {
		string mountPath = TargetUtil::getMountPath(b->getTarget(), volumes);
		if (!mountPath.empty()) {
			auto s = dirMap.find(mountPath);
			if (s != dirMap.end()) {
				bool countAll = (useSingleTempDir && (mountPath != tempVol));
				for(const auto& q: b->getQueueItems()) {
					if (countAll || q->getDownloadedBytes() == 0) {
						s->second.queued += q->getSize();
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
				LogManager::getInstance()->message(STRING_F(SAVE_FAILED_X, b->getName() % e.getError()), LogManager::LOG_ERROR);
			}
		}
	}
}

} //dcpp