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
#include <web-server/ApiRouter.h>

#include <web-server/ApiRequest.h>
#include <web-server/WebUserManager.h>
#include <web-server/WebSocket.h>

#include <airdcpp/File.h>
#include <airdcpp/Util.h>
#include <airdcpp/StringTokenizer.h>

#include <sstream>

namespace webserver {
	using namespace dcpp;

	ApiRouter::ApiRouter() {
	}

	ApiRouter::~ApiRouter() {

	}

	void ApiRouter::handleSocketRequest(const string& aRequestBody, WebSocketPtr& aSocket, bool aIsSecure) noexcept {

		dcdebug("Received socket request: %s\n", aRequestBody.c_str());
		bool authenticated = aSocket->getSession() != nullptr;

		json responseJsonData, errorJson;
		websocketpp::http::status_code::value code;
		int callbackId = -1;

		try {
			json requestJson = json::parse(aRequestBody);
			auto cb = requestJson.find("callback_id");
			if (cb != requestJson.end()) {
				callbackId = cb.value();
			}

			ApiRequest apiRequest(requestJson["path"], requestJson["method"], responseJsonData, errorJson);
			apiRequest.parseSocketRequestJson(requestJson);
			apiRequest.setSession(aSocket->getSession());

			apiRequest.validate();

			code = handleRequest(apiRequest, aIsSecure, aSocket, aSocket->getIp());
		} catch (const std::exception& e) {
			errorJson = { "message", "Parsing failed: " + string(e.what()) };
			code = websocketpp::http::status_code::bad_request;
		}

		if (callbackId > 0) {
			aSocket->sendApiResponse(responseJsonData, errorJson, code, callbackId);
		}
	}

	websocketpp::http::status_code::value ApiRouter::handleHttpRequest(const string& aRequestPath,
		const SessionPtr& aSession, const string& aRequestBody, json& output_, json& error_,
		bool aIsSecure, const string& aRequestMethod, const string& aIp) noexcept {

		dcdebug("Received HTTP request: %s\n", aRequestBody.c_str());
		try {
			ApiRequest apiRequest(aRequestPath, aRequestMethod, output_, error_);
			apiRequest.validate();

			apiRequest.parseHttpRequestJson(aRequestBody);
			apiRequest.setSession(aSession);

			return handleRequest(apiRequest, aIsSecure, nullptr, aIp);
		} catch (const std::exception& e) {
			error_ = { "message", "Parsing failed: " + string(e.what()) };
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
	}

	api_return ApiRouter::handleRequest(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket, const string& aIp) noexcept {
		// Special case because we may not have the session yet
		if (aRequest.getApiModule() == "session") {
			return handleSessionRequest(aRequest, aIsSecure, aSocket, aIp);
		}

		// Require auth for all other modules
		if (!aRequest.getSession()) {
			aRequest.setResponseErrorStr("Not authorized");
			return websocketpp::http::status_code::unauthorized;
		}

		// Require using the same protocol that was used for logging in
		if (aRequest.getSession()->isSecure() != aIsSecure) {
			aRequest.setResponseErrorStr("Protocol mismatch");
			return websocketpp::http::status_code::not_acceptable;
		}

		int code;
		try {
			code = aRequest.getSession()->handleRequest(aRequest);
		} catch (const ArgumentException& e) {
			aRequest.setResponseErrorJson(e.getErrorJson());
			code = CODE_UNPROCESSABLE_ENTITY;
		} catch (const std::exception& e) {
			aRequest.setResponseErrorStr(e.what());
			code = websocketpp::http::status_code::bad_request;
		}

		return static_cast<api_return>(code);
	}

	api_return ApiRouter::handleSessionRequest(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket, const string& aIp) {
		if (aRequest.getApiVersion() != 0) {
			aRequest.setResponseErrorStr("Invalid API version");
			return websocketpp::http::status_code::precondition_failed;
		}

		if (aRequest.getStringParam(0) == "auth") {
			if (aRequest.getMethod() == ApiRequest::METHOD_POST) {
				return sessionApi.handleLogin(aRequest, aIsSecure, aSocket, aIp);
			} else if (aRequest.getMethod() == ApiRequest::METHOD_DELETE) {
				return sessionApi.handleLogout(aRequest);
			}
		} else if (aRequest.getStringParam(0) == "socket") {
			return sessionApi.handleSocketConnect(aRequest, aIsSecure, aSocket);
		}

		aRequest.setResponseErrorStr("Invalid command");
		return websocketpp::http::status_code::bad_request;
	}
}