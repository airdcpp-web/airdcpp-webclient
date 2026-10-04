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

#ifndef DCPLUSPLUS_WEBSERVER_HTTP_MANAGER_H
#define DCPLUSPLUS_WEBSERVER_HTTP_MANAGER_H

#include "stdinc.h"

#include "FileServer.h"

#include "HttpRequest.h"
#include "HttpUtil.h"
#include "WebServerManager.h"
#include "WebUserManager.h"
#include "IServerEndpoint.h"

#include <airdcpp/util/AppUtil.h>
#include <airdcpp/util/Util.h>


namespace webserver {
	class HttpManager {
	public:
		explicit HttpManager(WebServerManager* aWsm) noexcept : wsm(aWsm) {}

		const FileServer& getFileServer() const noexcept {
			return fileServer;
		}

		HttpManager(HttpManager&) = delete;
		HttpManager& operator=(HttpManager&) = delete;

		void setEndpointHandlers(IServerEndpoint& aEndpoint, bool aIsSecure) {
			aEndpoint.setHttpHandler(std::bind_front(&HttpManager::handleHttpRequest, this, std::ref(aEndpoint), aIsSecure));
		}

		void start(const string& aWebResourcePath) noexcept;
		void stop() noexcept;

		static constexpr int64_t MAX_HTTP_BODY_SIZE = 16LL * 1024 * 1024; // 16 MiB default
	private:
		static api_return handleApiRequest(const HttpRequest& aRequest,
			json& output_, json& error_, const ApiDeferredHandler& aDeferredHandler) noexcept;

		// Returns false in case of invalid token format
		bool getOptionalHttpSession(IServerEndpoint& ep, ConnectionHdl hdl, const string& aIp, SessionPtr& session_);

		bool setHttpResponse(IServerEndpoint& ep, ConnectionHdl hdl, http::status aStatus, const string& aOutput);

		void handleHttpApiRequest(const HttpRequest& aRequest, IServerEndpoint& ep, ConnectionHdl hdl);
		void handleHttpFileRequest(const HttpRequest& aRequest, IServerEndpoint& ep, ConnectionHdl hdl);

		void handleHttpRequest(IServerEndpoint& ep, bool aIsSecure, ConnectionHdl hdl);

		WebServerManager* wsm;
		FileServer fileServer;
	};
}

#endif // DCPLUSPLUS_DCPP_WEBSERVER_H
