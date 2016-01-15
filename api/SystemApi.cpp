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

#include <web-server/JsonUtil.h>
#include <web-server/Timer.h>
#include <web-server/WebServerManager.h>

#include <api/SystemApi.h>
#include <api/common/Serializer.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/TimerManager.h>

namespace webserver {
	SystemApi::SystemApi(Session* aSession) : ApiModule(aSession, Access::ANY), timer(getTimer([this] { onTimer(); }, 500)) {

		METHOD_HANDLER("stats", Access::ANY, ApiRequest::METHOD_GET, (), false, SystemApi::handleGetStats);

		METHOD_HANDLER("away", Access::ANY, ApiRequest::METHOD_GET, (), false, SystemApi::handleGetAwayState);
		METHOD_HANDLER("away", Access::ANY, ApiRequest::METHOD_POST, (), true, SystemApi::handleSetAway);

		createSubscription("away_state");
		timer->start(true);
	}

	SystemApi::~SystemApi() {
		timer->stop(true);
	}

	void SystemApi::onTimer() noexcept {
		if (!subscriptionActive("away_state")) {
			return;
		}

		auto newState = serializeAwayState();
		if (newState == previousAway) {
			return;
		}

		previousAway = newState;
		send("away_state", newState);
	}

	string SystemApi::getAwayState() noexcept {
		switch (AirUtil::getAwayMode()) {
			case AWAY_OFF: return "off";
			case AWAY_MANUAL: return "manual";
			case AWAY_MINIMIZE:
			case AWAY_IDLE: return "idle";
		}

		dcassert(0);
		return "";
	}

	json SystemApi::serializeAwayState() noexcept {
		return {
			{ "state", getAwayState() },
			{ "away_idle_time", SETTING(AWAY_IDLE_TIME) },
		};
	}

	api_return SystemApi::handleGetAwayState(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeAwayState());
		return websocketpp::http::status_code::ok;
	}

	api_return SystemApi::handleSetAway(ApiRequest& aRequest) {
		auto away = JsonUtil::getField<bool>("away", aRequest.getRequestBody());
		AirUtil::setAway(away ? AWAY_MANUAL : AWAY_OFF);

		return websocketpp::http::status_code::ok;
	}

	api_return SystemApi::handleGetStats(ApiRequest& aRequest) {
		auto started = TimerManager::getStartTime();
		auto server = session->getServer();

		aRequest.setResponseBody({
			{ "server_threads", server->getServerThreads() },
			{ "client_started", started },
			{ "client_version", fullVersionString },
			{ "active_sessions", server->getUserManager().getSessionCount() },
		});
		return websocketpp::http::status_code::ok;
	}
}