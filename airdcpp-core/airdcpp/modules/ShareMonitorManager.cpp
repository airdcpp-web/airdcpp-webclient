/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#include "ShareMonitorManager.h"

#include <airdcpp/AirUtil.h>
#include <airdcpp/File.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/ShareManager.h>

namespace dcpp {
	ShareMonitorManager::ShareMonitorManager() : monitor(1, false) {

	}

	ShareMonitorManager::~ShareMonitorManager() {
		ShareManager::getInstance()->removeListener(this);
		TimerManager::getInstance()->removeListener(this);
		monitor.removeListener(this);
	}

	bool ShareMonitorManager::useMonitoring(const ShareDirectoryInfoPtr& aRootInfo) noexcept {
		return SETTING(MONITORING_MODE) == SettingsManager::MONITORING_ALL || (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_INCOMING && aRootInfo->incoming);
	}

	void ShareMonitorManager::startup() noexcept {
		ShareManager::getInstance()->addListener(this);
		TimerManager::getInstance()->addListener(this);
		monitor.addListener(this);

		monitor.callAsync([=] { rebuildMonitoring(); });
	}

	void ShareMonitorManager::setMonitorDebug(bool aEnabled) noexcept {
		monitorDebug = aEnabled;

		monitor.setDebug(aEnabled);
	}

	string ShareMonitorManager::printStats() const noexcept {
		string ret = "\r\n\r\n-=[ Monitoring statistics ]=-\r\n\r\n";
		if (monitor.hasDirectories()) {
			ret += "Debug mode: ";
			ret += (monitorDebug ? "Enabled" : "Disabled");
			ret += " \r\n\r\nMonitored paths:\r\n";
			ret += monitor.getStats();
		} else {
			ret += "No folders are being monitored\r\n";
		}

		return ret;
	}

	void ShareMonitorManager::on(ShareManagerListener::RefreshQueued, uint8_t /*aTaskType*/, const RefreshPathList& aPaths) noexcept {
		for (const auto& p: aPaths) {
			monitor.callAsync([=] { removeNotifications(p); });
		}
	}

	void ShareMonitorManager::on(ShareManagerListener::RootCreated, const string& aPath) noexcept {
		auto rootInfo = ShareManager::getInstance()->getRootInfo(aPath);
		if (useMonitoring(rootInfo)) {
			addMonitoring({ aPath });
		}
	}

	void ShareMonitorManager::on(ShareManagerListener::RootRemoved, const string& aPath) noexcept {
		removeMonitoring({ aPath });
	}

	void ShareMonitorManager::on(ShareManagerListener::RootUpdated, const string& aPath) noexcept {
		auto rootInfo = ShareManager::getInstance()->getRootInfo(aPath);
		if (useMonitoring(rootInfo)) {
			addMonitoring({ rootInfo->path });
		} else {
			removeMonitoring({ rootInfo->path });
		}
	}

	void ShareMonitorManager::on(TimerManagerListener::Second, uint64_t /*tick*/) noexcept {
		while (monitor.dispatch()) {
			//...
		}
	}

