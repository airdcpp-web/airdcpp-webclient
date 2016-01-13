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

#include <web-server/stdinc.h>
#include <web-server/WebSocket.h>
#include <web-server/WebServerManager.h>

#include <api/ApiModule.h>

namespace webserver {
	ApiModule::ApiModule(Session* aSession, Access aSubscriptionAccess, const StringList* aSubscriptions) : session(aSession), subscriptionAccess(aSubscriptionAccess) {
		socket = WebServerManager::getInstance()->getSocket(aSession->getToken());

		if (aSubscriptions) {
			for (const auto& s : *aSubscriptions) {
				subscriptions.emplace(s, false);
			}
		}

		aSession->addListener(this);

		METHOD_HANDLER("listener", aSubscriptionAccess, ApiRequest::METHOD_POST, (STR_PARAM), false, ApiModule::handleSubscribe);
		METHOD_HANDLER("listener", aSubscriptionAccess, ApiRequest::METHOD_DELETE, (STR_PARAM), false, ApiModule::handleUnsubscribe);
	}

	ApiModule::~ApiModule() {
		session->removeListener(this);
		socket = nullptr;
	}

	void ApiModule::on(SessionListener::SocketConnected, const WebSocketPtr& aSocket) noexcept {
		socket = aSocket;
	}

	void ApiModule::on(SessionListener::SocketDisconnected) noexcept {
		// Disable all subscriptions
		for (auto& s : subscriptions) {
			s.second = false;
		}

		socket = nullptr;
	}

	bool ApiModule::RequestHandler::matchParams(const ApiRequest::RequestParamList& aRequestParams) const noexcept {
		if (isModuleHandler()) {
			// The request needs to contain more params than the handler has (submodule section is required)
			if (aRequestParams.size() <= params.size()) {
				return false;
			}
		} else if (aRequestParams.size() != params.size()) {
			return false;
		}

		for (auto i = 0; i < static_cast<int>(params.size()); i++) {
			if (!params[i].match(aRequestParams[i])) {
				return false;
			}
		}

		return true;
	}

	api_return ApiModule::handleRequest(ApiRequest& aRequest) {
		// Find section
		auto i = requestHandlers.find(aRequest.getStringParam(0));
		if (i == requestHandlers.end()) {
			aRequest.setResponseErrorStr("Invalid API section");
			return websocketpp::http::status_code::bad_request;
		}

		aRequest.popParam();
		const auto& sectionHandlers = i->second;

		bool hasParamMatch = false; // for better error reporting

		// Match parameters
		auto handler = boost::find_if(sectionHandlers, [&](const RequestHandler& aHandler) {
			// Regular matching
			auto matchesParams = aHandler.matchParams(aRequest.getParameters());
			if (!matchesParams) {
				return false;
			}

			if (aHandler.method == aRequest.getMethod() || aHandler.isModuleHandler()) {
				return true;
			}

			hasParamMatch = true;
			return false;
		});

		if (handler == sectionHandlers.end()) {
			if (hasParamMatch) {
				aRequest.setResponseErrorStr("Method not supported for this command");
			} else {
				aRequest.setResponseErrorStr("Invalid parameters for this API section");
			}

			return websocketpp::http::status_code::bad_request;
		}

		// Check JSON payload
		if (handler->requireJson && !aRequest.hasRequestBody()) {
			aRequest.setResponseErrorStr("JSON body required");
			return websocketpp::http::status_code::bad_request;
		}

		// Check permission
		if (!session->getUser()->hasPermission(handler->access)) {
			aRequest.setResponseErrorStr("Permission denied");
			return websocketpp::http::status_code::forbidden;
		}

		// Exact params could be removed from the request...

		return handler->f(aRequest);
	}

	api_return ApiModule::handleSubscribe(ApiRequest& aRequest) {
		if (!socket) {
			aRequest.setResponseErrorStr("Socket required");
			return websocketpp::http::status_code::precondition_required;
		}

		const auto& subscription = aRequest.getStringParam(0);
		if (!subscriptionExists(subscription)) {
			aRequest.setResponseErrorStr("No such subscription: " + subscription);
			return websocketpp::http::status_code::not_found;
		}

		setSubscriptionState(subscription, true);
		return websocketpp::http::status_code::ok;
	}

	api_return ApiModule::handleUnsubscribe(ApiRequest& aRequest) {
		auto subscription = aRequest.getStringParam(0);
		if (subscriptionExists(subscription)) {
			setSubscriptionState(subscription, false);
			return websocketpp::http::status_code::ok;
		}

		return websocketpp::http::status_code::not_found;
	}

	bool ApiModule::send(const json& aJson) {
		// Ensure that the socket won't be deleted while sending the message...
		auto s = socket;
		if (!s) {
			return false;
		}

		s->sendPlain(aJson.dump(4));
		return true;
	}

	bool ApiModule::send(const string& aSubscription, const json& aJson) {
		json j;
		j["event"] = aSubscription;
		j["data"] = aJson;

		return send(j);
	}

	bool ApiModule::maybeSend(const string& aSubscription, JsonCallback aCallback) {
		if (!subscriptionActive(aSubscription)) {
			return false;
		}

		return send(aSubscription, aCallback());
	}

	void ApiModule::addAsyncTask(CallBack&& aTask) {
		session->getServer()->addAsyncTask([=] {
			asyncRunWrapper(move(aTask));
		});
	}

	TimerPtr ApiModule::getTimer(CallBack&& aTask, time_t aIntervalMillis) {
		return session->getServer()->addTimer([=] {
			asyncRunWrapper(move(aTask));
		}, aIntervalMillis);
	}

	void ApiModule::asyncRunWrapper(const CallBack& aTask) {
		// Ensure that the session (and socket) won't be deleted
		auto s = session->getServer()->getUserManager().getSession(session->getToken());
		if (!s) {
			return;
		}

		aTask();
	}
}