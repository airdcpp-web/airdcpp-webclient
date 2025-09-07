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

#ifndef DCPLUSPLUS_DCPP_SHARE_REFRESHTASK_H
#define DCPLUSPLUS_DCPP_SHARE_REFRESHTASK_H

#include <string>

#include <airdcpp/forward.h>
#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/core/queue/Task.h>

namespace dcpp {

enum RefreshTaskType : uint8_t {
	REFRESH,
};

enum class RefreshTaskQueueResult : uint8_t {
	STARTED,
	QUEUED,
	EXISTS,
};

struct RefreshTaskQueueInfo {
	optional<ShareRefreshTaskToken> token;
	RefreshTaskQueueResult result;
};

enum class ShareRefreshType : uint8_t {
	ADD_ROOT_DIRECTORY,
	REFRESH_DIRS,
	REFRESH_INCOMING,
	REFRESH_ALL,
	STARTUP,
	BUNDLE
};

enum class ShareRefreshPriority : uint8_t {
	MANUAL,
	SCHEDULED,
	NORMAL,
	BLOCKING,
};

struct ShareRefreshTask : public Task {
	ShareRefreshTask(ShareRefreshTaskToken aToken, const RefreshPathList& aDirs, const string& aDisplayName, ShareRefreshType aRefreshType, ShareRefreshPriority aPriority);

	const ShareRefreshTaskToken token;
	const RefreshPathList dirs;
	const string displayName;
	const ShareRefreshType type;
	const ShareRefreshPriority priority;

	bool canceled = false;
	bool running = false;
};

typedef std::vector<ShareRefreshTask> ShareRefreshTaskList;

struct ShareTasksManager {
	struct RefreshTaskHandler {
		virtual void refreshCompleted(bool, const ShareRefreshTask&, const ShareRefreshStats&) {}
		virtual bool refreshPath(const string&, const ShareRefreshTask&, ShareRefreshStats&) { 
			return false; 
		}
	};

	virtual shared_ptr<RefreshTaskHandler> startRefresh(const ShareRefreshTask& aTask) noexcept = 0;
	virtual void onRefreshQueued(const ShareRefreshTask& aTask) noexcept = 0;
};


}

#endif