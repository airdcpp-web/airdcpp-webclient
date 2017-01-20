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
#include <web-server/version.h>

#include <web-server/JsonUtil.h>
#include <web-server/Timer.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>

#include <api/SystemApi.h>
#include <api/common/Serializer.h>

#include <airdcpp/ActivityManager.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/Thread.h>
#include <airdcpp/TimerManager.h>

namespace webserver {
	SystemApi::SystemApi(Session* aSession) : SubscribableApiModule(aSession, Access::ANY) {

		METHOD_HANDLER(Access::ANY, METHOD_GET,		(EXACT_PARAM("stats")),			SystemApi::handleGetStats);

		METHOD_HANDLER(Access::ANY, METHOD_GET,		(EXACT_PARAM("away")),			SystemApi::handleGetAwayState);
		METHOD_HANDLER(Access::ANY, METHOD_POST,	(EXACT_PARAM("away")),			SystemApi::handleSetAway);

		METHOD_HANDLER(Access::ADMIN, METHOD_POST,	(EXACT_PARAM("restart_web")),	SystemApi::handleRestartWeb);
		METHOD_HANDLER(Access::ADMIN, METHOD_POST,	(EXACT_PARAM("shutdown")),		SystemApi::handleShutdown);

		METHOD_HANDLER(Access::ANY, METHOD_GET,		(EXACT_PARAM("system_info")),	SystemApi::handleGetSystemInfo);

		createSubscription("away_state");

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

	api_return SystemApi::handleRestartWeb(ApiRequest& aRequest) {
		systemActionThread = make_shared<SystemActionThread>(systemActionThread, false);
		return websocketpp::http::status_code::no_content;
	}

	api_return SystemApi::handleShutdown(ApiRequest& aRequest) {
		systemActionThread = make_shared<SystemActionThread>(systemActionThread, true);
		return websocketpp::http::status_code::no_content;
	}

	void SystemApi::on(ActivityManagerListener::AwayModeChanged, AwayMode /*aNewMode*/) noexcept {
		send("away_state", serializeAwayState());
	}

	/*string SystemApi::getNetworkType(const string& aIp) noexcept {
		auto ip = aIp;

		// websocketpp will map IPv4 addresses to IPv6
		auto v6 = aIp.find(":") != string::npos;
		if (aIp.find("[::ffff:") == 0) {
			auto end = aIp.rfind("]");
			ip = aIp.substr(8, end - 8);
			v6 = false;
		}
		else if (aIp[0] == '[') {
			// Remove brackets
			auto end = aIp.rfind("]");
			ip = aIp.substr(1, end - 1);
		}

		if (Util::isPrivateIp(ip, v6)) {
			return "private";
		}
		else if (Util::isLocalIp(ip, v6)) {
			return "local";
		}

		return "internet";
	}*/

	string SystemApi::getHostname() noexcept {
#ifdef _WIN32
		TCHAR computerName[1024];
		DWORD size = 1024;
		GetComputerName(computerName, &size);
		return Text::fromT(computerName);
#else
		char hostname[128];
		gethostname(hostname, sizeof hostname);
		return hostname;
#endif
	}

	string SystemApi::getPlatform() noexcept {
#ifdef _WIN32
		return "windows";
#elif APPLE
		return "osx";
#else
		return "other";
#endif
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
		return websocketpp::http::status_code::ok;
	}

	api_return SystemApi::handleSetAway(ApiRequest& aRequest) {
		auto away = JsonUtil::getField<bool>("away", aRequest.getRequestBody());
		ActivityManager::getInstance()->setAway(away ? AWAY_MANUAL : AWAY_OFF);

		aRequest.setResponseBody(serializeAwayState());
		return websocketpp::http::status_code::ok;
	}

	api_return SystemApi::handleGetStats(ApiRequest& aRequest) {
		auto server = session->getServer();

		aRequest.setResponseBody({
			{ "server_threads", WEBCFG(SERVER_THREADS).num() },
			{ "active_sessions", server->getUserManager().getSessionCount() },
		});
		return websocketpp::http::status_code::ok;
	}

	json SystemApi::getSystemInfo() noexcept {
		auto started = TimerManager::getStartTime();
		return {
			{ "api_version", API_VERSION },
			{ "api_feature_level", API_FEATURE_LEVEL },
			{ "path_separator", PATH_SEPARATOR_STR },
			{ "platform", getPlatform() },
			{ "hostname", getHostname() },
			{ "cid", ClientManager::getInstance()->getMyCID().toBase32() },
			{ "client_version", fullVersionString },
			{ "client_started", started },
		};
	}

	api_return SystemApi::handleGetSystemInfo(ApiRequest& aRequest) {
		aRequest.setResponseBody(getSystemInfo());
		return websocketpp::http::status_code::ok;
	}
}