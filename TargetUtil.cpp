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

#include "stdinc.h"

#include "FavoriteManager.h"
#include "QueueManager.h"
#include "ShareManager.h"
#include "TargetUtil.h"

#ifdef _WIN32
#include <ShlObj.h>
#include <direct.h>
#endif

namespace dcpp {

string TargetUtil::getMountPath(const string& aPath, const VolumeSet& aVolumes) {
	if (aVolumes.find(aPath) != aVolumes.end()) {
		return aPath;
	}

	auto l = aPath.length();
	for (;;) {
		l = aPath.rfind(PATH_SEPARATOR, l-2);
		if (l == string::npos || l <= 1)
			break;
		if (aVolumes.find(aPath.substr(0, l+1)) != aVolumes.end()) {
			return aPath.substr(0, l+1);
		}
	}

#ifdef WIN32
	//not found from the volumes... network path? this won't work with mounted dirs
	if (aPath.length() > 2 && aPath.substr(0,2) == "\\\\") {
		l = aPath.find("\\", 2);
		if (l != string::npos) {
			//get the drive letter
			l = aPath.find("\\", l+1);
			if (l != string::npos) {
				return aPath.substr(0, l+1);
			}
		}
	}
#endif
	return Util::emptyString;
}

bool TargetUtil::getVirtualTarget(const string& aTarget, TargetUtil::TargetType targetType, TargetInfo& ti_, const int64_t& aSize) {
	if (targetType == TARGET_PATH) {
		ti_.targetDir = aTarget;
	} else {
		vector<pair<string, StringList>> dirList;
		if (targetType == TARGET_FAVORITE) {
			dirList = FavoriteManager::getInstance()->getFavoriteDirs();
		} else {
			dirList = ShareManager::getInstance()->getGroupedDirectories();
		}

		auto s = find_if(dirList.begin(), dirList.end(), CompareFirst<string, StringList>(aTarget));
		if (s != dirList.end()) {
			const auto& targets = s->second;
			bool tmp = getTarget(targets, ti_, aSize);
			if (!ti_.targetDir.empty()) {
				return tmp;
			}
		}
	}

	if (ti_.targetDir.empty()) {
		//failed to get the target, use the default one
		ti_.targetDir = SETTING(DOWNLOAD_DIRECTORY);
	}
	return getDiskInfo(ti_);
}

bool TargetUtil::getTarget(const StringList& targets, TargetInfo& retTi_, const int64_t& aSize) {
	VolumeSet volumes;
	getVolumes(volumes);
	TargetInfoMap targetMap;

	for(auto& i: targets) {
		string target = getMountPath(i, volumes);
		if (!target.empty() && targetMap.find(target) == targetMap.end()) {
			auto free = File::getFreeSpace(target);
			if (free > 0) {
				targetMap[target] = TargetInfo(i, free);
			}
		}
	}

	if (targetMap.empty()) {
		//failed to get the volumes
		if (!targets.empty()) {
			retTi_.targetDir = targets.front();
		} else {
			retTi_.targetDir = SETTING(DOWNLOAD_DIRECTORY);
		}

		retTi_.diskSpace = File::getFreeSpace(retTi_.targetDir);
		return retTi_.getFreeSpace() >= aSize;
	}

	QueueManager::getInstance()->getDiskInfo(targetMap, volumes);

	compareMap(targetMap, retTi_, aSize, SETTING(DL_AUTOSELECT_METHOD));
	if (retTi_.targetDir.empty()) //no dir with enough space, choose the one with most space available
		compareMap(targetMap, retTi_, aSize, (int8_t)SettingsManager::SELECT_MOST_SPACE);

	return retTi_.getFreeSpace() >= aSize;
}

void TargetUtil::compareMap(const TargetInfoMap& aTargetMap, TargetInfo& retTi_, const int64_t& aSize, int8_t aMethod) {

	for (auto mapTi: aTargetMap | map_values) {
		if (aMethod == (int8_t)SettingsManager::SELECT_LEAST_SPACE) {
			int64_t diff = mapTi.getFreeSpace() - aSize;
			if (diff > 0 && (diff < (retTi_.getFreeSpace() - aSize) || !retTi_.isInitialized()))
				retTi_ = mapTi;
		} else if (mapTi.getFreeSpace() > retTi_.getFreeSpace() || !retTi_.isInitialized()) {
			retTi_ = mapTi;
		}
	}
}

bool TargetUtil::getDiskInfo(TargetInfo& targetInfo_) {
	VolumeSet volumes;
	getVolumes(volumes);
	string pathVol = getMountPath(targetInfo_.targetDir, volumes);
	if (pathVol.empty()) {
		return false;
	}

	targetInfo_.diskSpace = File::getFreeSpace(pathVol);

	TargetInfoMap targetMap;
	targetMap[pathVol] = targetInfo_;

	QueueManager::getInstance()->getDiskInfo(targetMap, volumes);
	targetInfo_ = targetMap[pathVol];
	return true;
}

void TargetUtil::getVolumes(VolumeSet& volumes) {

#ifdef WIN32
	TCHAR   buf[MAX_PATH];  
	HANDLE  hVol;    
	BOOL    found;
	TCHAR   buf2[MAX_PATH];

	// lookup drive volumes.
	hVol = FindFirstVolume(buf, MAX_PATH);
	if(hVol != INVALID_HANDLE_VALUE) {
		found = true;
		//while we find drive volumes.
		while(found) {
			if(GetDriveType(buf) != DRIVE_CDROM && GetVolumePathNamesForVolumeName(buf, buf2, MAX_PATH, NULL)) {
				volumes.insert(Text::fromT(buf2));
			}
			found = FindNextVolume( hVol, buf, MAX_PATH );
		}
   		found = FindVolumeClose(hVol);
	}

	// and a check for mounted Network drives, todo fix a better way for network space
	ULONG drives = _getdrives();
	TCHAR drive[3] = { _T('A'), _T(':'), _T('\0') };

	while(drives != 0) {
		if(drives & 1 && ( GetDriveType(drive) != DRIVE_CDROM && GetDriveType(drive) == DRIVE_REMOTE) ){
			string path = Text::fromT(drive);
			if( path[ path.length() -1 ] != PATH_SEPARATOR ) {
				path += PATH_SEPARATOR;
			}
			volumes.insert(path);
		}

		++drive[0];
		drives = (drives >> 1);
	}
#endif
}

void TargetUtil::reportInsufficientSize(const TargetInfo& ti, int64_t aSize) {
	string tmp;
	if (ti.queued > 0) {
		tmp = STRING(AUTO_SEARCH) + ": " + STRING_F(NOT_ENOUGH_SPACE_QUEUED_PAUSED,
			ti.targetDir %
			Util::formatBytes(ti.diskSpace) % 
			Util::formatBytes(ti.queued) %
			Util::formatBytes(aSize));
	} else {
		tmp = STRING(AUTO_SEARCH) + ": " + STRING_F(NOT_ENOUGH_SPACE_PAUSED,
			ti.targetDir.c_str() %
			Util::formatBytes(ti.getFreeSpace()) % 
			Util::formatBytes(aSize));
	}
	LogManager::getInstance()->message(tmp, LogManager::LOG_WARNING);
}

string TargetUtil::getInsufficientSizeMessage(const TargetInfo& ti, int64_t aSize) {
	string tmp;
	if (ti.queued > 0) {
		tmp = STRING_F(CONFIRM_SIZE_WARNING_QUEUE, 
			Util::formatBytes(ti.queued).c_str() %
			ti.targetDir.c_str() %
			Util::formatBytes(ti.diskSpace).c_str() % 
			Util::formatBytes(aSize).c_str());
	} else {
		tmp = STRING_F(CONFIRM_SIZE_WARNING, 
			Util::formatBytes(ti.getFreeSpace()).c_str() % 
			ti.targetDir.c_str() %
			Util::formatBytes(aSize).c_str());
	}
	return tmp;
}

} //dcpp