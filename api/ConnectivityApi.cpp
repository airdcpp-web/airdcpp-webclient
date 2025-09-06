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

#include <api/ConnectivityApi.h>

#include <api/common/Serializer.h>
#include <api/common/MessageUtils.h>

#include <airdcpp/connectivity/ConnectivityManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/search/SearchManager.h>

namespace webserver {
	ConnectivityApi::ConnectivityApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::SETTINGS_VIEW) 
	{
		createSubscriptions({ "connectivity_detection_message", "connectivity_detection_started", "connectivity_detection_finished" });

		METHOD_HANDLER(Access::SETTINGS_VIEW, METHOD_GET,	(EXACT_PARAM("status")), ConnectivityApi::handleGetStatus);
		METHOD_HANDLER(Access::SETTINGS_EDIT, METHOD_POST,	(EXACT_PARAM("detect")), ConnectivityApi::handleDetect);

		ConnectivityManager::getInstance()->addListener(this);
	}

	ConnectivityApi::~ConnectivityApi() {
		ConnectivityManager::getInstance()->removeListener(this);
	}

	json ConnectivityApi::formatStatus(bool v6) noexcept {
		auto modeKey = v6 ? SettingsManager::INCOMING_CONNECTIONS6 : SettingsManager::INCOMING_CONNECTIONS;
		auto modeValue = v6 ? SETTING(INCOMING_CONNECTIONS6) : SETTING(INCOMING_CONNECTIONS);
		auto protocolEnabled = modeValue != SettingsManager::INCOMING_DISABLED;

		string text;
		auto autoEnabled = protocolEnabled && (v6 ? SETTING(AUTO_DETECT_CONNECTION6) : SETTING(AUTO_DETECT_CONNECTION));
		if (!autoEnabled) {
			auto enumStrings = SettingsManager::getEnumStrings(modeKey, true);
			if (!enumStrings.empty()) {
				text = STRING_I(enumStrings[modeValue]);
			} else {
				text = "Invalid configuration";
			}
		} else {
			text = ConnectivityManager::getInstance()->getStatus(v6);
		}

		auto field = [](const string& s) { return s.empty() ? "undefined" : s; };
		return {
			{ "auto_detect", autoEnabled },
			{ "enabled", protocolEnabled },
			{ "text", text },
			{ "bind_address", field(v6 ? CONNSETTING(BIND_ADDRESS6) : CONNSETTING(BIND_ADDRESS)) },
			{ "external_ip", field(v6 ? CONNSETTING(EXTERNAL_IP6) : CONNSETTING(EXTERNAL_IP)) },
		};
	}

	api_return ConnectivityApi::handleGetStatus(ApiRequest& aRequest) {
		aRequest.setResponseBody({
			{ "status_v4", formatStatus(false) },
			{ "status_v6", formatStatus(true) },
			{ "tcp_port", ConnectionManager::getInstance()->getPort() },
			{ "tls_port", ConnectionManager::getInstance()->getSecurePort() },
			{ "udp_port", SearchManager::getInstance()->getPort() },
		});

		return http_status::ok;
	}

	api_return ConnectivityApi::handleDetect(ApiRequest&) {
		ConnectivityManager::getInstance()->detectConnection();
		return http_status::no_content;
	}

	void ConnectivityApi::on(ConnectivityManagerListener::Message, const LogMessagePtr& aMessage) noexcept {
		if (!subscriptionActive("connectivity_detection_message"))
			return;

		send("connectivity_detection_message", MessageUtils::serializeLogMessage(aMessage));
	}

	void ConnectivityApi::on(ConnectivityManagerListener::Started, bool v6) noexcept {
		if (!subscriptionActive("connectivity_detection_started"))
			return;

		send("connectivity_detection_started", {  
			{ "v6", v6 }
		});
	}

	void ConnectivityApi::on(ConnectivityManagerListener::Finished, bool v6, bool aFailed) noexcept {
		if (!subscriptionActive("connectivity_detection_finished"))
			return;

		send("connectivity_detection_finished", {
			{ "v6", v6 },
			{ "failed", aFailed }
		});
	}
}