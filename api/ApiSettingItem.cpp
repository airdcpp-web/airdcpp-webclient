/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#include "stdinc.h"

#include <api/ApiSettingItem.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/ConnectionManager.h>
#include <airdcpp/ConnectivityManager.h>
#include <airdcpp/Localization.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/SettingHolder.h>
#include <airdcpp/SettingItem.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/StringTokenizer.h>

namespace webserver {
	string ApiSettingItem::formatTitle(ResourceManager::Strings aDesc, ResourceManager::Strings aUnit) noexcept {
		auto title = ResourceManager::getInstance()->getString(aDesc);

		if (aUnit != ResourceManager::LAST) {
			title += " (" + ResourceManager::getInstance()->getString(aUnit) + ")";
		}

		return title;
	}

	const ApiSettingItem::MinMax ApiSettingItem::defaultMinMax = { 0, MAX_INT_VALUE };

	ApiSettingItem::ApiSettingItem(const string& aName, Type aType, Type aItemType) :
		name(aName), type(aType), itemType(aItemType) {

	}

	bool ApiSettingItem::usingAutoValue(bool aForce) const noexcept {
		return false;
	}

	json ApiSettingItem::getAutoValue() const noexcept {
		// Setting types with auto values should override this method
		return getValue();
	}

	JsonSettingItem::JsonSettingItem(const string& aKey, const json& aDefaultValue, Type aType,
		bool aOptional, const MinMax& aMinMax, const string& aHelp, Type aItemType, const EnumOption::List& aEnumOptions) :

		ApiSettingItem(aKey, aType, aItemType), defaultValue(aDefaultValue),
		optional(aOptional), minMax(aMinMax), help(aHelp), enumOptions(aEnumOptions)
	{
		dcassert(aType != TYPE_NUMBER || minMax.min != minMax.max);
	}


	ServerSettingItem::ServerSettingItem(const string& aKey, const ResourceManager::Strings aTitleKey, const json& aDefaultValue, Type aType, bool aOptional,
		const MinMax& aMinMax, const ResourceManager::Strings aUnit): JsonSettingItem(aKey, aDefaultValue, aType, aOptional, aMinMax), titleKey(aTitleKey) {

	}

	ApiSettingItem::PtrList ServerSettingItem::getValueTypes() const noexcept {
		return ApiSettingItem::PtrList();
	}

	string ServerSettingItem::getTitle() const noexcept {
		return ResourceManager::getInstance()->getString(titleKey);
	}

	ExtensionSettingItem::ExtensionSettingItem(const string& aKey, const string& aTitle, const json& aDefaultValue, Type aType,
		bool aOptional, const MinMax& aMinMax, const List& aObjectValues, const string& aHelp, Type aItemType, const EnumOption::List& aEnumOptions) : 

		JsonSettingItem(aKey, aDefaultValue, aType, aOptional, aMinMax, aHelp, aItemType, aEnumOptions), title(aTitle), objectValues(aObjectValues) {

	}

	ApiSettingItem::PtrList ExtensionSettingItem::getValueTypes() const noexcept {
		return valueTypesToPtrList(objectValues);
	}

	// Returns the value and bool indicating whether it's an auto detected value
	json JsonSettingItem::getValue() const noexcept {
		return getValueRef();
	}

	const json& JsonSettingItem::getValueRef() const noexcept {
		return isDefault() ? defaultValue : value;
	}

	const string& JsonSettingItem::getHelpStr() const noexcept {
		return help;
	}

	void JsonSettingItem::unset() noexcept {
		value = nullptr;
	}

	bool JsonSettingItem::setValue(const json& aJson) {
		if (aJson.is_null()) {
			unset();
		} else {
			// The value should have been validated before
			value = aJson;
		}

		return true;
	}

	int JsonSettingItem::num() const {
		return getValueRef().get<int>();
	}

	ApiSettingItem::ListNumber JsonSettingItem::numList() const {
		return getValueRef().get<vector<int>>();
	}

	ApiSettingItem::ListString JsonSettingItem::strList() const {
		return getValueRef().get<vector<string>>();
	}

	uint64_t JsonSettingItem::uint64() const {
		return getValueRef().get<uint64_t>();
	}

	string JsonSettingItem::str() const {
		if (getValueRef().is_number()) {
			return Util::toString(num());
		}

		return getValueRef().get<string>();
	}

	bool JsonSettingItem::boolean() const {
		return getValueRef().get<bool>();
	}

	bool JsonSettingItem::isDefault() const noexcept {
		return value.is_null();
	}

