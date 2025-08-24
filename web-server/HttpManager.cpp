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

#include "stdinc.h"

#include <web-server/HttpManager.h>
#include <web-server/ApiRouter.h>
#include <web-server/HttpUtil.h>

#include <web-server/ApiRequest.h>
#include <web-server/HttpRequest.h>
#include <web-server/Session.h>


namespace webserver {

	void HttpManager::start(const string& aWebResourcePath) noexcept {
		if (!aWebResourcePath.empty()) {
			fileServer.setResourcePath(aWebResourcePath);
		} else {
			fileServer.setResourcePath(AppUtil::getPath(AppUtil::PATH_RESOURCES) + "web-resources" + PATH_SEPARATOR);
		}
	}

	void HttpManager::stop() noexcept {
		fileServer.stop();
	}

	http_status HttpManager::handleApiRequest(const HttpRequest& aRequest, json& output_, json& error_, const ApiDeferredHandler& aDeferredHandler) noexcept
	{
		const auto& httpRequest = aRequest.httpRequest;
		dcdebug("Received HTTP request: %s\n", httpRequest.get_body().c_str());

		json bodyJson;
		if (!httpRequest.get_body().empty()) {
			try {
				bodyJson = json::parse(httpRequest.get_body());
			} catch (const std::exception& e) {
				error_ = ApiRequest::toResponseErrorStr("Failed to parse JSON: " + string(e.what()));
				return http_status::bad_request;
			}
		}

		try {
			ApiRequest apiRequest(aRequest.path, httpRequest.get_method(), std::move(bodyJson), aRequest.session, aDeferredHandler, output_, error_);
			RouterRequest routerRequest{ apiRequest, aRequest.secure, nullptr, aRequest.ip };
			const auto status = ApiRouter::handleRequest(routerRequest);
			return status;
		} catch (const std::invalid_argument& e) {
			error_ = ApiRequest::toResponseErrorStr(string(e.what()));
		}

		return http_status::bad_request;
	}
}