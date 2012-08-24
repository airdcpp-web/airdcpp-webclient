/*
 * Copyright (C) 2011-2012 AirDC++ Project
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

#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>
#include <boost/random/discrete_distribution.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/fusion/include/count_if.hpp>
#include <boost/range/adaptor/map.hpp>

#include "BundleQueue.h"
#include "SettingsManager.h"
#include "AirUtil.h"
#include "QueueItem.h"
#include "LogManager.h"
#include "TimerManager.h"

namespace dcpp {

using boost::range::for_each;
using boost::adaptors::map_values;

BundleQueue::BundleQueue() : 
	nextSearch(0),
	nextRecentSearch(0)
{ 
	highestSel=0, highSel=0, normalSel=0, lowSel=0, calculations=0;
}

BundleQueue::~BundleQueue() { }

void BundleQueue::addBundle(BundlePtr aBundle) {
	aBundle->unsetFlag(Bundle::FLAG_NEW);
	aBundle->setDownloadedBytes(0); //sets to downloaded segments

	addSearchPrio(aBundle);
	bundles[aBundle->getToken()] = aBundle;

	//check if we need to insert the root bundle dir
	if (!aBundle->isFileBundle()) {
		if (aBundle->getBundleDirs().find(aBundle->getTarget()) == aBundle->getBundleDirs().end()) {
			string releaseDir = AirUtil::getReleaseDir(aBundle->getTarget());
			if (!releaseDir.empty()) {
				bundleDirs[releaseDir] = aBundle;
			}
		}
	}
}

void BundleQueue::addSearchPrio(BundlePtr aBundle) {
	if (aBundle->getPriority() < Bundle::LOW) {
		return;
	}

	if (aBundle->isRecent()) {
		dcassert(std::find(recentSearchQueue.begin(), recentSearchQueue.end(), aBundle) == recentSearchQueue.end());
		recentSearchQueue.push_back(aBundle);
		return;
	} else {
		dcassert(std::find(prioSearchQueue[aBundle->getPriority()].begin(), prioSearchQueue[aBundle->getPriority()].end(), aBundle) == prioSearchQueue[aBundle->getPriority()].end());
		prioSearchQueue[aBundle->getPriority()].push_back(aBundle);
	}
}

void BundleQueue::removeSearchPrio(BundlePtr aBundle) {
	if (aBundle->getPriority() < Bundle::LOW) {
		return;
	}

	if (aBundle->isRecent()) {
		auto i = std::find(recentSearchQueue.begin(), recentSearchQueue.end(), aBundle);
		//dcassert(i != recentSearchQueue.end());
		if (i != recentSearchQueue.end()) {
			recentSearchQueue.erase(i);
		}
	} else {
		auto i = std::find(prioSearchQueue[aBundle->getPriority()].begin(), prioSearchQueue[aBundle->getPriority()].end(), aBundle);
		//dcassert(i != prioSearchQueue[aBundle->getPriority()].end());
		if (i != prioSearchQueue[aBundle->getPriority()].end()) {
			prioSearchQueue[aBundle->getPriority()].erase(i);
		}
	}
}

BundlePtr BundleQueue::findSearchBundle(uint64_t aTick, bool force /* =false */) {
	BundlePtr bundle = nullptr;
	if(aTick >= nextSearch || force) {
		bundle = findAutoSearch();
		//LogManager::getInstance()->message("Next search in " + Util::toString(next) + " minutes");
	}
	
	if(!bundle && (aTick >= nextRecentSearch || force)) {
		bundle = findRecent();
		//LogManager::getInstance()->message("Next recent search in " + Util::toString(recentBundles > 1 ? 5 : 10) + " minutes");
	}

	if(bundle) {
		if (!bundle->isRecent()) {
			calculations++;
			switch((int)bundle->getPriority()) {
				case 2:
					lowSel++;
					break;
				case 3:
					normalSel++;
					break;
				case 4:
					highSel++;
					break;
				case 5:
					highestSel++;
					break;
			}
		} else {
			//LogManager::getInstance()->message("Performing search for a RECENT bundle: " + bundle->getName());
		}
		//LogManager::getInstance()->message("Calculations performed: " + Util::toString(calculations) + ", highest: " + Util::toString(((double)highestSel/calculations)*100) + "%, high: " + Util::toString(((double)highSel/calculations)*100) + "%, normal: " + Util::toString(((double)normalSel/calculations)*100) + "%, low: " + Util::toString(((double)lowSel/calculations)*100) + "%");
	}
	return bundle;
}

int64_t BundleQueue::recalculateSearchTimes(bool aRecent, bool isPrioChange) {
	if (!aRecent) {
		int prioBundles = getPrioSum();
		int minInterval = SETTING(SEARCH_TIME);

		if (prioBundles > 0) {
			minInterval = max(60 / prioBundles, SETTING(SEARCH_TIME));
		}

		if (nextSearch > 0 && isPrioChange) {
			nextSearch = min(nextSearch, GET_TICK() + (minInterval * 60 * 1000));
		} else {
			nextSearch = GET_TICK() + (minInterval * 60 * 1000);
		}
		return nextSearch;
	} else {
		if (nextRecentSearch > 0 && isPrioChange) {
			nextRecentSearch = min(nextRecentSearch, GET_TICK() + getRecentIntervalMs());
		} else {
			nextRecentSearch = GET_TICK() + getRecentIntervalMs();
		}
		return nextRecentSearch;
	}
}

