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
#include <web-server/WebServerManager.h>

#include <api/ConnectivityApi.h>

#include <api/common/Serializer.h>

namespace webserver {
	ConnectivityApi::ConnectivityApi(Session* aSession) : SubscribableApiModule(aSession, Access::SETTINGS_VIEW) {
		ConnectivityManager::getInstance()->addListener(this);

		createSubscription("connectivity_message");
		createSubscription("connectivity_started");
		createSubscription("connectivity_finished");

		METHOD_HANDLER("status", Access::SETTINGS_VIEW, ApiRequest::METHOD_GET, (), false, ConnectivityApi::handleGetStatus);
		METHOD_HANDLER("detect", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (), false, ConnectivityApi::handleDetect);
	}

	ConnectivityApi::~ConnectivityApi() {
		ConnectivityManager::getInstance()->removeListener(this);
	}

	json ConnectivityApi::formatStatus(bool v6) noexcept {
		auto modeKey = v6 ? SettingsManager::INCOMING_CONNECTIONS6 : SettingsManager::INCOMING_CONNECTIONS;
		auto modeValue = v6 ? SETTING(INCOMING_CONNECTIONS6) : SETTING(INCOMING_CONNECTIONS);
		auto protocolEnabled = modeValue != SettingsManager::INCOMING_DISABLED;

		string text;
		auto autoEnabled = v6 ? SETTING(AUTO_DETECT_CONNECTION6) : SETTING(AUTO_DETECT_CONNECTION);
		if (!autoEnabled) {
			auto enumStrings = SettingsManager::getEnumStrings(modeKey, true);
			if (!enumStrings.empty()) {
				text = ResourceManager::getInstance()->getString(enumStrings[modeValue]);
			} else {
				text = "Invalid configuration";
			}
		} else {
			text = ConnectivityManager::getInstance()->getStatus(v6);
		}

		return {
			{ "auto_detect", autoEnabled },
			{ "enabled", protocolEnabled },
			{ "text", text },
		};
	}

	api_return ConnectivityApi::handleGetStatus(ApiRequest& aRequest) {
		aRequest.setResponseBody({
			{ "status_v4", formatStatus(false) },
			{ "status_v6", formatStatus(true) }
		});

		return websocketpp::http::status_code::ok;
	}

	api_return ConnectivityApi::handleDetect(ApiRequest& aRequest) {
		ConnectivityManager::getInstance()->detectConnection();
		return websocketpp::http::status_code::ok;
	}

	void ConnectivityApi::on(ConnectivityManagerListener::Message, const string& aMessage) noexcept {
		if (!subscriptionActive("connectivity_message"))
			return;

		send("connectivity_message", aMessage);
	}

	void ConnectivityApi::on(ConnectivityManagerListener::Started, bool v6) noexcept {
		if (!subscriptionActive("connectivity_started"))
			return;

		send("connectivity_started", {  
			{ "v6", v6 }
		});
	}

	void ConnectivityApi::on(ConnectivityManagerListener::Finished, bool v6, bool aFailed) noexcept {
		if (!subscriptionActive("connectivity_finished"))
			return;

		send("connectivity_finished", {
			{ "v6", v6 },
			{ "failed", aFailed }
		});
	}
}