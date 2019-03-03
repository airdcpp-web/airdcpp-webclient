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

#include <web-server/JsonUtil.h>

#include <api/common/SettingUtils.h>

#include <airdcpp/Util.h>


namespace webserver {
	json SettingUtils::serializeDefinition(const ApiSettingItem& aItem) noexcept {
		json ret = {
			{ "key", aItem.name },
			{ "title", aItem.getTitle() },
			{ "type", typeToStr(aItem.type) },
			{ "default_value", aItem.getDefaultValue() },
		};

		if (!aItem.getHelpStr().empty()) {
			ret["help"] = aItem.getHelpStr();
		}

		if (aItem.isOptional()) {
			ret["optional"] = true;
		}

		{
			for (const auto& opt : aItem.getEnumOptions()) {
				ret["options"].push_back({
					{ "id", opt.id },
					{ "name", opt.text },
				});
			}
		}

		if (aItem.type == ApiSettingItem::TYPE_NUMBER) {
			const auto& minMax = aItem.getMinMax();

			if (minMax.min != 0) {
				ret["min"] = minMax.min;
			}

			if (minMax.max != MAX_INT_VALUE) {
				ret["max"] = minMax.max;
			}
		}

		if (aItem.type == ApiSettingItem::TYPE_LIST) {
			ret["item_type"] = typeToStr(aItem.itemType);
			if (aItem.itemType == ApiSettingItem::TYPE_STRUCT) {
				dcassert(!aItem.getValueTypes().empty());
				for (const auto& valueType: aItem.getValueTypes()) {
					ret["definitions"].push_back(serializeDefinition(*valueType));
				}
			}
		}

		return ret;
	}

	string SettingUtils::typeToStr(ApiSettingItem::Type aType) noexcept {
		switch (aType) {
			case ApiSettingItem::TYPE_BOOLEAN: return "boolean";
			case ApiSettingItem::TYPE_NUMBER: return "number";
			case ApiSettingItem::TYPE_STRING: return "string";
			case ApiSettingItem::TYPE_FILE_PATH: return "file_path";
			case ApiSettingItem::TYPE_DIRECTORY_PATH: return "directory_path";
			case ApiSettingItem::TYPE_TEXT: return "text";
			case ApiSettingItem::TYPE_LIST: return "list";
			case ApiSettingItem::TYPE_STRUCT: return "struct";
			case ApiSettingItem::TYPE_LAST: dcassert(0);
		}

		dcassert(0);
		return Util::emptyString;
	}

	json SettingUtils::validateObjectListValue(const ApiSettingItem::PtrList& aPropertyDefinitions, const json& aValue) {
		// Unknown properties will be ignored...
		auto ret = json::object();
		for (const auto& def: aPropertyDefinitions) {
			auto i = aValue.find(def->name);
			if (i == aValue.end()) {
				ret[def->name] = validateValue(def->getDefaultValue(), *def);
			} else {
				ret[def->name] = validateValue(i.value(), *def);
			}
		}

		return ret;
	}

	json SettingUtils::validateValue(const json& aValue, const ApiSettingItem& aItem) {
		auto convertedValue = convertValue(aValue, aItem.name, aItem.type, aItem.itemType, aItem.isOptional(), aItem.getMinMax(), aItem.getValueTypes());
		if (!aItem.getEnumOptions().empty()) {
			validateEnumValue(convertedValue, aItem.name, aItem.type, aItem.itemType, aItem.getEnumOptions());
		}

		return convertedValue;
	}

	void SettingUtils::validateEnumValue(const json& aValue, const string& aKey, ApiSettingItem::Type aType, ApiSettingItem::Type aItemType, const ApiSettingItem::EnumOption::List& aEnumOptions) {
		if (!ApiSettingItem::optionsAllowed(aType, aItemType)) {
			JsonUtil::throwError(aKey, JsonUtil::ERROR_INVALID, "options not supported for type " + typeToStr(aType));
		}
				
		if (aType == ApiSettingItem::TYPE_LIST) {
			// Array, validate all values
			for (const auto& itemId: aValue) {
				auto i = boost::find_if(aEnumOptions, [&](const ApiSettingItem::EnumOption& opt) { return opt.id == itemId; });
				if (i == aEnumOptions.end()) {
					JsonUtil::throwError(aKey, JsonUtil::ERROR_INVALID, "All values can't be found from enum options");
				}
			}
		} else if (aType == ApiSettingItem::TYPE_NUMBER || aType == ApiSettingItem::TYPE_STRING) {
			// Single value
			auto i = boost::find_if(aEnumOptions, [&](const ApiSettingItem::EnumOption& opt) { return opt.id == aValue; });
			if (i == aEnumOptions.end()) {
				JsonUtil::throwError(aKey, JsonUtil::ERROR_INVALID, "Value is not one of the enum options");
			}
		}
	}

