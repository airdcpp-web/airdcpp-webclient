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

#include <web-server/stdinc.h>
#include <api/ApiSettingItem.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/ConnectionManager.h>
#include <airdcpp/ConnectivityManager.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/SettingHolder.h>
#include <airdcpp/SettingItem.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/StringTokenizer.h>

namespace webserver {
	ApiSettingItem::ApiSettingItem(const string& aName, Type aType, Unit&& aUnit) : 
		name(aName), type(aType), unit(move(aUnit)) {

	}

	json ApiSettingItem::infoToJson(bool aForceAutoValues) const noexcept {
		auto value = valueToJson(aForceAutoValues);

		// Serialize the setting
		json ret;
		ret["value"] = value.first;
		ret["key"] = name;
		ret["title"] = getTitle();
		if (value.second) {
			ret["auto"] = true;
		}

		if (unit.str != ResourceManager::LAST) {
			ret["unit"] = ResourceManager::getInstance()->getString(unit.str) + (unit.isSpeed ? "/s" : "");
		}

		if (type == TYPE_FILE_PATH) {
			ret["type"] = "file_path";
		} else if (type == TYPE_DIRECTORY_PATH) {
			ret["type"] = "directory_path";
		} else if (type == TYPE_LONG_TEXT) {
			ret["type"] = "long_text";
		} else if (value.first.is_boolean()) {
			ret["type"] = "boolean";
		} else if (value.first.is_number()) {
			ret["type"] = "number";
		} else if (value.first.is_string()) {
			ret["type"] = "string";
		} else {
			dcassert(0);
		}

		return ret;
	}

	ServerSettingItem::ServerSettingItem(const string& aKey, const string& aTitle, const json& aDefaultValue, Type aType, Unit&& aUnit) :
		ApiSettingItem(aKey, aType, move(aUnit)), desc(aTitle), defaultValue(aDefaultValue), value(aDefaultValue) {

	}

	json ServerSettingItem::infoToJson(bool aForceAutoValues) const noexcept {
		return ApiSettingItem::infoToJson(aForceAutoValues);
	}

	// Returns the value and bool indicating whether it's an auto detected value
	pair<json, bool> ServerSettingItem::valueToJson(bool /*aForceAutoValues*/) const noexcept {
		return { value, false };
	}

	void ServerSettingItem::unset() noexcept {
		value = defaultValue;
	}

	bool ServerSettingItem::setCurValue(const json& aJson) {
		if (aJson.is_null()) {
			unset();
		} else {
			JsonUtil::ensureType(name, aJson, defaultValue);
			value = aJson;
		}

		return true;
	}

	int ServerSettingItem::num() {
		return value.get<int>();
	}

	uint64_t ServerSettingItem::uint64() {
		return value.get<uint64_t>();
	}

	string ServerSettingItem::str() {
		if (value.is_number()) {
			return Util::toString(num());
		}

		return value.get<string>();
	}

	bool ServerSettingItem::isDefault() const noexcept {
		return value == defaultValue;
	}

	CoreSettingItem::CoreSettingItem(const string& aName, int aKey, ResourceManager::Strings aDesc, Type aType, Unit&& aUnit) :
		ApiSettingItem(aName, aType, move(aUnit)), SettingItem({ aKey, aDesc }) {

	}


