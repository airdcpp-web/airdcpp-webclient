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

#include <web-server/version.h>
#include <web-server/ApiRouter.h>
#include <web-server/HttpUtil.h>

#include <web-server/ApiRequest.h>
#include <web-server/Session.h>

#include <api/SessionApi.h>

#include <sstream>

namespace webserver {
	api_return ApiRouter::handleRequest(RouterRequest& aRequest) noexcept {
		auto& apiRequest = aRequest.apiRequest;
		if (apiRequest.getApiVersion() != API_VERSION) {
			apiRequest.setResponseErrorStr("Unsupported API version");
			return http_status::precondition_failed;
		}

		int code;
		try {
			// Special case because we may not have the session yet
			if (apiRequest.getApiModule() == "sessions" && !apiRequest.getSession()) {
				return routeAuthRequest(aRequest);
			}

			// Require auth for all other modules
			if (!apiRequest.getSession()) {
				apiRequest.setResponseErrorStr("Not authorized");
				return http_status::unauthorized;
			}

			// Require using the same protocol that was used for logging in
			if (apiRequest.getSession()->getSessionType() != Session::TYPE_BASIC_AUTH && (apiRequest.getSession()->getSessionType() == Session::TYPE_SECURE) != aRequest.isSecure) {
				apiRequest.setResponseErrorStr("Protocol mismatch");
				return http_status::not_acceptable;
			}

			apiRequest.getSession()->updateActivity();

			code = apiRequest.getSession()->handleRequest(apiRequest);
		} catch (const ArgumentException& e) {
			apiRequest.setResponseErrorJson(e.toJSON());
			code = CODE_UNPROCESSABLE_ENTITY;
		} catch (const RequestException& e) {
			apiRequest.setResponseErrorStr(e.what());
			code = e.getCode();
		} catch (const std::exception& e) {
			apiRequest.setResponseErrorStr(e.what());
			code = http_status::bad_request;
		}

		dcassert(HttpUtil::isStatusOk(code) || code == CODE_DEFERRED || apiRequest.hasErrorMessage());
		return static_cast<api_return>(code);
	}

	api_return ApiRouter::routeAuthRequest(RouterRequest& aRequest) {
		auto& apiRequest = aRequest.apiRequest;
		if (apiRequest.getPathTokenAt(0) == "authorize" && apiRequest.getMethod() == METHOD_POST) {
			return SessionApi::handleLogin(aRequest);
		} else if (apiRequest.getPathTokenAt(0) == "socket" && apiRequest.getMethod() == METHOD_POST) {
			return SessionApi::handleSocketConnect(aRequest);
		}

		apiRequest.setResponseErrorStr("Invalid command/method (not authenticated)");
		return http_status::bad_request;
	}
}