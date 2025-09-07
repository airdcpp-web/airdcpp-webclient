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

#include <web-server/WebServerManager.h>
#include <web-server/WebUserManager.h>

#include <api/base/ApiModule.h>

namespace webserver {
	ApiModule::ApiModule(Session* aSession) : session(aSession) {

	}

	ApiModule::~ApiModule() {

	}

	optional<ApiRequest::NamedParamMap> ApiModule::RequestHandler::matchParams(const ApiRequest::PathTokenList& aPathTokens) const noexcept {
		if (method == METHOD_FORWARD) {
			if (aPathTokens.size() < params.size()) {
				return nullopt;
			}
		} else if (aPathTokens.size() != params.size()) {
			return nullopt;
		}

		for (auto i = 0; i < static_cast<int>(params.size()); i++) {
			try {
				if (!boost::regex_search(aPathTokens[i], params[i].reg)) {
					return nullopt;
				}
			} catch (const std::runtime_error&) {
				return nullopt;
			}
		}

		ApiRequest::NamedParamMap paramMap;
		for (auto i = 0; i < static_cast<int>(params.size()); i++) {
			paramMap[params[i].id] = aPathTokens[i];
		}

		return paramMap;
	}

	api_return ApiModule::handleRequest(ApiRequest& aRequest) {
		bool hasParamNameMatch = false; // for better error reporting

		// Match parameters
		auto handler = find_if(requestHandlers.begin(), requestHandlers.end(), [&](const RequestHandler& aHandler) {
			// Regular matching
			auto namedParams = aHandler.matchParams(aRequest.getPathTokens());
			if (!namedParams) {
				return false;
			}

			if (aHandler.method == aRequest.getMethod() || aHandler.method == METHOD_FORWARD) {
				aRequest.setNamedParams(*namedParams);
				return true;
			}

			hasParamNameMatch = true;
			return false;
		});

		if (handler == requestHandlers.end()) {
			if (hasParamNameMatch) {
				aRequest.setResponseErrorStr("Method " + aRequest.getMethodStr() + " is not supported for this handler");
				return http_status::method_not_allowed;
			}

			aRequest.setResponseErrorStr("The supplied URL " + aRequest.getRequestPath() + " doesn't match any method in this API module");
			return http_status::bad_request;
		}

		// Check permission
		if (!session->getUser()->hasPermission(handler->access)) {
			aRequest.setResponseErrorStr("The permission " + WebUser::accessToString(handler->access) + " is required for accessing this method");
			return http_status::forbidden;
		}

		return handler->f(aRequest);
	}

	TimerPtr ApiModule::getTimer(Callback&& aTask, time_t aIntervalMillis) {
		return session->getServer()->addTimer(std::move(aTask), aIntervalMillis,
			std::bind(&ApiModule::asyncRunWrapper, std::placeholders::_1, session->getId())
		);
	}

	Callback ApiModule::getAsyncWrapper(Callback&& aTask) noexcept {
		auto sessionId = session->getId();
		return [task = std::move(aTask), sessionId] {
			return asyncRunWrapper(task, sessionId);
		};
	}

	void ApiModule::asyncRunWrapper(const Callback& aTask, LocalSessionId aSessionId) {
		// Ensure that the session (and socket) won't be deleted
		auto s = WebServerManager::getInstance()->getUserManager().getSession(aSessionId);
		if (!s) {
			return;
		}

		aTask();
	}

	void ApiModule::addAsyncTask(Callback&& aTask) {
		session->getServer()->addAsyncTask(getAsyncWrapper(std::move(aTask)));
	}
}