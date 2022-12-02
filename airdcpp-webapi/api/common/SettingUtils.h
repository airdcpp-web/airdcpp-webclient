/*
* Copyright (C) 2011-2022 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SETTING_UTILS_H
#define DCPLUSPLUS_DCPP_SETTING_UTILS_H

#include <airdcpp/forward.h>

#include <web-server/ApiSettingItem.h>


namespace webserver {
	class SettingUtils {
	public:
		static json validateValue(const json& aValue, const ApiSettingItem& aItem, UserList* userReferences_);

		static ExtensionSettingItem deserializeDefinition(const json& aJson, bool aIsListValue = false);
		static ExtensionSettingItem::List deserializeDefinitions(const json& aJson);

		static json serializeDefinition(const ApiSettingItem& aItem) noexcept;
	private:
		static void validateEnumValue(const json& aValue, const string& aKey, ApiSettingItem::Type aType, ApiSettingItem::Type aItemType, const ApiSettingItem::EnumOption::List& aEnumOptions);
		static json validateObjectListValue(const ApiSettingItem::PtrList& aPropertyDefinitions, const json& aValue, UserList* userReferences_);

		static json convertValue(const json& aValue, const string& aKey, ApiSettingItem::Type aType, ApiSettingItem::Type aItemType, bool aOptional, const ApiSettingItem::MinMax& aMinMax, const ApiSettingItem::PtrList& aObjectValues, UserList* userReferences_);
		static json convertListCompatibleValue(const json& aValue, const string& aKey, ApiSettingItem::Type aType, bool aOptional, const ApiSettingItem::MinMax& aMinMax, UserList* userReferences_);


		static json parseEnumOptionId(const json& aJson, ApiSettingItem::Type aType);
		static json parseStringSetting(const string& aFieldName, const json& aJson, bool aOptional, ApiSettingItem::Type aType);
		static json parseIntSetting(const string& aFieldName, const json& aJson, bool aOptional, const ApiSettingItem::MinMax& aMinMax);

		static bool isListCompatibleValue(ApiSettingItem::Type aType) noexcept {
			return aType == ApiSettingItem::TYPE_NUMBER || ApiSettingItem::isString(aType) || aType == ApiSettingItem::TYPE_HINTER_USER;
		}

		static string typeToStr(ApiSettingItem::Type aType) noexcept;
		static ApiSettingItem::Type deserializeType(const string& aFieldName, const json& aJson, bool aOptional);
	};
}

#endif