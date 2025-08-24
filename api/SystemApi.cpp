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

#include <web-server/JsonUtil.h>
#include <web-server/SystemUtil.h>
#include <web-server/Timer.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/WebUserManager.h>

#include <api/SystemApi.h>
#include <api/common/Serializer.h>

#include <airdcpp/hub/activity/ActivityManager.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/core/localization/Localization.h>
#include <airdcpp/core/thread/Thread.h>
#include <airdcpp/core/timer/TimerManager.h>

namespace webserver {
	SystemApi::SystemApi(Session* aSession) : SubscribableApiModule(aSession, Access::ANY) {

		createSubscriptions({ "away_state" });

		METHOD_HANDLER(Access::ANY, METHOD_GET,		(EXACT_PARAM("stats")),			SystemApi::handleGetStats);

		METHOD_HANDLER(Access::ANY, METHOD_GET,		(EXACT_PARAM("away")),			SystemApi::handleGetAwayState);
		METHOD_HANDLER(Access::ANY, METHOD_POST,	(EXACT_PARAM("away")),			SystemApi::handleSetAway);

		METHOD_HANDLER(Access::ADMIN, METHOD_POST,	(EXACT_PARAM("restart_web")),	SystemApi::handleRestartWeb);
		METHOD_HANDLER(Access::ADMIN, METHOD_POST,	(EXACT_PARAM("shutdown")),		SystemApi::handleShutdown);

		METHOD_HANDLER(Access::ANY, METHOD_GET,		(EXACT_PARAM("system_info")),	SystemApi::handleGetSystemInfo);

		ActivityManager::getInstance()->addListener(this);
	}

	SystemApi::~SystemApi() {
		ActivityManager::getInstance()->removeListener(this);
	}

	// We can't stop the server from a server pool thread...
	class SystemActionThread : public Thread {
	public:
		typedef shared_ptr<SystemActionThread> Ptr;
		SystemActionThread(Ptr& aPtr, bool aShutDown) : thisPtr(aPtr), shutdown(aShutDown) {
			start();
		}

		int run() override {
			sleep(500);
			if (shutdown) {
				WebServerManager::getInstance()->getShutdownF()();
			} else {
				WebServerManager::getInstance()->stop();
				WebServerManager::getInstance()->start(nullptr);
			}

			thisPtr.reset();
			return 0;
		}
	private:
		Ptr& thisPtr;
		const bool shutdown;
	};
	static SystemActionThread::Ptr systemActionThread;

	api_return SystemApi::handleRestartWeb(ApiRequest&) {
		systemActionThread = make_shared<SystemActionThread>(systemActionThread, false);
		return http_status::no_content;
	}

	api_return SystemApi::handleShutdown(ApiRequest&) {
		systemActionThread = make_shared<SystemActionThread>(systemActionThread, true);
		return http_status::no_content;
	}

	void SystemApi::on(ActivityManagerListener::AwayModeChanged, AwayMode /*aNewMode*/) noexcept {
		maybeSend("away_state", [] {
			return serializeAwayState();
		});
	}

	string SystemApi::getAwayState(AwayMode aAwayMode) noexcept {
		switch (aAwayMode) {
			case AWAY_OFF: return "off";
			case AWAY_MANUAL: return "manual";
			case AWAY_IDLE: return "idle";
		}

		dcassert(0);
		return "";
	}

	json SystemApi::serializeAwayState() noexcept {
		return {
			{ "id", getAwayState(ActivityManager::getInstance()->getAwayMode()) },
		};
	}

	api_return SystemApi::handleGetAwayState(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeAwayState());
		return http_status::ok;
	}

	api_return SystemApi::handleSetAway(ApiRequest& aRequest) {
		auto away = JsonUtil::getField<bool>("away", aRequest.getRequestBody());
		ActivityManager::getInstance()->setAway(away ? AWAY_MANUAL : AWAY_OFF);

		aRequest.setResponseBody(serializeAwayState());
		return http_status::ok;
	}

	api_return SystemApi::handleGetStats(ApiRequest& aRequest) {
		auto server = session->getServer();

		aRequest.setResponseBody({
			{ "server_threads", WEBCFG(SERVER_THREADS).num() },
			{ "active_sessions", server->getUserManager().getUserSessionCount() },
		});
		return http_status::ok;
	}

	json SystemApi::getSystemInfo() noexcept {
		auto started = TimerManager::getStartTime();
		return {
			{ "api_version", API_VERSION },
			{ "api_feature_level", API_FEATURE_LEVEL },
			{ "path_separator", PATH_SEPARATOR_STR },
			{ "platform", SystemUtil::getPlatform() },
			{ "hostname", SystemUtil::getHostname() },
			{ "cid", ClientManager::getInstance()->getMyCID().toBase32() },
			{ "client_version", fullVersionString },
			{ "client_started", started },
			{ "language", Localization::getCurLanguageLocale() }
		};
	}

	api_return SystemApi::handleGetSystemInfo(ApiRequest& aRequest) {
		aRequest.setResponseBody(getSystemInfo());
		return http_status::ok;
	}
}