/*
 * Copyright (C) 2013-2022 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SETTINGITEM_
#define DCPLUSPLUS_DCPP_SETTINGITEM_

#include "stdinc.h"

#include "ResourceManager.h"

#include <boost/variant.hpp>

namespace dcpp {


struct SettingItem {
	typedef boost::variant<string, bool, int, double> SettingValue;
	typedef vector<SettingItem> List;

	const int key;
	const ResourceManager::Strings desc;

	SettingValue getCurValue(bool useDefault = true) const noexcept;
	SettingValue getDefaultValue() const noexcept;

	void unset() const noexcept;
	bool isSet() const noexcept;
	bool isDefault() const noexcept;

	const string& getDescription() const noexcept;
	string currentToString() const noexcept;

	struct ToString : boost::static_visitor<string> {
		ToString(int aKey) : key(aKey) { }

		string operator()(const string& s) const noexcept;
		string operator()(int s) const noexcept;
		string operator()(double d) const noexcept;
		string operator()(bool b) const noexcept;
	private:
		const int key;
	};

	class CompareKey {
	public:
		CompareKey(int aKey) : key(aKey) { }
		bool operator()(const SettingItem& s) const noexcept { return s.key == key; }
	private:
		const int key;
	};

	bool operator==(const SettingItem& aSetting) const noexcept { return aSetting.key == key; }
	struct Hash {
		size_t operator()(const SettingItem& x) const noexcept { return hash<int>()(x.key); }
	};
};

struct ProfileSettingItem : public SettingItem {
	ProfileSettingItem(int aKey, const SettingValue& aProfileValue, ResourceManager::Strings aName);

	typedef vector<ProfileSettingItem> List;

	const SettingValue profileValue;

	void setProfileToDefault(bool reset) const noexcept;
	bool isProfileCurrent() const noexcept;
	string profileToString() const noexcept;
};

}

#endif