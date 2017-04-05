/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SHAREMONITOR_MANAGER_H
#define DCPLUSPLUS_DCPP_SHAREMONITOR_MANAGER_H

#include <airdcpp/forward.h>
#include <airdcpp/typedefs.h>
#include <airdcpp/GetSet.h>

#include <airdcpp/ShareManagerListener.h>
#include <airdcpp/TimerManagerListener.h>

#include <airdcpp/ShareDirectoryInfo.h>
#include <airdcpp/Singleton.h>

#include "DirectoryMonitorListener.h"
#include "DirectoryMonitor.h"

namespace dcpp {
	class ShareMonitorManager : public Singleton<ShareMonitorManager>, private DirectoryMonitorListener, private ShareManagerListener, private TimerManagerListener
	{
	public:
		ShareMonitorManager();
		~ShareMonitorManager();

		void startup() noexcept;
		string printStats() const noexcept;

		// Call when a drive has been removed and it should be removed from monitoring
		// Monitoring won't fail it otherwise and the monitoring will neither be restored if the device is readded
		void deviceRemoved(const string& aDrive);

		// Handle monitoring changes (being called regularly from TimerManager so manual calls aren't mandatory)
		void handleChangedFiles() noexcept;

		// Called when the monitoring mode has been changed
		void rebuildMonitoring() noexcept;

		IGETSET(bool, monitorDebug, MonitorDebug, false);
	private:
		DirectoryMonitor monitor;

		static bool useMonitoring(const ShareDirectoryInfoPtr& aRootInfo) noexcept;

		void addMonitoring(const StringList& aPaths) noexcept;
		void removeMonitoring(const StringList& aPaths) noexcept;

		class DirModifyInfo {
		public:
			typedef deque<DirModifyInfo> List;
			DirModifyInfo(const string& aPath);

			void updateActivity() noexcept;

			time_t getLastActivity() const noexcept {
				return lastActivity;
			}

			const string& getVolume() const noexcept {
				return volume;
			}

			GETSET(string, path, Path);
		private:
			string volume;
			time_t lastActivity;
		};

		typedef set<string, Util::PathSortOrderBool> PathSet;

		DirModifyInfo::List fileModifications;

		struct FileItem {
			const string path;
			const bool isDirectory;
		};

		optional<FileItem> ShareMonitorManager::checkModifiedPath(const string& aPath) noexcept;
		void addModifyInfo(const string& aPath) noexcept;

		// Recursively removes all notifications for the given path
		void removeNotifications(const string& aPath) noexcept;

		DirModifyInfo::List::iterator findModifyInfo(const string& aFile) noexcept;
		void handleChangedFiles(uint64_t aTick, bool aForced = false) noexcept;
		bool handleModifyInfo(DirModifyInfo& aInfo, uint64_t aTick, bool aForced) noexcept;

		void restoreFailedMonitoredPaths();

		//DirectoryMonitorListener
		virtual void on(DirectoryMonitorListener::FileCreated, const string& aPath) noexcept;
		virtual void on(DirectoryMonitorListener::FileModified, const string& aPath) noexcept;
		virtual void on(DirectoryMonitorListener::FileRenamed, const string& aOldPath, const string& aNewPath) noexcept;
		virtual void on(DirectoryMonitorListener::FileDeleted, const string& aPath) noexcept;
		virtual void on(DirectoryMonitorListener::Overflow, const string& aPath) noexcept;
		virtual void on(DirectoryMonitorListener::DirectoryFailed, const string& aPath, const string& aError) noexcept;

		// ShareManagerListener
		void on(ShareManagerListener::RootCreated, const string& aPath) noexcept;
		void on(ShareManagerListener::RootRemoved, const string& aPath) noexcept;
		void on(ShareManagerListener::RootUpdated, const string& aPath) noexcept;
		void on(ShareManagerListener::RefreshQueued, uint8_t aTaskType, const RefreshPathList& aPaths) noexcept;

		// TimerManagerListener
		void on(TimerManagerListener::Second, uint64_t tick) noexcept;
		void on(TimerManagerListener::Minute, uint64_t tick) noexcept;

		string lastMessage;
		uint64_t messageTick = 0;
		void reportFile(const string& aMessage) noexcept;
	};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SHAREMONITOR_MANAGER_H)