	json SettingUtils::convertValue(const json& aValue, const string& aKey, ApiSettingItem::Type aType, ApiSettingItem::Type aItemType, bool aOptional, const ApiSettingItem::MinMax& aMinMax, const ApiSettingItem::PtrList& aObjectValues) {
		if (aType == ApiSettingItem::TYPE_NUMBER) {
			return parseIntSetting(aKey, aValue, aOptional, aMinMax);
		} else if (ApiSettingItem::isString(aType)) {
			return parseStringSetting(aKey, aValue, aOptional, aType);
		} else if (aType == ApiSettingItem::TYPE_BOOLEAN) {
			return JsonUtil::parseValue<bool>(aKey, aValue, aOptional);
		} else if (aType == ApiSettingItem::TYPE_LIST) {
			if (aItemType == ApiSettingItem::TYPE_STRUCT) {
				auto ret = json::array();
				for (const auto& listValueObj : JsonUtil::parseValue<json::array_t>(aKey, aValue, aOptional)) {
					ret.push_back(validateObjectListValue(aObjectValues, JsonUtil::parseValue<json::object_t>(aKey, listValueObj, false)));
				}

				return ret;
			} else if (aItemType == ApiSettingItem::TYPE_NUMBER) {
				auto ret = json::array();
				for (const int item : JsonUtil::parseValue<ApiSettingItem::ListNumber>(aKey, aValue, aOptional)) {
					ret.push_back(parseIntSetting(aKey, item, false, aMinMax));
				}

				return ret;
			} else if (ApiSettingItem::isString(aItemType)) {
				auto ret = json::array();
				for (const string& item : JsonUtil::parseValue<ApiSettingItem::ListString>(aKey, aValue, aOptional)) {
					ret.push_back(parseStringSetting(aKey, item, false, aItemType));
				}

				return ret;
			} else {
				JsonUtil::throwError(aKey, JsonUtil::ERROR_INVALID, "type " + typeToStr(aItemType) + " is not supported for list items");
			}
		} else if (aType == ApiSettingItem::TYPE_STRUCT) {
			JsonUtil::throwError(aKey, JsonUtil::ERROR_INVALID, "object type is supported only for list items");
		}

		dcassert(0);
		return nullptr;
	}

	ExtensionSettingItem::List SettingUtils::deserializeDefinitions(const json& aJson) {
		ExtensionSettingItem::List ret;

		for (const auto& def: aJson) {
			ret.push_back(deserializeDefinition(def));
		}

		return ret;
	}

	json SettingUtils::parseEnumOptionId(const json& aJson, ApiSettingItem::Type aType) {
		if (aType == ApiSettingItem::TYPE_NUMBER) {
			return JsonUtil::getField<int>("id", aJson, false);
		}

		return JsonUtil::getField<string>("id", aJson, false);
	}

	json SettingUtils::parseStringSetting(const string& aFieldName, const json& aJson, bool aOptional, ApiSettingItem::Type aType) {
		auto value = JsonUtil::parseValue<string>(aFieldName, aJson, aOptional);

		// Validate paths
		if (aType == ApiSettingItem::TYPE_DIRECTORY_PATH) {
			value = Util::validatePath(value, true);
		} else if (aType == ApiSettingItem::TYPE_FILE_PATH) {
			value = Util::validatePath(value, false);
		}

		return value;
	}

	json SettingUtils::parseIntSetting(const string& aFieldName, const json& aJson, bool aOptional, const ApiSettingItem::MinMax& aMinMax) {
		auto num = JsonUtil::parseValue<int>(aFieldName, aJson, aOptional);

		// Validate range
		JsonUtil::validateRange(aFieldName, num, aMinMax.min, aMinMax.max);

		return num;
	}

