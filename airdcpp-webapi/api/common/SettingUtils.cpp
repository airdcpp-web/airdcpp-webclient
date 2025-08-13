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

#include <web-server/JsonUtil.h>

#include <api/common/SettingUtils.h>
#include <api/common/Deserializer.h>

#include <airdcpp/user/HintedUser.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/util/Util.h>


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

			ret["min"] = minMax.min;

			if (minMax.max != MAX_INT_VALUE) {
				ret["max"] = minMax.max;
			}
		}

		if (aItem.type == ApiSettingItem::TYPE_LIST) {
			ret["item_type"] = typeToStr(aItem.itemType);
			if (aItem.itemType == ApiSettingItem::TYPE_STRUCT) {
				dcassert(!aItem.getListObjectFields().empty());
				for (const auto& valueType: aItem.getListObjectFields()) {
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
			case ApiSettingItem::TYPE_TEXT: return "text";
			case ApiSettingItem::TYPE_URL: return "url";
			case ApiSettingItem::TYPE_PASSWORD: return "password";
			case ApiSettingItem::TYPE_EMAIL: return "email";
			case ApiSettingItem::TYPE_FILE_PATH: return "file_path";
			case ApiSettingItem::TYPE_EXISTING_FILE_PATH: return "existing_file_path";
			case ApiSettingItem::TYPE_DIRECTORY_PATH: return "directory_path";
			case ApiSettingItem::TYPE_LIST: return "list";
			case ApiSettingItem::TYPE_STRUCT: return "struct";
			case ApiSettingItem::TYPE_HUB_URL: return "hub_url";
			case ApiSettingItem::TYPE_HINTER_USER: return "hinted_user";
			case ApiSettingItem::TYPE_LAST: dcassert(0);
		}

		dcassert(0);
		return Util::emptyString;
	}

	json SettingUtils::validateObjectListValue(const ApiSettingItem::PtrList& aPropertyDefinitions, const json& aValue, SettingReferenceList* references_) {
		// Unknown properties will be ignored...
		auto ret = json::object();
		for (const auto& def: aPropertyDefinitions) {
			auto i = aValue.find(def->name);
			if (i == aValue.end()) {
				ret[def->name] = validateValue(def->getDefaultValue(), *def, references_);
			} else {
				ret[def->name] = validateValue(i.value(), *def, references_);
			}
		}

		return ret;
	}

	json SettingUtils::validateValue(const json& aValue, const ApiSettingItem& aItem, SettingReferenceList* references_) {
		auto convertedValue = convertValue(aValue, aItem.name, aItem.type, aItem.itemType, aItem.isOptional(), aItem.getMinMax(), aItem.getListObjectFields(), references_);
		if (!aItem.getEnumOptions().empty()) {
			validateEnumValue(convertedValue, aItem.name, aItem.type, aItem.itemType, aItem.getEnumOptions());
		}

		return convertedValue;
	}

	void SettingUtils::validateEnumValue(const json& aValue, const string& aKey, ApiSettingItem::Type aType, ApiSettingItem::Type aItemType, const ApiSettingItem::EnumOption::List& aEnumOptions) {
		if (!ApiSettingItem::enumOptionsAllowed(aType, aItemType)) {
			JsonUtil::throwError(aKey, JsonException::ERROR_INVALID, "options not supported for type " + typeToStr(aType));
		}
				
		if (aType == ApiSettingItem::TYPE_LIST) {
			// Array, validate all values
			for (const auto& itemId: aValue) {
				auto i = ranges::find_if(aEnumOptions, [&](const ApiSettingItem::EnumOption& opt) { return opt.id == itemId; });
				if (i == aEnumOptions.end()) {
					JsonUtil::throwError(aKey, JsonException::ERROR_INVALID, "All values can't be found from enum options");
				}
			}
		} else if (aType == ApiSettingItem::TYPE_NUMBER || aType == ApiSettingItem::TYPE_STRING) {
			// Single value
			auto i = ranges::find_if(aEnumOptions, [&](const ApiSettingItem::EnumOption& opt) { return opt.id == aValue; });
			if (i == aEnumOptions.end()) {
				JsonUtil::throwError(aKey, JsonException::ERROR_INVALID, "Value is not one of the enum options");
			}
		}
	}

	json SettingUtils::convertValue(
		const json& aValue, const string& aKey, ApiSettingItem::Type aType, ApiSettingItem::Type aItemType, bool aOptional, 
		const ApiSettingItem::MinMax& aMinMax, const ApiSettingItem::PtrList& aObjectValues, SettingReferenceList* references_
	) {
		if (isListCompatibleValue(aType)) {
			return convertListCompatibleValue(aValue, aKey, aType, aOptional, aMinMax, references_);
		} else if (aType == ApiSettingItem::TYPE_BOOLEAN) {
			return JsonUtil::parseValue<bool>(aKey, aValue, aOptional);
		} else if (aType == ApiSettingItem::TYPE_LIST) {
			if (aItemType == ApiSettingItem::TYPE_STRUCT) {
				auto ret = json::array();
				for (const auto& listValueObj: JsonUtil::parseValue<json::array_t>(aKey, aValue, aOptional)) {
					ret.push_back(validateObjectListValue(aObjectValues, JsonUtil::parseValue<json::object_t>(aKey, listValueObj, false), references_));
				}

				return ret;
			} else if (isListCompatibleValue(aItemType)) {
				auto ret = json::array();
				for (const auto& item: JsonUtil::parseValue<json::array_t>(aKey, aValue, aOptional)) {
					ret.push_back(convertListCompatibleValue(item, aKey, aItemType, false, aMinMax, references_));
				}

				return ret;
			} else {
				JsonUtil::throwError(aKey, JsonException::ERROR_INVALID, "type " + typeToStr(aItemType) + " is not supported for list items");
			}
		} else if (aType == ApiSettingItem::TYPE_STRUCT) {
			JsonUtil::throwError(aKey, JsonException::ERROR_INVALID, "object type is supported only for list items");
		}

		dcassert(0);
		return nullptr;
	}

	json SettingUtils::convertListCompatibleValue(
		const json& aValue, const string& aKey, ApiSettingItem::Type aType, bool aOptional, 
		const ApiSettingItem::MinMax& aMinMax, SettingReferenceList* references_
	) {
		if (aType == ApiSettingItem::TYPE_NUMBER) {
			return parseIntSetting(aKey, aValue, aOptional, aMinMax);
		} else if (ApiSettingItem::isString(aType)) {
			return parseStringSetting(aKey, aValue, aOptional, aType);
		} else if (aType == ApiSettingItem::TYPE_HINTER_USER) {
			if (aValue.is_null()) {
				return nullptr;
			}

			auto user = Deserializer::parseOfflineHintedUser(aValue, aKey, false);
			if (references_) {
				(*references_).push_back(user.user);
			}

			return {
				{ "nicks", user.nicks },
				{ "cid", user.user->getCID().toBase32() },
				{ "hub_url", user.hint },
			};
		}

		dcassert(0);
		return json();
	}

	ExtensionSettingItem::List SettingUtils::deserializeDefinitions(const json& aJson) {
		ExtensionSettingItem::List ret;

		for (const auto& defJson: aJson) {
			auto def = deserializeDefinition(defJson);
			if (ApiSettingItem::findSettingItem<ExtensionSettingItem>(ret, def.name)) {
				JsonUtil::throwError("type", JsonException::ERROR_INVALID, "Duplicate setting definition key " + def.name + " detected");
			}

			ret.push_back(def);
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
			value = PathUtil::validateDirectoryPath(value);
		} else if (aType == ApiSettingItem::TYPE_FILE_PATH) {
			value = PathUtil::validateFilePath(value);
		} else if (aType == ApiSettingItem::TYPE_EXISTING_FILE_PATH) {
			value = PathUtil::validateFilePath(value);
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
			JsonUtil::throwError("type", JsonException::ERROR_INVALID, "Field of type " + typeToStr(type) + " can't be used for list item");
		}

		auto isOptional = JsonUtil::getOptionalFieldDefault<bool>("optional", aJson, false);
		if (isOptional && (type == ApiSettingItem::TYPE_BOOLEAN || type == ApiSettingItem::TYPE_NUMBER)) {
			JsonUtil::throwError("optional", JsonException::ERROR_INVALID, "Field of type " + typeToStr(type) + " can't be optional");
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
			key, type, itemType, true, minMax, ApiSettingItem::valueTypesToPtrList(objectValues),
			nullptr
		);

		ApiSettingItem::EnumOption::List enumOptions;
		if (ApiSettingItem::enumOptionsAllowed(type, itemType)) {
			auto optionsJson = JsonUtil::getOptionalRawField("options", aJson, false);
			if (!optionsJson.is_null()) {
				for (const auto& opt : optionsJson) {
					enumOptions.emplace_back(
						parseEnumOptionId(opt, type),
						JsonUtil::getField<string>("name", opt, false)
					);
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
			}
			else if (*itemTypeStr == "password") {
				return ApiSettingItem::TYPE_PASSWORD;
			} else if (*itemTypeStr == "url") {
				return ApiSettingItem::TYPE_URL;
			} else if (*itemTypeStr == "email") {
				return ApiSettingItem::TYPE_EMAIL;
			} else if (*itemTypeStr == "file_path") {
				return ApiSettingItem::TYPE_FILE_PATH;
			} else if (*itemTypeStr == "existing_file_path") {
				return ApiSettingItem::TYPE_EXISTING_FILE_PATH;
			} else if (*itemTypeStr == "directory_path") {
				return ApiSettingItem::TYPE_DIRECTORY_PATH;
			} else if (*itemTypeStr == "hub_url") {
				return ApiSettingItem::TYPE_HUB_URL;
			} else if (*itemTypeStr == "hinted_user") {
				return ApiSettingItem::TYPE_HINTER_USER;
			} else if (*itemTypeStr == "list") {
				return ApiSettingItem::TYPE_LIST;
			} else if (*itemTypeStr == "struct") {
				return ApiSettingItem::TYPE_STRUCT;
			}

			JsonUtil::throwError(aFieldName, JsonException::ERROR_INVALID, "Invalid item type \"" + *itemTypeStr + "\"");
		}

		return ApiSettingItem::TYPE_LAST;
	}
}
