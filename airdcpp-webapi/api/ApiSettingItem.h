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

#ifndef DCPLUSPLUS_DCPP_APISETTINGITEM_H
#define DCPLUSPLUS_DCPP_APISETTINGITEM_H

#include <web-server/stdinc.h>

#include <airdcpp/GetSet.h>
#include <airdcpp/SettingItem.h>
#include <airdcpp/ResourceManager.h>

namespace webserver {
#define MAX_INT_VALUE std::numeric_limits<int>::max()
	class ApiSettingItem {
	public:
		typedef vector<ApiSettingItem> List;
		enum Type {
			TYPE_NUMBER,
			TYPE_BOOLEAN,
			TYPE_STRING,
			TYPE_FILE_PATH,
			TYPE_DIRECTORY_PATH,
			TYPE_TEXT,
			TYPE_LAST
		};

		struct MinMax {
			const int min;
			const int max;
		};

		static const MinMax defaultMinMax;

		ApiSettingItem(const string& aName, Type aType);

		static bool isString(Type aType) noexcept {
			return aType == TYPE_STRING || aType == TYPE_TEXT || aType == TYPE_FILE_PATH || aType == TYPE_DIRECTORY_PATH;
		}

		// Returns the value and bool indicating whether it's an auto detected value
		virtual string getTitle() const noexcept = 0;

		virtual bool setValue(const json& aJson) = 0;
		virtual void unset() noexcept = 0;
		virtual json getValue() const noexcept = 0;
		virtual json getDefaultValue() const noexcept = 0;

		virtual bool isOptional() const noexcept = 0;
		virtual const MinMax& getMinMax() const noexcept = 0;

		struct EnumOption {
			const json id;
			const string text;
			typedef vector<EnumOption> List;
		};

		virtual EnumOption::List getEnumOptions() const noexcept = 0;

		virtual bool usingAutoValue(bool aForce) const noexcept;
		virtual json getAutoValue() const noexcept;

		const string name;
		const Type type;

		template<typename T, typename ListT>
		static T* findSettingItem(ListT& aSettings, const string& aKey) noexcept {
			auto p = find_if(aSettings.begin(), aSettings.end(), [&](const T& aItem) { return aItem.name == aKey; });
			if (p != aSettings.end()) {
				return &(*p);
			}

			return nullptr;
		}
	};

	class CoreSettingItem : public ApiSettingItem {
	public:
		enum Group {
			GROUP_NONE,
			GROUP_CONN_V4,
			GROUP_CONN_V6,
			GROUP_CONN_GEN,
			GROUP_LIMITS_DL,
			GROUP_LIMITS_UL,
			GROUP_LIMITS_MCN
		};

		CoreSettingItem(const string& aName, int aKey, ResourceManager::Strings aDesc, Type aType = TYPE_LAST, ResourceManager::Strings aUnit = ResourceManager::Strings::LAST);

		// Returns the value and bool indicating whether it's an auto detected value
		json getValue() const noexcept override;
		json getAutoValue() const noexcept override;

		// Throws on invalid JSON
		bool setValue(const json& aJson) override;
		void unset() noexcept override;

		string getTitle() const noexcept override;

		const ResourceManager::Strings unit;

		const MinMax& getMinMax() const noexcept override;
		bool isOptional() const noexcept override;

		static Type parseAutoType(Type aType, int aKey) noexcept;
		json getDefaultValue() const noexcept override;

		EnumOption::List getEnumOptions() const noexcept override;
		bool usingAutoValue(bool aForce) const noexcept override;
	private:
		const SettingItem si;
	};

	class ServerSettingItem : public ApiSettingItem {
	public:
		typedef vector<ServerSettingItem> List;

		ServerSettingItem(const string& aKey, const string& aTitle, const json& aDefaultValue, Type aType, bool aOptional = false, const MinMax& aMinMax = defaultMinMax);

		// Returns the value and bool indicating whether it's an auto detected value
		json getValue() const noexcept override;

		bool setValue(const json& aJson) override;

		const string desc;

		string getTitle() const noexcept override {
			return desc;
		}

		void unset() noexcept override;

		int num();
		uint64_t uint64();
		string str();
		bool boolean();

		bool isDefault() const noexcept;

		bool isOptional() const noexcept override {
			return optional;
		}

		const MinMax& getMinMax() const noexcept override;
		json getDefaultValue() const noexcept override;

		EnumOption::List getEnumOptions() const noexcept override;
		//ServerSettingItem(ServerSettingItem&& rhs) noexcept = default;
		//ServerSettingItem& operator=(ServerSettingItem&& rhs) noexcept = default;
	private:
		const MinMax minMax;

		const bool optional;
		json value;
		const json defaultValue;
	};
}

#endif