int BundleQueue::getRecentIntervalMs() const {
	int recentBundles = count_if(recentSearchQueue.begin(), recentSearchQueue.end(), [](BundlePtr b) { return b->allowAutoSearch(); });
	if (recentBundles == 1) {
		return 15 * 60 * 1000;
	} else if (recentBundles == 2) {
		return 8 * 60 * 1000;
	} else {
		return 5 * 60 * 1000;
	}
}

BundlePtr BundleQueue::findRecent() {
	if (recentSearchQueue.size() == 0) {
		return nullptr;
	}

	uint32_t count = 0;
	for (;;) {
		BundlePtr b = recentSearchQueue.front();
		recentSearchQueue.pop_front();

		//check if the bundle still belongs to here
		if (b->checkRecent()) {
			recentSearchQueue.push_back(b);
		} else {
			addSearchPrio(b);
		}

		if (b->allowAutoSearch()) {
			return b;
		} else if (count >= recentSearchQueue.size()) {
			break;
		}

		count++;
	}

	return nullptr;
}

boost::mt19937 gen;
static vector<double> probabilities;

int BundleQueue::getPrioSum() const {
	probabilities.clear();

	int prioBundles = 0;
	int p = Bundle::LOW;
	do {
		int dequeBundles = count_if(prioSearchQueue[p].begin(), prioSearchQueue[p].end(), [](BundlePtr b) { return b->allowAutoSearch(); });
		probabilities.push_back((p-1)*dequeBundles); //multiply with a priority factor to get bigger probability for higher prio bundles
		prioBundles += dequeBundles;
		p++;
	} while(p < Bundle::LAST);

	probabilities.shrink_to_fit();
	return prioBundles;
}

BundlePtr BundleQueue::findAutoSearch() {
	int prioBundles = getPrioSum();

	//do we have anything where to search from?
	if (prioBundles == 0) {
		return nullptr;
	}

	auto dist = boost::random::discrete_distribution<>(probabilities);

	//choose the search queue, can't be paused or lowest
	auto& sbq = prioSearchQueue[dist(gen) + 2];
	dcassert(!sbq.empty());

	//find the first item from the search queue that can be searched for
	auto s = find_if(sbq.begin(), sbq.end(), [](BundlePtr b) { return b->allowAutoSearch(); } );

	if (s != sbq.end()) {
		BundlePtr b = *s;
		//move to the back
		sbq.erase(s);
		sbq.push_back(b);
		return b;
	}

	return nullptr;
}

BundlePtr BundleQueue::find(const string& bundleToken) const {
	auto i = bundles.find(bundleToken);
	if (i != bundles.end()) {
		return i->second;
	}
	return nullptr;
}

BundlePtr BundleQueue::findDir(const string& aPath) const {
	string dir = AirUtil::getReleaseDir(aPath);
	if (dir.empty()) {
		return nullptr;
	}

	auto i = bundleDirs.find(dir);
	if (i != bundleDirs.end()) {
		return i->second;
	}
	return nullptr;
}

void BundleQueue::getInfo(const string& aSource, BundleList& retBundles, int& finishedFiles, int& fileBundles) const {
	BundlePtr tmpBundle;
	bool subFolder = false;

	//find the matching bundles
	for(auto j = bundles.cbegin(), iend = bundles.cend(); j != iend; ++j) {
		tmpBundle = j->second;
		if (tmpBundle->isFinished()) {
			//don't modify those
			continue;
		}

		if (AirUtil::isParentOrExact(aSource, tmpBundle->getTarget())) {
			//parent or the same dir
			retBundles.push_back(tmpBundle);
			if (tmpBundle->isFileBundle())
				fileBundles++;
		} else if (!tmpBundle->isFileBundle() && AirUtil::isSub(aSource, tmpBundle->getTarget())) {
			//subfolder
			retBundles.push_back(tmpBundle);
			subFolder = true;
			break;
		}
	}

	//count the finished files
	if (subFolder) {
		for_each(tmpBundle->getFinishedFiles(), [&](QueueItemPtr qi) { 
			if(AirUtil::isSub(qi->getTarget(), aSource)) 
				finishedFiles++; 
		});
	} else {
		for_each(retBundles, [&](BundlePtr b) { finishedFiles += b->getFinishedFiles().size(); });
	}
}

BundlePtr BundleQueue::getMergeBundle(const string& aTarget) const {
	/* Returns directory bundles that are in sub or parent dirs (or in the same location), in which we can merge to */
	for(auto i = bundles.cbegin(), iend = bundles.cend(); i != iend; ++i) {
		BundlePtr compareBundle = i->second;
		if (!compareBundle->isFileBundle() && (AirUtil::isSub(aTarget, compareBundle->getTarget()) || AirUtil::isParentOrExact(aTarget, compareBundle->getTarget()))) {
			return compareBundle;
		}
	}
	return nullptr;
}