	#define USE_AUTO(aType, aSetting) (type == aType && (aForceAutoValues || SETTING(aSetting)))
	json CoreSettingItem::autoValueToJson(bool aForceAutoValues) const noexcept {
		json v;
		if (USE_AUTO(TYPE_CONN_V4, AUTO_DETECT_CONNECTION) || USE_AUTO(TYPE_CONN_V6, AUTO_DETECT_CONNECTION6) ||
			(type == TYPE_CONN_GEN && (SETTING(AUTO_DETECT_CONNECTION) || SETTING(AUTO_DETECT_CONNECTION6)))) {

			if (key == SettingsManager::TCP_PORT) {
				v = ConnectionManager::getInstance()->getPort();
			} else if (key == SettingsManager::UDP_PORT) {
				v = SearchManager::getInstance()->getPort();
			} else if (key == SettingsManager::TLS_PORT) {
				v = ConnectionManager::getInstance()->getSecurePort();
			} else {
				if (key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
					v = ConnectivityManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(key));
				} else if (key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
					v = ConnectivityManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(key));
				} else if (key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
					v = ConnectivityManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(key));
				} else {
					dcassert(0);
				}
			}
		} else if (USE_AUTO(TYPE_LIMITS_DL, DL_AUTODETECT)) {
			if (key == SettingsManager::DOWNLOAD_SLOTS) {
				v = AirUtil::getSlots(true);
			} else if (key == SettingsManager::MAX_DOWNLOAD_SPEED) {
				v = AirUtil::getSpeedLimit(true);
			}
		} else if (USE_AUTO(TYPE_LIMITS_UL, UL_AUTODETECT)) {
			if (key == SettingsManager::SLOTS) {
				v = AirUtil::getSlots(false);
			} else if (key == SettingsManager::MIN_UPLOAD_SPEED) {
				v = AirUtil::getSpeedLimit(false);
			} else if (key == SettingsManager::AUTO_SLOTS) {
				v = AirUtil::getMaxAutoOpened();
			}
		} else if (USE_AUTO(TYPE_LIMITS_MCN, MCN_AUTODETECT)) {
			v = AirUtil::getSlotsPerUser(key == SettingsManager::MAX_MCN_DOWNLOADS);
		}

		return v;
	}

	pair<json, bool> CoreSettingItem::valueToJson(bool aForceAutoValues) const noexcept {
		auto v = autoValueToJson(aForceAutoValues);
		if (!v.is_null()) {
			return { v, true };
		}

		if (key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
			v = SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(key), true);
		} else if (key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
			v = SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(key), true);
		} else if (key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
			v = SettingsManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(key), true);
		} else {
			dcassert(0);
		}

		return { v, false };
	}

	json CoreSettingItem::infoToJson(bool aForceAutoValues) const noexcept {
		// Get the current value
		auto value = valueToJson(aForceAutoValues);

		// Serialize the setting
		json ret = ApiSettingItem::infoToJson(aForceAutoValues);

		// Serialize possible enum values
		auto enumStrings = SettingsManager::getEnumStrings(key, false);
		if (!enumStrings.empty()) {
			for (const auto& i : enumStrings) {
				ret["values"].push_back({
					{ "text", ResourceManager::getInstance()->getString(i.second) },
					{ "value", i.first }
				});
			}
		} else if (key == SettingsManager::BIND_ADDRESS || key == SettingsManager::BIND_ADDRESS6) {
			auto bindAddresses = AirUtil::getBindAdapters(key == SettingsManager::BIND_ADDRESS6);
			for (const auto& adapter : bindAddresses) {
				ret["values"].push_back({
					{ "text", adapter.ip + (!adapter.adapterName.empty() ? " (" + adapter.adapterName + ")" : Util::emptyString) },
					{ "value", adapter.ip }
				});
			}
		} else if (key == SettingsManager::MAPPER) {
			auto mappers = ConnectivityManager::getInstance()->getMappers(false);
			for (const auto& mapper : mappers) {
				ret["values"].push_back({
					{ "text", mapper },
					{ "value", mapper }
				});
			}
		}

		return ret;
	}

	void CoreSettingItem::unset() noexcept {
		SettingItem::unset();
	}

	bool CoreSettingItem::setCurValue(const json& aJson) {
		if ((type == TYPE_CONN_V4 && SETTING(AUTO_DETECT_CONNECTION)) ||
			(type == TYPE_CONN_V6 && SETTING(AUTO_DETECT_CONNECTION6))) {
			//display::Manager::get()->cmdMessage("Note: Connection autodetection is enabled for the edited protocol. The changed setting won't take effect before auto detection has been disabled.");
		}

		if ((type == TYPE_LIMITS_DL && SETTING(DL_AUTODETECT)) ||
			(type == TYPE_LIMITS_UL && SETTING(UL_AUTODETECT)) ||
			(type == TYPE_LIMITS_MCN && SETTING(MCN_AUTODETECT))) {

			//display::Manager::get()->cmdMessage("Note: auto detection is enabled for the edited settings group. The changed setting won't take effect before auto detection has been disabled.");
		}

		if (key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
			auto value = JsonUtil::parseValue<string>(name, aJson);
			if (type == TYPE_DIRECTORY_PATH) {
				value = Util::validatePath(value, true);
			}

			SettingsManager::getInstance()->set(static_cast<SettingsManager::StrSetting>(key), value);
		} else if (key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
			SettingsManager::getInstance()->set(static_cast<SettingsManager::IntSetting>(key), JsonUtil::parseValue<int>(name, aJson));
		} else if (key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
			SettingsManager::getInstance()->set(static_cast<SettingsManager::BoolSetting>(key), JsonUtil::parseValue<bool>(name, aJson));
		} else {
			dcassert(0);
			return false;
		}

		return true;
	}
}
