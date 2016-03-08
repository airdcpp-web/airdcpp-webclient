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

#ifndef DCPLUSPLUS_DCPP_SESSIONAPI_H
#define DCPLUSPLUS_DCPP_SESSIONAPI_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>

namespace webserver {
	class SessionApi {
	public:
		SessionApi();

		api_return handleLogin(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket, const string& aIp);
		api_return handleSocketConnect(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket);
		api_return handleLogout(ApiRequest& aRequest);
		api_return handleActivity(ApiRequest& aRequest);

		json getSystemInfo(const string& aIp) const noexcept;
	private:
		static string getHostname() noexcept;
	};
}

#endif