	void ShareMonitorManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
		handleChangedFiles(aTick, false);
		restoreFailedMonitoredPaths();
	}

	void ShareMonitorManager::rebuildMonitoring() noexcept {
		StringList monAdd;
		StringList monRem;

		{
			auto roots = ShareManager::getInstance()->getRootInfos();
			for (const auto& rootInfo : roots) {
				if (useMonitoring(rootInfo)) {
					monAdd.push_back(rootInfo->path);
				} else {
					monRem.push_back(rootInfo->path);
				}
			}
		}

		addMonitoring(monAdd);
		removeMonitoring(monRem);
	}

	void ShareMonitorManager::restoreFailedMonitoredPaths() {
		auto restored = monitor.restoreFailedPaths();
		for (const auto& dir : restored) {
			LogManager::getInstance()->message(STRING_F(MONITORING_RESTORED_X, dir), LogMessage::SEV_INFO);
		}
	}

	void ShareMonitorManager::deviceRemoved(const string& aDrive) {
		monitor.deviceRemoved(aDrive);
	}

	void ShareMonitorManager::removeNotifications(const string& aPath) noexcept {
		auto p = findModifyInfo(aPath);
		if (p != fileModifications.end()) {
			fileModifications.erase(p);
		}
	}

	void ShareMonitorManager::addMonitoring(const StringList& aPaths) noexcept {
		int added = 0;
		for (const auto& p : aPaths) {
			try {
				if (monitor.addDirectory(p))
					added++;
			} catch (const MonitorException& e) {
				LogManager::getInstance()->message(STRING_F(FAILED_ADD_MONITORING, p % e.getError()), LogMessage::SEV_ERROR);
			}
		}

		if (added > 0) {
			LogManager::getInstance()->message(STRING_F(X_MONITORING_ADDED, added), LogMessage::SEV_INFO);
		}
	}

	void ShareMonitorManager::removeMonitoring(const StringList& aPaths) noexcept {
		int removed = 0;
		for (const auto& p : aPaths) {
			try {
				if (monitor.removeDirectory(p))
					removed++;
			} catch (const MonitorException& e) {
				LogManager::getInstance()->message("Error occurred when trying to remove the folder " + p + " from monitoring: " + e.getError(), LogMessage::SEV_ERROR);
			}
		}

		if (removed > 0) {
			LogManager::getInstance()->message(STRING_F(X_MONITORING_REMOVED, removed), LogMessage::SEV_INFO);
		}
	}

	ShareMonitorManager::DirModifyInfo::DirModifyInfo(const string& aPath) : path(aPath), volume(File::getMountPath(aPath)) {
		updateActivity();
	}

	void ShareMonitorManager::DirModifyInfo::updateActivity() noexcept {
		lastActivity = GET_TICK();
	}

	ShareMonitorManager::DirModifyInfo::List::iterator ShareMonitorManager::findModifyInfo(const string& aFile) noexcept {
		return find_if(fileModifications.begin(), fileModifications.end(), [&aFile](const DirModifyInfo& dmi) { 
			return AirUtil::isParentOrExactLocal(dmi.getPath(), aFile) || AirUtil::isSubLocal(dmi.getPath(), aFile); 
		});
	}

	void ShareMonitorManager::handleChangedFiles() noexcept {
		monitor.callAsync([this] { handleChangedFiles(GET_TICK(), true); });
	}

	bool ShareMonitorManager::handleModifyInfo(DirModifyInfo& info, uint64_t aTick, bool aForced) noexcept {
		if (!aForced) {
			// Not enough time ellapsed from the last change?
			if (SETTING(DELAY_COUNT_MODE) == SettingsManager::DELAY_DIR) {
				if (info.getLastActivity() + static_cast<uint64_t>(SETTING(MONITORING_DELAY) * 1000) > aTick) {
					return false;
				}
			} else {
				if (any_of(fileModifications.begin(), fileModifications.end(), [&](const DirModifyInfo& aInfo) {
					if (SETTING(DELAY_COUNT_MODE) == SettingsManager::DELAY_VOLUME && compare(aInfo.getVolume(), info.getVolume()) != 0)
						return false;
					return aInfo.getLastActivity() + static_cast<uint64_t>(SETTING(MONITORING_DELAY) * 1000) > aTick;
				})) {
					return false;
				}
			}
		}

		ShareManager::getInstance()->refreshPaths({ info.getPath() });
		return true;
	}

	void ShareMonitorManager::handleChangedFiles(uint64_t aTick, bool aForced /*false*/) noexcept {
		for (auto k = fileModifications.begin(); k != fileModifications.end();) {
			if (handleModifyInfo(*k, aTick, aForced)) {
				k = fileModifications.erase(k);
			}
			else {
				k++;
			}
		}
	}

	void ShareMonitorManager::reportFile(const string& aMsg) noexcept {
		// There may be sequential modification notifications so don't spam the same message many times
		if (lastMessage != aMsg || messageTick + 3000 < GET_TICK()) {
			LogManager::getInstance()->message(aMsg, LogMessage::SEV_INFO);
			lastMessage = aMsg;
			messageTick = GET_TICK();
		}
	};

	optional<ShareMonitorManager::FileItem> ShareMonitorManager::checkModifiedPath(const string& aPath) noexcept {
		FileFindIter f(aPath);
		if (f != FileFindIter()) {
			auto isDirectory = f->isDirectory();
			auto path = isDirectory ? aPath + PATH_SEPARATOR : aPath;

			try {
				ShareManager::getInstance()->validatePath(path, false);
			} catch (const ShareException& e) {
				reportFile(e.getError());
				return nullopt;
			} catch (...) {
				return nullopt;
			}

			return FileItem({ path, isDirectory });
		}

		return nullopt;
	}

	void ShareMonitorManager::addModifyInfo(const string& aPath) noexcept {
		auto p = findModifyInfo(aPath);
		if (p == fileModifications.end()) {
			//add a new modify info
			fileModifications.emplace_front(aPath);
		} else {
			if (AirUtil::isSubLocal((*p).getPath(), aPath))
				(*p).setPath(aPath);

			p->updateActivity();
		}
	}

	void ShareMonitorManager::on(DirectoryMonitorListener::DirectoryFailed, const string& aPath, const string& aError) noexcept {
		LogManager::getInstance()->message(STRING_F(MONITOR_DIR_FAILED, aPath % aError), LogMessage::SEV_ERROR);
	}

	void ShareMonitorManager::on(DirectoryMonitorListener::FileCreated, const string& aPath) noexcept {
		if (monitorDebug)
			LogManager::getInstance()->message("File added: " + aPath, LogMessage::SEV_INFO);

		auto fileItem = checkModifiedPath(aPath);
		if (fileItem) {
			auto path = (*fileItem).isDirectory ? (*fileItem).path : Util::getFilePath(aPath);
			addModifyInfo(path);
		}
	}

	void ShareMonitorManager::on(DirectoryMonitorListener::FileModified, const string& aPath) noexcept {
		if (monitorDebug)
			LogManager::getInstance()->message("File modified: " + aPath, LogMessage::SEV_INFO);

		auto fileItem = checkModifiedPath(aPath);
		if (!fileItem) {
			return;
		}

		if (!(*fileItem).isDirectory) { // modified directories won't matter
			addModifyInfo(Util::getFilePath(aPath));
		}
	}

	void ShareMonitorManager::on(DirectoryMonitorListener::FileRenamed, const string& aOldPath, const string& aNewPath) noexcept {
		if (monitorDebug) {
			LogManager::getInstance()->message("File renamed, old: " + aOldPath + " new: " + aNewPath, LogMessage::SEV_INFO);
		}

		addModifyInfo(Util::getFilePath(aNewPath));
	}

	void ShareMonitorManager::on(DirectoryMonitorListener::FileDeleted, const string& aPath) noexcept {
		if (monitorDebug) {
			LogManager::getInstance()->message("File deleted: " + aPath, LogMessage::SEV_INFO);
		}

		// Refresh the parent
		addModifyInfo(Util::getFilePath(aPath));
	}

	void ShareMonitorManager::on(DirectoryMonitorListener::Overflow, const string& aRootPath) noexcept {
		if (monitorDebug)
			LogManager::getInstance()->message("Monitoring overflow: " + aRootPath, LogMessage::SEV_INFO);

		// Refresh the root
		ShareManager::getInstance()->refreshPaths({ aRootPath });
	}
} // namespace dcpp
