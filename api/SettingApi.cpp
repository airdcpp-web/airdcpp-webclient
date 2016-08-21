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

#include <api/SettingApi.h>
#include <api/ApiSettingItem.h>

#include <api/CoreSettings.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>

#include <web-server/JsonUtil.h>
#include <api/common/Serializer.h>

#include <airdcpp/SettingHolder.h>

namespace webserver {
	SettingApi::SettingApi(Session* aSession) : ApiModule(aSession) {
		METHOD_HANDLER("items", Access::SETTINGS_VIEW, ApiRequest::METHOD_POST, (EXACT_PARAM("info")), true, SettingApi::handleGetSettingInfos);
		METHOD_HANDLER("items", Access::ANY, ApiRequest::METHOD_POST, (EXACT_PARAM("get")), true, SettingApi::handleGetSettingValues);
		METHOD_HANDLER("items", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("set")), true, SettingApi::handleSetSettings);
		METHOD_HANDLER("items", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("reset")), true, SettingApi::handleResetSettings);
	}

	SettingApi::~SettingApi() {
	}

	api_return SettingApi::handleGetSettingInfos(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();

		auto forceAutoValues = JsonUtil::getOptionalFieldDefault<bool>("force_auto_values", requestJson, false);

		json retJson;
		parseSettingKeys(requestJson, [&](ApiSettingItem* aItem) {
			retJson[aItem->name] = aItem->infoToJson(forceAutoValues);
		});

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
	}

	api_return SettingApi::handleGetSettingValues(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();

		json retJson;
		parseSettingKeys(requestJson, [&](ApiSettingItem* aItem) {
			retJson[aItem->name] = aItem->valueToJson().first;
		});

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
	}

	void SettingApi::parseSettingKeys(const json& aJson, ParserF aHandler) {
		auto keys = JsonUtil::getField<StringList>("keys", aJson, true);
		for (const auto& key : keys) {
			auto setting = getSettingItem(key);
			if (!setting) {
				JsonUtil::throwError(key, JsonUtil::ERROR_INVALID, "Setting not found");
			}

			aHandler(setting);
		}
	}

	api_return SettingApi::handleResetSettings(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();

		parseSettingKeys(requestJson, [&](ApiSettingItem* aItem) {
			aItem->unset();
		});

		return websocketpp::http::status_code::no_content;
	}

	api_return SettingApi::handleSetSettings(ApiRequest& aRequest) {
		SettingHolder h(nullptr);

		for (const auto& elem : json::iterator_wrapper(aRequest.getRequestBody())) {
			auto setting = getSettingItem(elem.key());
			if (!setting) {
				JsonUtil::throwError(elem.key(), JsonUtil::ERROR_INVALID, "Setting not found");
			}

			setting->setCurValue(elem.value());
		}

		SettingsManager::getInstance()->save();
		WebServerManager::getInstance()->save(nullptr);

		return websocketpp::http::status_code::ok;
	}

	ApiSettingItem* SettingApi::getSettingItem(const string& aKey) noexcept {
		auto p = boost::find_if(coreSettings, [&](ApiSettingItem& aItem) { return aItem.name == aKey; });
		if (p != coreSettings.end()) {
			return &(*p);
		}

		return WebServerSettings::getSettingItem(aKey);
	}
}