void BundleQueue::getSubBundles(const string& aTarget, BundleList& retBundles) const {
	/* Returns bundles that are inside aTarget */
	for_each(bundles | map_values, [&](BundlePtr compareBundle) {
		if (AirUtil::isSub(compareBundle->getTarget(), aTarget)) {
			retBundles.push_back(compareBundle);
		}
	});
}

void BundleQueue::addBundleItem(QueueItemPtr qi, BundlePtr aBundle) {
	if (aBundle->addQueue(qi) && !aBundle->isFileBundle()) {
		string dir = Util::getDir(qi->getTarget(), false, false);
		string releaseDir = AirUtil::getReleaseDir(dir);
		if (!releaseDir.empty()) {
			bundleDirs[releaseDir] = aBundle;
		}
	}
}

void BundleQueue::removeBundleItem(QueueItemPtr qi, bool finished) {
	if (qi->getBundle()->removeQueue(qi, finished) && !finished && !qi->getBundle()->isFileBundle()) {
		string releaseDir = AirUtil::getReleaseDir(Util::getDir(qi->getTarget(), false, false));
		if (!releaseDir.empty()) {
			bundleDirs.erase(releaseDir);
		}
	}
}

void BundleQueue::addFinishedItem(QueueItemPtr qi, BundlePtr aBundle) {
	if (aBundle->addFinishedItem(qi, false) && !aBundle->isFileBundle()) {
		string dir = Util::getDir(qi->getTarget(), false, false);
		string releaseDir = AirUtil::getReleaseDir(dir);
		if (!releaseDir.empty()) {
			bundleDirs[releaseDir] = aBundle;
		}
	}
}

void BundleQueue::removeFinishedItem(QueueItemPtr qi) {
	if (qi->getBundle()->removeFinishedItem(qi) && !qi->getBundle()->isFileBundle()) {
		string releaseDir = AirUtil::getReleaseDir(Util::getDir(qi->getTarget(), false, false));
		if (!releaseDir.empty()) {
			bundleDirs.erase(releaseDir);
		}
	}
}

void BundleQueue::removeBundle(BundlePtr aBundle) {
	if (aBundle->isSet(Bundle::FLAG_NEW)) {
		return;
	}

	for_each(aBundle->getBundleDirs(), [&](pair<string, pair<uint32_t, uint32_t>> dirs) {
		string releaseDir = AirUtil::getReleaseDir(dirs.first);
		if (!releaseDir.empty()) {
			bundleDirs.erase(releaseDir);
		}
	});

	//make sure that everything will be freed from the memory
	dcassert(aBundle->getFinishedFiles().empty());
	dcassert(aBundle->getQueueItems().empty());

	/*for(auto i = aBundle->getFinishedFiles().begin(); i != aBundle->getFinishedFiles().end(); )
		aBundle->getFinishedFiles().erase(i);
	for(auto i = aBundle->getQueueItems().begin(); i != aBundle->getQueueItems().end(); )
		aBundle->getQueueItems().erase(i);*/

	removeSearchPrio(aBundle);
	bundles.erase(aBundle->getToken());

	aBundle->deleteBundleFile();
}

void BundleQueue::moveBundle(BundlePtr aBundle, const string& newTarget) {
	//remove the old release dir
	string releaseDir = AirUtil::getReleaseDir(aBundle->getTarget());
	if (!releaseDir.empty()) {
		bundleDirs.erase(releaseDir);
	}

	aBundle->setTarget(newTarget);

	//add new
	releaseDir = AirUtil::getReleaseDir(aBundle->getTarget());
	if (!releaseDir.empty()) {
		bundleDirs[releaseDir] = aBundle;
	}
}

void BundleQueue::getDiskInfo(TargetUtil::TargetInfoMap& dirMap, const StringSet& volumes) const {
	string tempVol;
	bool useSingleTempDir = (SETTING(TEMP_DOWNLOAD_DIRECTORY).find("%[targetdrive]") == string::npos);
	if (useSingleTempDir) {
		tempVol = TargetUtil::getMountPath(SETTING(TEMP_DOWNLOAD_DIRECTORY), volumes);
	}

	for_each(bundles | map_values, [&](BundlePtr b) {
		string mountPath = TargetUtil::getMountPath(b->getTarget(), volumes);
		if (!mountPath.empty()) {
			auto s = dirMap.find(mountPath);
			if (s != dirMap.end()) {
				bool countAll = (useSingleTempDir && (mountPath != tempVol));
				s->second.queued += b->getDiskUse(countAll);
			}
		}
	});
}

void BundleQueue::saveQueue(bool force) noexcept {
	try {
		for_each(bundles | map_values, [force](BundlePtr b) {
			if (!b->isFinished() && (b->getDirty() || force)) 
				b->save();
		});
	} catch(...) {
		// ...
	}
}

} //dcpp