	ExtensionSettingItem SettingUtils::deserializeDefinition(const json& aJson, bool aIsListValue) {
		auto key = JsonUtil::getField<string>("key", aJson, false);
		auto title = JsonUtil::getField<string>("title", aJson, false);

		auto type = deserializeType("type", aJson, false);
		auto itemType = deserializeType("item_type", aJson, type != ApiSettingItem::TYPE_LIST);

		if (aIsListValue && type == ApiSettingItem::TYPE_LIST) {
			JsonUtil::throwError("type", JsonUtil::ERROR_INVALID, "Field of type " + typeToStr(type) + " can't be used for list item");
		}

		auto isOptional = JsonUtil::getOptionalFieldDefault<bool>("optional", aJson, false);
		if (isOptional && (type == ApiSettingItem::TYPE_BOOLEAN || type == ApiSettingItem::TYPE_NUMBER)) {
			JsonUtil::throwError("optional", JsonUtil::ERROR_INVALID, "Field of type " + typeToStr(type) + " can't be optional");
		}

		auto help = JsonUtil::getOptionalFieldDefault<string>("help", aJson, Util::emptyString);

		ApiSettingItem::MinMax minMax = {
			JsonUtil::getOptionalFieldDefault<int>("min", aJson, 0),
			JsonUtil::getOptionalFieldDefault<int>("max", aJson, MAX_INT_VALUE)
		};

		ExtensionSettingItem::List objectValues;
		if (type == ApiSettingItem::TYPE_LIST && itemType == ApiSettingItem::TYPE_STRUCT) {
			for (const auto& valueJ: JsonUtil::getRawField("definitions", aJson)) {
				objectValues.push_back(deserializeDefinition(valueJ, true));
			}
		}

		auto defaultValue = convertValue(
			JsonUtil::getOptionalRawField("default_value", aJson, !isOptional), 
			key, type, itemType, true, minMax, ApiSettingItem::valueTypesToPtrList(objectValues)
		);

		ApiSettingItem::EnumOption::List enumOptions;
		if (ApiSettingItem::optionsAllowed(type, itemType)) {
			auto optionsJson = JsonUtil::getOptionalRawField("options", aJson, false);
			if (!optionsJson.is_null()) {
				for (const auto& opt : optionsJson) {
					enumOptions.push_back({
						parseEnumOptionId(opt, type),
						JsonUtil::getField<string>("name", opt, false)
						});
				}
			}
		}

		if (!enumOptions.empty()) {
			validateEnumValue(defaultValue, key, type, itemType, enumOptions);
		}

		return ExtensionSettingItem(key, title, defaultValue, type, isOptional, minMax, objectValues, help, itemType, enumOptions);
	}

	ExtensionSettingItem::Type SettingUtils::deserializeType(const string& aFieldName, const json& aJson, bool aOptional) {
		auto itemTypeStr = JsonUtil::getOptionalField<string>(aFieldName, aJson, !aOptional);
		if (itemTypeStr) {
			if (*itemTypeStr == "string") {
				return ApiSettingItem::TYPE_STRING;
			} else if (*itemTypeStr == "boolean") {
				return ApiSettingItem::TYPE_BOOLEAN;
			} else if (*itemTypeStr == "number") {
				return ApiSettingItem::TYPE_NUMBER;
			} else if (*itemTypeStr == "text") {
				return ApiSettingItem::TYPE_TEXT;
			} else if (*itemTypeStr == "file_path") {
				return ApiSettingItem::TYPE_FILE_PATH;
			} else if (*itemTypeStr == "directory_path") {
				return ApiSettingItem::TYPE_DIRECTORY_PATH;
			} else if (*itemTypeStr == "list") {
				return ApiSettingItem::TYPE_LIST;
			} else if (*itemTypeStr == "struct") {
				return ApiSettingItem::TYPE_STRUCT;
			}

			dcassert(0);
			JsonUtil::throwError(aFieldName, JsonUtil::ERROR_INVALID, "Invalid item type " + *itemTypeStr);
		}

		return ApiSettingItem::TYPE_LAST;
	}
}
