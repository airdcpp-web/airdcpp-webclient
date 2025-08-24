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

#include <api/SettingApi.h>

#include <api/CoreSettings.h>
#include <api/common/Serializer.h>
#include <api/common/SettingUtils.h>

#include <web-server/ApiSettingItem.h>
#include <web-server/JsonUtil.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>

#include <airdcpp/settings/SettingHolder.h>

namespace webserver {
	SettingApi::SettingApi(Session* aSession) : ApiModule(aSession) {
		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_POST, (EXACT_PARAM("definitions")),	SettingApi::handleGetDefinitions);
		METHOD_HANDLER(Access::ANY,				METHOD_POST, (EXACT_PARAM("get")),			SettingApi::handleGetValues);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST, (EXACT_PARAM("set")),			SettingApi::handleSetValues);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST, (EXACT_PARAM("reset")),		SettingApi::handleResetValues);
		METHOD_HANDLER(Access::ANY,				METHOD_POST, (EXACT_PARAM("get_defaults")), SettingApi::handleGetDefaultValues);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST, (EXACT_PARAM("set_defaults")), SettingApi::handleSetDefaultValues);
	}

	SettingApi::~SettingApi() {
	}

	api_return SettingApi::handleGetDefinitions(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();

		json retJson = json::array();
		parseSettingKeys(requestJson, [&](const ApiSettingItem& aItem) {
			retJson.push_back(SettingUtils::serializeDefinition(aItem));
		}, aRequest.getSession()->getServer());

		aRequest.setResponseBody(retJson);
		return http_status::ok;
	}

	api_return SettingApi::handleGetDefaultValues(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();

		auto retJson = json::object();
		parseSettingKeys(requestJson, [&](const ApiSettingItem& aItem) {
			retJson[aItem.name] = aItem.getDefaultValue();
		}, aRequest.getSession()->getServer());

		aRequest.setResponseBody(retJson);
		return http_status::ok;
	}

	api_return SettingApi::handleSetDefaultValues(ApiRequest& aRequest) {
		auto hasSet = false;
		parseSettingValues(aRequest.getRequestBody(), [&](ApiSettingItem& aItem, const json& aValue) {
			decltype(auto) settings = aRequest.getSession()->getServer()->getSettingsManager();
			settings.setDefaultValue(aItem, aValue);

			hasSet = true;
		}, aRequest.getSession()->getServer());

		dcassert(hasSet);
		return http_status::no_content;
	}

	api_return SettingApi::handleGetValues(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();

		auto valueMode = JsonUtil::getEnumFieldDefault<string>("value_mode", requestJson, "current", { "current", "force_auto", "force_manual" });
		if (JsonUtil::getOptionalFieldDefault<bool>("force_auto_values", requestJson, false)) { // Deprecated
			valueMode = "force_auto";
		}

		auto retJson = json::object();
		parseSettingKeys(requestJson, [&](const ApiSettingItem& aItem) {
			if (valueMode != "force_manual" && aItem.usingAutoValue(valueMode == "force_auto")) {
				retJson[aItem.name] = aItem.getAutoValue();
			} else {
				retJson[aItem.name] = aItem.getValue();
			}
		}, aRequest.getSession()->getServer());

		aRequest.setResponseBody(retJson);
		return http_status::ok;
	}

	void SettingApi::parseSettingKeys(const json& aJson, const KeyParserF& aHandler, WebServerManager* aWsm) {
		auto keys = JsonUtil::getField<StringList>("keys", aJson, true);
		for (const auto& key : keys) {
			auto setting = getSettingItem(key, aWsm);
			if (!setting) {
				JsonUtil::throwError(key, JsonException::ERROR_INVALID, "Setting not found");
			}

			aHandler(*setting);
		}
	}

	void SettingApi::parseSettingValues(const json& aJson, const ValueParserF& aHandler, WebServerManager* aWsm) {
		for (const auto& elem: aJson.items()) {
			auto setting = getSettingItem(elem.key(), aWsm);
			if (!setting) {
				JsonUtil::throwError(elem.key(), JsonException::ERROR_INVALID, "Setting not found");
			}

			auto value = SettingUtils::validateValue(elem.value(), *setting, nullptr);
			aHandler(*setting, value);
		}
	}

	api_return SettingApi::handleResetValues(ApiRequest& aRequest) {
		const auto& requestJson = aRequest.getRequestBody();
		decltype(auto) settings = aRequest.getSession()->getServer()->getSettingsManager();

		parseSettingKeys(requestJson, [&](ApiSettingItem& aItem) {
			settings.unset(aItem);
		}, aRequest.getSession()->getServer());

		return http_status::no_content;
	}

	api_return SettingApi::handleSetValues(ApiRequest& aRequest) {
		auto server = aRequest.getSession()->getServer();
		auto holder = make_shared<SettingHolder>(
			[server](const string& aError) {
				server->log(aError, LogMessage::SEV_ERROR);
			}
		);

		bool hasSet = false;

		parseSettingValues(aRequest.getRequestBody(), [&](ApiSettingItem& aItem, const json& aValue) {
			server->getSettingsManager().setValue(aItem, aValue);
			hasSet = true;
		}, aRequest.getSession()->getServer());

		dcassert(hasSet);

		SettingsManager::getInstance()->save();

		// This may take a while, don't wait
		addAsyncTask([holder] {
			holder->apply();
		});

		return http_status::no_content;
	}

	ApiSettingItem* SettingApi::getSettingItem(const string& aKey, WebServerManager* aWsm) noexcept {
		auto p = ApiSettingItem::findSettingItem<CoreSettingItem>(coreSettings, aKey);
		if (p) {
			return p;
		}

		return aWsm->getSettingsManager().getSettingItem(aKey);
	}
}
