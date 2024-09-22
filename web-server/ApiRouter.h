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

#ifndef DCPLUSPLUS_WEBSERVER_APIROUTER_H
#define DCPLUSPLUS_WEBSERVER_APIROUTER_H

#include "forward.h"

#include <airdcpp/typedefs.h>

namespace webserver {
	// struct HttpRequest;
	struct RouterRequest;

	class ApiRouter {
	public:

		// static void handleSocketRequest(const std::string& aMessage, const WebSocketPtr& aSocket, bool aIsSecure) noexcept;
		//static api_return handleHttpRequest(const HttpRequest& aRequest,
		//	json& output_, json& error_, const ApiDeferredHandler& aDeferredHandler) noexcept;
	// private:
		static api_return handleRequest(RouterRequest& aRequest) noexcept;

		static api_return routeAuthRequest(RouterRequest& aRequest);
	};
}

#endif