	json JsonSettingItem::getDefaultValue() const noexcept {
		return defaultValue;
	}

	ApiSettingItem::EnumOption::List JsonSettingItem::getEnumOptions() const noexcept {
		return enumOptions;
	}


	const ApiSettingItem::MinMax& JsonSettingItem::getMinMax() const noexcept {
		return minMax;
	}

	map<int, CoreSettingItem::MinMax> minMaxMappings = {
		{ SettingsManager::TCP_PORT, { 1, 65535 } },
		{ SettingsManager::UDP_PORT, { 1, 65535 } },
		{ SettingsManager::TLS_PORT, { 1, 65535 } },

		{ SettingsManager::MAX_HASHING_THREADS, { 1, 100 } },
		{ SettingsManager::HASHERS_PER_VOLUME, { 1, 100 } },

		{ SettingsManager::MAX_COMPRESSION, { 0, 9 } },
		{ SettingsManager::MINIMUM_SEARCH_INTERVAL, { 5, 1000 } },

		{ SettingsManager::UPLOAD_SLOTS, { 1, 250 } },
		{ SettingsManager::DOWNLOAD_SLOTS, { 0, 250 } },
		{ SettingsManager::SET_MINISLOT_SIZE, { 64, MAX_INT_VALUE } },
		{ SettingsManager::EXTRA_SLOTS, { 1, 100 } },

		{ SettingsManager::NUMBER_OF_SEGMENTS, { 1, 10 } },
		{ SettingsManager::BUNDLE_SEARCH_TIME, { 5, MAX_INT_VALUE } },

		// No validation for other enums at the moment but negative value would cause issues otherwise...
		{ SettingsManager::INCOMING_CONNECTIONS, { SettingsManager::INCOMING_DISABLED, SettingsManager::INCOMING_LAST } },
		{ SettingsManager::INCOMING_CONNECTIONS6, { SettingsManager::INCOMING_DISABLED, SettingsManager::INCOMING_LAST } },
	};

	set<int> optionalSettingKeys = {
		SettingsManager::DESCRIPTION,
		SettingsManager::EMAIL,

		SettingsManager::EXTERNAL_IP,
		SettingsManager::EXTERNAL_IP6,

		SettingsManager::DEFAULT_AWAY_MESSAGE,
		SettingsManager::SKIPLIST_DOWNLOAD,
		SettingsManager::SKIPLIST_SHARE,
		SettingsManager::FREE_SLOTS_EXTENSIONS,

		SettingsManager::HTTP_PROXY,
		SettingsManager::SOCKS_SERVER,
		SettingsManager::SOCKS_USER,
		SettingsManager::SOCKS_PASSWORD,

		SettingsManager::LANGUAGE_FILE,
	};

	map<int, CoreSettingItem::Group> groupMappings = {
		{ SettingsManager::TCP_PORT, CoreSettingItem::GROUP_CONN_GEN },
		{ SettingsManager::UDP_PORT, CoreSettingItem::GROUP_CONN_GEN },
		{ SettingsManager::TLS_PORT, CoreSettingItem::GROUP_CONN_GEN },
		{ SettingsManager::MAPPER, CoreSettingItem::GROUP_CONN_GEN },

		{ SettingsManager::BIND_ADDRESS, CoreSettingItem::GROUP_CONN_V4 },
		{ SettingsManager::INCOMING_CONNECTIONS, CoreSettingItem::GROUP_CONN_V4 },
		{ SettingsManager::EXTERNAL_IP, CoreSettingItem::GROUP_CONN_V4 },
		{ SettingsManager::IP_UPDATE, CoreSettingItem::GROUP_CONN_V4 },
		{ SettingsManager::NO_IP_OVERRIDE, CoreSettingItem::GROUP_CONN_V4 },

		{ SettingsManager::BIND_ADDRESS6, CoreSettingItem::GROUP_CONN_V6 },
		{ SettingsManager::INCOMING_CONNECTIONS6, CoreSettingItem::GROUP_CONN_V6 },
		{ SettingsManager::EXTERNAL_IP6, CoreSettingItem::GROUP_CONN_V6 },
		{ SettingsManager::IP_UPDATE6, CoreSettingItem::GROUP_CONN_V6 },
		{ SettingsManager::NO_IP_OVERRIDE6, CoreSettingItem::GROUP_CONN_V6 },

		{ SettingsManager::DOWNLOAD_SLOTS, CoreSettingItem::GROUP_LIMITS_DL },
		{ SettingsManager::MAX_DOWNLOAD_SPEED, CoreSettingItem::GROUP_LIMITS_DL },

		{ SettingsManager::MIN_UPLOAD_SPEED, CoreSettingItem::GROUP_LIMITS_UL },
		{ SettingsManager::AUTO_SLOTS, CoreSettingItem::GROUP_LIMITS_UL },
		{ SettingsManager::UPLOAD_SLOTS, CoreSettingItem::GROUP_LIMITS_UL },

		{ SettingsManager::MAX_MCN_DOWNLOADS, CoreSettingItem::GROUP_LIMITS_MCN },
		{ SettingsManager::MAX_MCN_UPLOADS, CoreSettingItem::GROUP_LIMITS_MCN },
	};

