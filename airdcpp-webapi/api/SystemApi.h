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

#ifndef DCPLUSPLUS_DCPP_SYSTEMAPI_H
#define DCPLUSPLUS_DCPP_SYSTEMAPI_H

#include <web-server/stdinc.h>
#include <api/ApiModule.h>

#include <airdcpp/ActivityManager.h>


namespace webserver {
	class SystemApi : public SubscribableApiModule, private ActivityManagerListener {
	public:
		SystemApi(Session* aSession);
		~SystemApi();

		int getVersion() const noexcept {
			return 0;
		}
	private:
		static string getAwayState(AwayMode aAwayMode) noexcept;
		static json serializeAwayState() noexcept;

		api_return handleGetAwayState(ApiRequest& aRequest);
		api_return handleSetAway(ApiRequest& aRequest);

		api_return handleGetStats(ApiRequest& aRequest);

		void on(ActivityManagerListener::AwayModeChanged, AwayMode aNewMode) noexcept;
	};
}

#endif