/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_HASHAPI_H
#define DCPLUSPLUS_DCPP_HASHAPI_H

#include <api/base/ApiModule.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/HashManager.h>

namespace webserver {
	class HashApi : public SubscribableApiModule, private HashManagerListener {
	public:
		HashApi(Session* aSession);
		~HashApi();
	private:
		json previousStats;
		void onTimer() noexcept;

		json serializeHashStatistics(const HashManager::HashStats& aStats) noexcept;

		static json formatDbStatus(bool aMaintenanceRunning) noexcept;
		void updateDbStatus(bool aMaintenanceRunning) noexcept;

		api_return handlePause(ApiRequest& aRequest);
		api_return handleResume(ApiRequest& aRequest);
		api_return handleStop(ApiRequest& aRequest);

		api_return handleOptimize(ApiRequest& aRequest);
		api_return handleGetDbStatus(ApiRequest& aRequest);
		api_return handleGetStats(ApiRequest& aRequest);

		void on(HashManagerListener::DirectoryHashed, const string& aPath, int aFilesHashed, int64_t aSizeHashed, time_t aHashDuration, int aHasherId) noexcept override;
		void on(HashManagerListener::HasherFinished, int aDirshashed, int aFilesHashed, int64_t aSizeHashed, time_t aHashDuration, int aHasherId) noexcept override;

		void on(HashManagerListener::MaintananceStarted) noexcept override;
		void on(HashManagerListener::MaintananceFinished) noexcept override;

		TimerPtr timer;
	};
}

#endif