	CoreSettingItem::CoreSettingItem(const string& aName, int aKey, ResourceManager::Strings aDesc, Type aType, ResourceManager::Strings aUnit) :
		ApiSettingItem(aName, parseAutoType(aType, aKey), ApiSettingItem::TYPE_LAST), si({ aKey, aDesc }), unit(aUnit) {

	}

	ApiSettingItem::Type CoreSettingItem::parseAutoType(Type aType, int aKey) noexcept {
		if (aKey >= SettingsManager::STR_FIRST && aKey < SettingsManager::STR_LAST) {
			if (aType == TYPE_LAST) return TYPE_STRING;
			dcassert(isString(aType));
		} else if (aKey >= SettingsManager::INT_FIRST && aKey < SettingsManager::INT_LAST) {
			if (aType == TYPE_LAST) return TYPE_NUMBER;
			dcassert(aType == TYPE_NUMBER);
		} else if (aKey >= SettingsManager::BOOL_FIRST && aKey < SettingsManager::BOOL_LAST) {
			if (aType == TYPE_LAST) return TYPE_BOOLEAN;
			dcassert(aType == TYPE_BOOLEAN);
		} else {
			dcassert(0);
		}

		return aType;
	}

#define USE_AUTO(aType, aGroupSetting) ((groupMappings.find(si.key) != groupMappings.end() && groupMappings.at(si.key) == aType) && (aForceAutoValues || SETTING(aGroupSetting)))
	bool CoreSettingItem::usingAutoValue(bool aForceAutoValues) const noexcept {
		if (USE_AUTO(GROUP_CONN_V4, AUTO_DETECT_CONNECTION) || USE_AUTO(GROUP_CONN_V6, AUTO_DETECT_CONNECTION6) ||
			(USE_AUTO(GROUP_CONN_GEN, AUTO_DETECT_CONNECTION) || USE_AUTO(GROUP_CONN_GEN, AUTO_DETECT_CONNECTION6))) {

			return true;
		} else if (USE_AUTO(GROUP_LIMITS_DL, DL_AUTODETECT)) {
			return true;
		} else if (USE_AUTO(GROUP_LIMITS_UL, UL_AUTODETECT)) {
			return true;
		} else if (USE_AUTO(GROUP_LIMITS_MCN, MCN_AUTODETECT)) {
			return true;
		}

		return false;
	}

	json CoreSettingItem::getAutoValue() const noexcept {
		switch (si.key) {
			case SettingsManager::TCP_PORT: return ConnectionManager::getInstance()->getPort();
			case SettingsManager::UDP_PORT: return SearchManager::getInstance()->getPort();
			case SettingsManager::TLS_PORT: return ConnectionManager::getInstance()->getSecurePort();
			case SettingsManager::MAPPER: 

			case SettingsManager::BIND_ADDRESS: 
			case SettingsManager::EXTERNAL_IP: 

			case SettingsManager::BIND_ADDRESS6:
			case SettingsManager::EXTERNAL_IP6: return ConnectivityManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(si.key));

			case SettingsManager::INCOMING_CONNECTIONS: 
			case SettingsManager::INCOMING_CONNECTIONS6: return ConnectivityManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(si.key));

			case SettingsManager::IP_UPDATE: 
			case SettingsManager::NO_IP_OVERRIDE: 

