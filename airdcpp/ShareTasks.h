/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SHARE_TASKS_H
#define DCPLUSPLUS_DCPP_SHARE_TASKS_H


#include <airdcpp/TimerManagerListener.h>

#include <airdcpp/Message.h>
#include <airdcpp/ShareDirectoryInfo.h>
#include <airdcpp/ShareRefreshInfo.h>
#include <airdcpp/ShareRefreshTask.h>
#include <airdcpp/TaskQueue.h>
#include <airdcpp/Thread.h>

namespace dcpp {

class ErrorCollector;
class SharePathValidator;

class ShareTasks: private Thread {
public:
	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	void shutdown() noexcept;

	bool isRefreshing() const noexcept { return refreshRunning; }

	// Abort filelist refresh (or an individual refresh task)
	RefreshPathList abortRefresh(optional<ShareRefreshTaskToken> aToken = nullopt) noexcept;

	ShareRefreshTaskList getRefreshTasks() const noexcept;

	explicit(false) ShareTasks(ShareTasksManager* const aManager);
	~ShareTasks() override;

	// Add directories for refresh
	RefreshTaskQueueInfo addRefreshTask(ShareRefreshPriority aPriority, const StringList& aDirs, ShareRefreshType aRefreshType, const string& aDisplayName = Util::emptyString, const ProgressFunction& aProgressF = nullptr) noexcept;
private:
	ShareTasksManager* const manager;

	struct TaskData {
		virtual ~TaskData() = default;
	};

	struct RefreshTask : public TaskData {
		explicit RefreshTask(int refreshOptions_) : refreshOptions(refreshOptions_) { }
		int refreshOptions;
	};

	TaskQueue tasks;
	
	static atomic_flag tasksRunning;
	bool refreshRunning = false;

	// Display a log message if the refresh can't be started immediately
	void reportPendingRefresh(ShareRefreshType aTask, const RefreshPathList& aDirectories, const string& aDisplayName) const noexcept;

	// Remove directories that have already been queued for refresh
	void validateRefreshTask(StringList& dirs_) noexcept;

	int run() override;

	void runTasks(const ProgressFunction& progressF = nullptr) noexcept;
	void runRefreshTask(const ShareRefreshTask& aTask, const ProgressFunction& progressF) noexcept;

	void reportTaskStatus(const ShareRefreshTask& aTask, bool aFinished, const ShareRefreshStats* aStats) const noexcept;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SHARE_TASKS_H)
