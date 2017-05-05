/*
* Copyright (C) 2011-2017 AirDC++ Project
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
#include <api/common/SettingUtils.h>

#include <airdcpp/SettingHolder.h>

namespace webserver {
	SettingApi::SettingApi(Session* aSession) : ApiModule(aSession) {
		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_POST, (EXACT_PARAM("definitions")),	SettingApi::handleGetDefinitions);
		METHOD_HANDLER(Access::ANY,				METHOD_POST, (EXACT_PARAM("get")),			SettingApi::handleGetValues);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST, (EXACT_PARAM("set")),			SettingApi::handleSetValues);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST, (EXACT_PARAM("reset")),		SettingApi::handleResetValues);
	}

	SettingApi::~SettingApi() {
	}

	api_return SettingApi::handleGetDefinitions(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();

		json retJson = json::array();
		parseSettingKeys(requestJson, [&](ApiSettingItem& aItem) {
			retJson.push_back(SettingUtils::serializeDefinition(aItem));
		});

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
	}

	api_return SettingApi::handleGetValues(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();

		auto forceAutoValues = JsonUtil::getOptionalFieldDefault<bool>("force_auto_values", requestJson, false);

		auto retJson = json::object();
		parseSettingKeys(requestJson, [&](ApiSettingItem& aItem) {
			if (aItem.usingAutoValue(forceAutoValues)) {
				retJson[aItem.name] = aItem.getAutoValue();
			} else {
				retJson[aItem.name] = aItem.getValue();
			}
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

			aHandler(*setting);
		}
	}

	api_return SettingApi::handleResetValues(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();

		parseSettingKeys(requestJson, [&](ApiSettingItem& aItem) {
			aItem.unset();
		});

		return websocketpp::http::status_code::no_content;
	}

	api_return SettingApi::handleSetValues(ApiRequest& aRequest) {
		SettingHolder h(nullptr);

		bool hasSet = false;
		for (const auto& elem : json::iterator_wrapper(aRequest.getRequestBody())) {
			auto setting = getSettingItem(elem.key());
			if (!setting) {
				JsonUtil::throwError(elem.key(), JsonUtil::ERROR_INVALID, "Setting not found");
			}

			setting->setValue(SettingUtils::validateValue(elem.value(), *setting));
			hasSet = true;
		}

		dcassert(hasSet);

		SettingsManager::getInstance()->save();
		WebServerManager::getInstance()->save(nullptr);

		return websocketpp::http::status_code::no_content;
	}

	ApiSettingItem* SettingApi::getSettingItem(const string& aKey) noexcept {
		auto p = ApiSettingItem::findSettingItem<CoreSettingItem>(coreSettings, aKey);
		if (p) {
			return p;
		}

		return WebServerSettings::getSettingItem(aKey);
	}
}