			case SettingsManager::IP_UPDATE6: 
			case SettingsManager::NO_IP_OVERRIDE6: return ConnectivityManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(si.key));

			case SettingsManager::DOWNLOAD_SLOTS: return AirUtil::getSlots(true, Util::toDouble(SETTING(DOWNLOAD_SPEED)));
			case SettingsManager::MAX_DOWNLOAD_SPEED: return AirUtil::getSpeedLimit(true, Util::toDouble(SETTING(DOWNLOAD_SPEED)));

			case SettingsManager::UPLOAD_SLOTS: return AirUtil::getSlots(false, Util::toDouble(SETTING(UPLOAD_SPEED)));
			case SettingsManager::MIN_UPLOAD_SPEED: return AirUtil::getSpeedLimit(false, Util::toDouble(SETTING(UPLOAD_SPEED)));
			case SettingsManager::AUTO_SLOTS: return AirUtil::getMaxAutoOpened(Util::toDouble(SETTING(UPLOAD_SPEED)));

			case SettingsManager::MAX_MCN_DOWNLOADS: return AirUtil::getSlotsPerUser(true, Util::toDouble(SETTING(DOWNLOAD_SPEED)));
			case SettingsManager::MAX_MCN_UPLOADS: return AirUtil::getSlotsPerUser(false, Util::toDouble(SETTING(UPLOAD_SPEED)));
		}

		return ApiSettingItem::getAutoValue();
	}

	const ApiSettingItem::MinMax& CoreSettingItem::getMinMax() const noexcept {
		auto i = minMaxMappings.find(si.key);
		return i != minMaxMappings.end() ? i->second : defaultMinMax;
	}

	bool CoreSettingItem::isOptional() const noexcept {
		return optionalSettingKeys.find(si.key) != optionalSettingKeys.end();
	}

	ApiSettingItem::PtrList CoreSettingItem::getValueTypes() const noexcept {
		return ApiSettingItem::PtrList();
	}

	const string& CoreSettingItem::getHelpStr() const noexcept {
		return Util::emptyString;
	}

	json CoreSettingItem::getValue() const noexcept {
		if (isString(type)) {
			return SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(si.key), true);
		} else if (type == TYPE_NUMBER) {
			return SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(si.key), true);
		} else if (type == TYPE_BOOLEAN) {
			return SettingsManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(si.key), true);
		}
			
		dcassert(0);
		return nullptr;
	}

	json CoreSettingItem::getDefaultValue() const noexcept {
		if (isString(type)) {
			return SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::StrSetting>(si.key));
		} else if (type == TYPE_NUMBER) {
			return SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(si.key));
		} else if (type == TYPE_BOOLEAN) {
			return SettingsManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(si.key));
		} else {
			dcassert(0);
		}

		return 0;
	}

	ApiSettingItem::EnumOption::List CoreSettingItem::getEnumOptions() const noexcept {
		EnumOption::List ret;

		auto enumStrings = SettingsManager::getEnumStrings(si.key, false);
		if (!enumStrings.empty()) {
			for (const auto& i : enumStrings) {
				ret.emplace_back(EnumOption({ i.first, ResourceManager::getInstance()->getString(i.second) }));
			}
		} else if (si.key == SettingsManager::BIND_ADDRESS || si.key == SettingsManager::BIND_ADDRESS6) {
			auto bindAddresses = AirUtil::getBindAdapters(si.key == SettingsManager::BIND_ADDRESS6);
			for (const auto& adapter : bindAddresses) {
				auto title = adapter.ip + (!adapter.adapterName.empty() ? " (" + adapter.adapterName + ")" : Util::emptyString);
				ret.emplace_back(EnumOption({ adapter.ip, title }));
			}
		} else if (si.key == SettingsManager::MAPPER) {
			auto mappers = ConnectivityManager::getInstance()->getMappers(false);
			for (const auto& mapper : mappers) {
				ret.emplace_back(EnumOption({ mapper, mapper }));
			}
		} else if (si.key == SettingsManager::LANGUAGE_FILE) {
			for (const auto& language: Localization::getLanguages()) {
				ret.emplace_back(EnumOption({ language.getLanguageSettingValue(), language.getLanguageName() }));
			}
		}

		return ret;
	}

	string CoreSettingItem::getTitle() const noexcept {
		return ApiSettingItem::formatTitle(si.desc, unit);
	}

	void CoreSettingItem::unset() noexcept {
		si.unset();
	}

	bool CoreSettingItem::setValue(const json& aJson) {
		if (isString(type)) {
			SettingsManager::getInstance()->set(static_cast<SettingsManager::StrSetting>(si.key), JsonUtil::parseValue<string>(name, aJson));
		} else if (type == TYPE_NUMBER) {
			SettingsManager::getInstance()->set(static_cast<SettingsManager::IntSetting>(si.key), JsonUtil::parseValue<int>(name, aJson));
		} else if (type == TYPE_BOOLEAN) {
			SettingsManager::getInstance()->set(static_cast<SettingsManager::BoolSetting>(si.key), JsonUtil::parseValue<bool>(name, aJson));
		} else {
			dcassert(0);
			return false;
		}

		return true;
	}
}
