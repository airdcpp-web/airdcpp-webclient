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

#ifndef DCPLUSPLUS_DCPP_HASHAPI_H
#define DCPLUSPLUS_DCPP_HASHAPI_H

#include <web-server/stdinc.h>

#include <api/ApiModule.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/HashManager.h>

namespace webserver {
	class HashApi : public ApiModule, private HashManagerListener {
	public:
		HashApi(Session* aSession);
		~HashApi();

		int getVersion() const noexcept {
			return 0;
		}
	private:
		static json formatDbStatus(bool aMaintenanceRunning) noexcept;
		void updateDbStatus(bool aMaintenanceRunning) noexcept;

		api_return handleOptimize(ApiRequest& aRequest);
		api_return handleGetDbStatus(ApiRequest& aRequest);

		void on(HashManagerListener::MaintananceStarted) noexcept;
		void on(HashManagerListener::MaintananceFinished) noexcept;
		//void on(ConnectivityManagerListener::Finished, bool /*v6*/, bool /*failed*/) noexcept;
		//virtual void on(SettingChanged) noexcept { }
	};
}

#endif