/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_CONNECTIVITYAPI_H
#define DCPLUSPLUS_DCPP_CONNECTIVITYAPI_H

#include <api/base/SubscribableApiModule.h>

#include <airdcpp/core/header/typedefs.h>
#include <airdcpp/connectivity/ConnectivityManagerListener.h>

namespace webserver {
	class ConnectivityApi : public SubscribableApiModule, private ConnectivityManagerListener {
	public:
		ConnectivityApi(Session* aSession);
		~ConnectivityApi();
	private:
		static json formatStatus(bool v6) noexcept;

		api_return handleDetect(ApiRequest& aRequest);
		api_return handleGetStatus(ApiRequest& aRequest);

		void on(ConnectivityManagerListener::Message, const LogMessagePtr& aMessage) noexcept override;
		void on(ConnectivityManagerListener::Started, bool /*v6*/) noexcept override;
		void on(ConnectivityManagerListener::Finished, bool /*v6*/, bool /*failed*/) noexcept override;
		//virtual void on(SettingChanged) noexcept { }
	};
}

#endif