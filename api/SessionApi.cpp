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

#include <web-server/stdinc.h>

#include <api/SessionApi.h>
#include <api/SystemApi.h>
#include <api/WebUserUtils.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>
#include <web-server/WebSocket.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/WebUserManager.h>

#include <airdcpp/ActivityManager.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/version.h>

namespace webserver {
	SessionApi::SessionApi(Session* aSession) : SubscribableApiModule(aSession, Access::ADMIN) {
		METHOD_HANDLER("activity", Access::ANY, ApiRequest::METHOD_POST, (), false, SessionApi::handleActivity);
		METHOD_HANDLER("auth", Access::ANY, ApiRequest::METHOD_DELETE, (), false, SessionApi::handleLogout);

		// Just fail these...
		METHOD_HANDLER("auth", Access::ANY, ApiRequest::METHOD_POST, (), false, SessionApi::failAuthenticatedRequest);
		METHOD_HANDLER("socket", Access::ANY, ApiRequest::METHOD_POST, (), false, SessionApi::failAuthenticatedRequest);

		METHOD_HANDLER("sessions", Access::ADMIN, ApiRequest::METHOD_GET, (), false, SessionApi::handleGetSessions);
		METHOD_HANDLER("session", Access::ANY, ApiRequest::METHOD_GET, (), false, SessionApi::handleGetCurrentSession);

		aSession->getServer()->getUserManager().addListener(this);

		createSubscription("session_created");
		createSubscription("session_removed");
	}

	SessionApi::~SessionApi() {
		session->getServer()->getUserManager().removeListener(this);
	}

	api_return SessionApi::failAuthenticatedRequest(ApiRequest& aRequest) {
		aRequest.setResponseErrorStr("This method can't be used after authentication");
		return websocketpp::http::status_code::precondition_failed;
	}

	api_return SessionApi::handleActivity(ApiRequest& aRequest) {
		if (!aRequest.getSession()->isUserSession()) {
			// This can be used to prevent the session from expiring
			return websocketpp::http::status_code::no_content;
		}

		ActivityManager::getInstance()->updateActivity();
		return websocketpp::http::status_code::no_content;
	}

	api_return SessionApi::handleLogout(ApiRequest& aRequest) {
		WebServerManager::getInstance()->logout(aRequest.getSession()->getId());
		return websocketpp::http::status_code::no_content;
	}

	websocketpp::http::status_code::value SessionApi::handleLogin(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket, const string& aIP) {
		const auto& reqJson = aRequest.getRequestBody();

		auto username = JsonUtil::getField<string>("username", reqJson, false);
		auto password = JsonUtil::getField<string>("password", reqJson, false);

		auto inactivityMinutes = JsonUtil::getOptionalFieldDefault<uint64_t>("max_inactivity", reqJson, WEBCFG(DEFAULT_SESSION_IDLE_TIMEOUT).uint64());
		auto userSession = JsonUtil::getOptionalFieldDefault<bool>("user_session", reqJson, false);

		auto session = WebServerManager::getInstance()->getUserManager().authenticateSession(username, password, 
			aIsSecure, inactivityMinutes, userSession, aIP);

		if (!session) {
			aRequest.setResponseErrorStr("Invalid username or password");
			return websocketpp::http::status_code::unauthorized;
		}

		if (aSocket) {
			session->onSocketConnected(aSocket);
			aSocket->setSession(session);
		}

		aRequest.setResponseBody({
			{ "token", session->getAuthToken() },
			{ "session", serializeSession(session) },
			{ "system", SystemApi::getSystemInfo() },
			{ "permissions", session->getUser()->getPermissions() }, // deprecated
			{ "user", session->getUser()->getUserName() }, // deprecated
			{ "run_wizard", SETTING(WIZARD_RUN) }, // deprecated
			{ "cid", ClientManager::getInstance()->getMyCID().toBase32() }, // deprecated
		});

		return websocketpp::http::status_code::ok;
	}

	api_return SessionApi::handleSocketConnect(ApiRequest& aRequest, bool aIsSecure, const WebSocketPtr& aSocket) {
		auto sessionToken = JsonUtil::getField<string>("authorization", aRequest.getRequestBody(), false);

		auto session = WebServerManager::getInstance()->getUserManager().getSession(sessionToken);
		if (!session) {
			aRequest.setResponseErrorStr("Invalid session token");
			return websocketpp::http::status_code::bad_request;
		}

		if ((session->getSessionType() == Session::TYPE_SECURE)  != aIsSecure) {
			aRequest.setResponseErrorStr("Invalid protocol");
			return websocketpp::http::status_code::bad_request;
		}

		session->onSocketConnected(aSocket);
		aSocket->setSession(session);

		return websocketpp::http::status_code::no_content;
	}

	api_return SessionApi::handleGetSessions(ApiRequest& aRequest) {
		auto sessions = session->getServer()->getUserManager().getSessions();

		auto ret = json::array();
		for (const auto& s : sessions) {
			ret.push_back(serializeSession(s));
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	api_return SessionApi::handleGetCurrentSession(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeSession(aRequest.getSession()));
		return websocketpp::http::status_code::ok;
	}

	string SessionApi::getSessionType(const SessionPtr& aSession) noexcept {
		switch (aSession->getSessionType()) {
			case Session::TYPE_BASIC_AUTH: return "basic_auth";
			case Session::TYPE_PLAIN: return "plain";
			case Session::TYPE_SECURE: return "secure";
		}

		dcassert(0);
		return "";
	}

	json SessionApi::serializeSession(const SessionPtr& aSession) noexcept {
		return {
			{ "id", aSession->getId() },
			{ "type", getSessionType(aSession) },
			{ "last_activity", GET_TICK() - aSession->getLastActivity() },
			{ "ip", aSession->getIp() },
			{ "user", Serializer::serializeItem(aSession->getUser(), WebUserUtils::propertyHandler) }
		};
	}

	void SessionApi::on(WebUserManagerListener::SessionCreated, const SessionPtr& aSession) noexcept {
		maybeSend("session_created", [&] {
			return json({
				{ "session", serializeSession(aSession) },
			});
		});
	}

	void SessionApi::on(WebUserManagerListener::SessionRemoved, const SessionPtr& aSession, bool aTimedOut) noexcept {
		maybeSend("session_removed", [&] {
			return json({
				{ "session", serializeSession(aSession) },
				{ "timed_out", aTimedOut },
			});
		});
	}
}