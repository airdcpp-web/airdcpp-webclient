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

#include <api/SessionApi.h>

#include <web-server/WebSocket.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebUserManager.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/Socket.h>
#include <airdcpp/TimerManager.h>
#include <airdcpp/version.h>

namespace webserver {
	SessionApi::SessionApi() {

	}

	json SessionApi::getSystemInfo(const string& aIp) const noexcept {
		json retJson;
		retJson["path_separator"] = PATH_SEPARATOR_STR;

		
		// IPv4 addresses will be mapped to IPv6
		auto ip = aIp;
		auto v6 = aIp.find(":") != string::npos;
		if (aIp.find("[::ffff:") == 0) {
			auto end = aIp.rfind("]");
			ip = aIp.substr(8, end-8);
			v6 = false;
		}

		retJson["network_type"] = Util::isPrivateIp(ip, v6) ? "local" : "internet";

		auto started = TimerManager::getStartTime();
		retJson["client_started"] = started;
		retJson["client_version"] = fullVersionString;
#ifdef WIN32
		retJson["platform"] = "windows";
#elif APPLE
		retJson["platform"] = "osx";
#else
		retJson["platform"] = "other";
#endif

		retJson["start_total_downloaded"] = SETTING(TOTAL_DOWNLOAD) - Socket::getTotalDown();
		retJson["start_total_uploaded"] = SETTING(TOTAL_UPLOAD) - Socket::getTotalUp();
		return retJson;
	}

	websocketpp::http::status_code::value SessionApi::handleLogin(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket, const string& aIp) {
		const auto& reqJson = aRequest.getRequestBody();
		auto session = WebServerManager::getInstance()->getUserManager().authenticate(reqJson["username"], reqJson["password"], aIsSecure);

		if (!session) {
			aRequest.setResponseErrorStr("Invalid username or password");
			return websocketpp::http::status_code::unauthorized;
		}

		json retJson = {
			{ "token", session->getToken() },
			{ "user", session->getUser()->getUserName() },
			{ "system", getSystemInfo(aIp) }
		};

		if (aSocket) {
			session->onSocketConnected(aSocket);
			aSocket->setSession(session);
		}

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
	}

	api_return SessionApi::handleLogout(ApiRequest& aRequest) {
		if (!aRequest.getSession()) {
			aRequest.setResponseErrorStr("Not authorized");
			return websocketpp::http::status_code::unauthorized;
		}

		WebServerManager::getInstance()->logout(aRequest.getSession()->getToken());

		return websocketpp::http::status_code::ok;
	}

	api_return SessionApi::handleSocketConnect(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket) {
		std::string sessionToken = aRequest.getRequestBody()["authorization"];

		SessionPtr session = WebServerManager::getInstance()->getUserManager().getSession(sessionToken);
		if (!session) {
			aRequest.setResponseErrorStr("Invalid session token");
			return websocketpp::http::status_code::bad_request;
		}

		if (session->isSecure() != aIsSecure) {
			aRequest.setResponseErrorStr("Invalid protocol");
			return websocketpp::http::status_code::bad_request;
		}

		session->onSocketConnected(aSocket);
		aSocket->setSession(session);

		return websocketpp::http::status_code::ok;
	}
}