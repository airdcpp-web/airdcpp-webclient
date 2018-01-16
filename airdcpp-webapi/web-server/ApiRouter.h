/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_APIROUTER_H
#define DCPLUSPLUS_DCPP_APIROUTER_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>

namespace webserver {
	class ApiRouter {
	public:
		ApiRouter();
		~ApiRouter();

		void handleSocketRequest(const std::string& aMessage, WebSocketPtr& aSocket, bool aIsSecure) noexcept;
		api_return handleHttpRequest(const std::string& aRequestPath, const websocketpp::http::parser::request& aRequest,
			json& output_, json& error_, bool aIsSecure, const string& aIp, const SessionPtr& aSession) noexcept;
	private:
		api_return handleRequest(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket, const string& aIp) noexcept;

		api_return routeAuthRequest(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket, const string& aIp);
	};
}

#endif