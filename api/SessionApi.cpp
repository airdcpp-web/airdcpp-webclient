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

#include <api/SessionApi.h>
#include <api/SystemApi.h>
#include <api/WebUserUtils.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>
#include <web-server/WebSocket.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/WebUserManager.h>

#include <airdcpp/hub/activity/ActivityManager.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/core/version.h>

namespace webserver {
	SessionApi::SessionApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::ADMIN) 
	{
		createSubscriptions({ "session_created", "session_removed" });

		// Just fail these since we have a session already...
		METHOD_HANDLER(Access::ANY, METHOD_POST, (EXACT_PARAM("authorize")), SessionApi::failAuthenticatedRequest);
		METHOD_HANDLER(Access::ANY, METHOD_POST, (EXACT_PARAM("socket")), SessionApi::failAuthenticatedRequest);

		// Methods for the current session
		METHOD_HANDLER(Access::ANY, METHOD_POST, (EXACT_PARAM("activity")), SessionApi::handleActivity);

		METHOD_HANDLER(Access::ANY, METHOD_GET, (EXACT_PARAM("self")), SessionApi::handleGetCurrentSession);
		METHOD_HANDLER(Access::ANY, METHOD_DELETE, (EXACT_PARAM("self")), SessionApi::handleRemoveCurrentSession);

		// Admin methods
		METHOD_HANDLER(Access::ADMIN, METHOD_GET, (), SessionApi::handleGetSessions);
		METHOD_HANDLER(Access::ADMIN, METHOD_GET, (TOKEN_PARAM), SessionApi::handleGetSession);
		METHOD_HANDLER(Access::ADMIN, METHOD_DELETE, (TOKEN_PARAM), SessionApi::handleRemoveSession);

		aSession->getServer()->getUserManager().addListener(this);
	}

	SessionApi::~SessionApi() {
		session->getServer()->getUserManager().removeListener(this);
	}

	api_return SessionApi::failAuthenticatedRequest(ApiRequest& aRequest) {
		aRequest.setResponseErrorStr("This method can't be used after authentication");
		return http_status::precondition_failed;
	}

	api_return SessionApi::handleActivity(ApiRequest& aRequest) {
		// This may also be used to prevent the session from expiring

		if (JsonUtil::getOptionalFieldDefault<bool>("user_active", aRequest.getRequestBody(), false)) {
			ActivityManager::getInstance()->updateActivity();
		}

		return http_status::no_content;
	}

	api_return SessionApi::handleRemoveCurrentSession(ApiRequest& aRequest) {
		return logout(aRequest, aRequest.getSession());
	}

	api_return SessionApi::logout(ApiRequest& aRequest, const SessionPtr& aSession) {
		if (aSession->getSessionType() == Session::TYPE_BASIC_AUTH) {
			aRequest.setResponseErrorStr("Sessions using basic authentication can't be deleted");
			return http_status::bad_request;
		}

		session->getServer()->getUserManager().logout(aSession);
		return http_status::no_content;
	}

	http_status SessionApi::handleLogin(RouterRequest& aRequest) {
		auto& apiRequest = aRequest.apiRequest;
		auto& um = WebServerManager::getInstance()->getUserManager();

		const auto& reqJson = apiRequest.getRequestBody();


		auto grantType = JsonUtil::getOptionalFieldDefault<string>("grant_type", reqJson, "password");
		auto inactivityMinutes = JsonUtil::getOptionalFieldDefault<uint64_t>("max_inactivity", reqJson, WEBCFG(DEFAULT_SESSION_IDLE_TIMEOUT).uint64());


		SessionPtr session = nullptr;
		auto sessionType = aRequest.isSecure ? Session::TYPE_SECURE : Session::TYPE_PLAIN;

		if (grantType == "password") {
			auto username = JsonUtil::getField<string>("username", reqJson, false);
			auto password = JsonUtil::getField<string>("password", reqJson, false);

			try {
				session = um.authenticateSession(username, password,
					sessionType, inactivityMinutes, aRequest.ip);
			} catch (const std::domain_error& e) {
				apiRequest.setResponseErrorStr(e.what());
				return http_status::unauthorized;
			}
		} else if (grantType == "refresh_token") {
			auto refreshToken = JsonUtil::getField<string>("refresh_token", reqJson, false);

			try {
				session = um.authenticateSession(refreshToken,
					sessionType, inactivityMinutes, aRequest.ip);
			} catch (const std::domain_error& e) {
				apiRequest.setResponseErrorStr(e.what());
				return http_status::bad_request;
			}
		} else {
			JsonUtil::throwError("grant_type", JsonException::ERROR_INVALID, "Invalid grant_type");
			return http_status::bad_request;
		}

		dcassert(session);

		if (aRequest.authenticationCallback) {
			aRequest.authenticationCallback(session);
		}


		apiRequest.setResponseBody(serializeLoginInfo(session, um.createRefreshToken(session->getUser())));
		return http_status::ok;
	}

	json SessionApi::serializeLoginInfo(const SessionPtr& aSession, const string& aRefreshToken) {
		json ret = {
			{ "session_id", aSession->getId() },
			{ "auth_token", aSession->getAuthToken() },
			{ "token_type", "Bearer" },
			{ "user", Serializer::serializeItem(aSession->getUser(), WebUserUtils::propertyHandler) },
			{ "system_info", SystemApi::getSystemInfo() },
			{ "wizard_pending", SETTING(WIZARD_PENDING) },
		};

		if (!aRefreshToken.empty()) {
			ret.emplace("refresh_token", aRefreshToken);
		}

		return ret;
	}

	api_return SessionApi::handleSocketConnect(RouterRequest& aRequest) {
		auto& apiRequest = aRequest.apiRequest;
		if (!aRequest.authenticationCallback) {
			apiRequest.setResponseErrorStr("This method may be called only via a websocket");
			return http_status::bad_request;
		}

		auto sessionToken = JsonUtil::getField<string>("auth_token", apiRequest.getRequestBody(), false);

		auto session = WebServerManager::getInstance()->getUserManager().getSession(sessionToken);
		if (!session) {
			apiRequest.setResponseErrorStr("Invalid session token");
			return http_status::bad_request;
		}

		if ((session->getSessionType() == Session::TYPE_SECURE) != aRequest.isSecure) {
			apiRequest.setResponseErrorStr("Invalid protocol");
			return http_status::bad_request;
		}

		aRequest.authenticationCallback(session);

		apiRequest.setResponseBody(serializeLoginInfo(session, Util::emptyString));
		return http_status::no_content;
	}

	api_return SessionApi::handleGetSessions(ApiRequest& aRequest) {
		auto sessions = session->getServer()->getUserManager().getSessions();
		aRequest.setResponseBody(Serializer::serializeList(sessions, serializeSession));
		return http_status::ok;
	}

	SessionPtr SessionApi::parseSessionParam(ApiRequest& aRequest) {
		auto sessionId = aRequest.getTokenParam();
		auto s = session->getServer()->getUserManager().getSession(sessionId);
		if (!s) {
			throw RequestException(http_status::not_found, "Session " + Util::toString(sessionId) + " was not found");
		}

		return s;
	}

	api_return SessionApi::handleGetSession(ApiRequest& aRequest) {
		auto s = parseSessionParam(aRequest);
		aRequest.setResponseBody(serializeSession(s));
		return http_status::ok;
	}

	api_return SessionApi::handleRemoveSession(ApiRequest& aRequest) {
		auto s = parseSessionParam(aRequest);
		return logout(aRequest, s);
	}

	api_return SessionApi::handleGetCurrentSession(ApiRequest& aRequest) const {
		aRequest.setResponseBody(serializeSession(aRequest.getSession()));
		return http_status::ok;
	}

	string SessionApi::getSessionType(const SessionPtr& aSession) noexcept {
		switch (aSession->getSessionType()) {
			case Session::TYPE_BASIC_AUTH: return "basic_auth";
			case Session::TYPE_PLAIN: return "plain";
			case Session::TYPE_SECURE: return "secure";
			case Session::TYPE_EXTENSION: return "extension";
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
		maybeSend("session_created", [&aSession] {
			return json({
				{ "session", serializeSession(aSession) },
			});
		});
	}

	void SessionApi::on(WebUserManagerListener::SessionRemoved, const SessionPtr& aSession, int aReason) noexcept {
		maybeSend("session_removed", [&aSession, &aReason] {
			return json({
				{ "session", serializeSession(aSession) },
				{ "timed_out", static_cast<WebUserManager::SessionRemovalReason>(aReason) == WebUserManager::SessionRemovalReason::TIMEOUT },
			});
		});
	}
}