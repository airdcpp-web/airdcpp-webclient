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

#include "TargetUtil.h"

#include <airdcpp/FavoriteManager.h>
#include <airdcpp/QueueManager.h>
#include <airdcpp/ShareManager.h>

namespace dcpp {

void TargetUtil::getVirtualTarget(const string& aTarget, TargetUtil::TargetType targetType, TargetInfo& ti_) {
	if (targetType == TARGET_PATH) {
		ti_.setTarget(aTarget);
	} else {
		GroupedDirectoryMap directoryMap;
		if (targetType == TARGET_FAVORITE) {
			directoryMap = FavoriteManager::getInstance()->getGroupedFavoriteDirs();
		} else {
			directoryMap = ShareManager::getInstance()->getGroupedDirectories();
		}

		auto s = directoryMap.find(aTarget);
		if (s != directoryMap.end()) {
			getTarget(s->second, ti_);
		} else {
			// Use the default one
			ti_.setTarget(SETTING(DOWNLOAD_DIRECTORY));
		}
	}
}

void TargetUtil::getTarget(const OrderedStringSet& aTargets, TargetInfo& retTi_) {
	auto volumes = File::getVolumes();
	TargetInfoMap targetMap;

	for(const auto& i: aTargets) {
		auto target = File::getMountPath(i, volumes);
		if (!target.empty() && targetMap.find(target) == targetMap.end()) {
			auto free = File::getFreeSpace(target);
			if (free > 0) {
				targetMap[target] = TargetInfo(i, free);
			}
		}
	}

	if (targetMap.empty()) {
		//failed to get the volumes
		if (!aTargets.empty()) {
			retTi_.setTarget(*aTargets.begin());
		} else {
			retTi_.setTarget(SETTING(DOWNLOAD_DIRECTORY));
		}

		retTi_.setFreeDiskSpace(File::getFreeSpace(retTi_.getTarget()));
	} else {
		compareMap(targetMap, retTi_);
		if (retTi_.getTarget().empty()) {
			//no dir with enough space, choose the one with most space available
			compareMap(targetMap, retTi_);
		}
	}
}

void TargetUtil::compareMap(const TargetInfoMap& aTargetMap, TargetInfo& retTi_) {

	for (auto mapTi: aTargetMap | map_values) {
		if (mapTi.getFreeDiskSpace() > retTi_.getFreeDiskSpace() || !retTi_.isInitialized()) {
			retTi_ = mapTi;
		}
	}
}

} //dcpp