/*
 * Copyright (C) 2013-2019 AirDC++ Project
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

#include "SettingItem.h"
#include "SettingsManager.h"

namespace dcpp {

SettingItem::SettingValue SettingItem::getCurValue(bool useDefault) const noexcept {
	if(key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(key), useDefault);
	} else if(key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(key), useDefault);
	} else if(key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(key), useDefault);
	} else {
		dcassert(0);
	}
	return 0;
}

bool SettingItem::isSet() const noexcept {
	return SettingsManager::getInstance()->isKeySet(key);
}

void SettingItem::unset() const noexcept {
	SettingsManager::getInstance()->unsetKey(key);
}

bool SettingItem::isDefault() const noexcept {
	if(key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
		return SettingsManager::getInstance()->isDefault(static_cast<SettingsManager::StrSetting>(key));
	} else if(key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
		return SettingsManager::getInstance()->isDefault(static_cast<SettingsManager::IntSetting>(key));
	} else if(key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
			return SettingsManager::getInstance()->isDefault(static_cast<SettingsManager::BoolSetting>(key));
	} else {
		dcassert(0);
	}
	return true;
}

SettingItem::SettingValue SettingItem::getDefaultValue() const noexcept {
	if (key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
		return SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::StrSetting>(key));
	} else if (key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(key));
	} else if (key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(key));
	} else {
		dcassert(0);
	}
	return 0;
}

/*void useProfileValue() const {
	if(key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
		SettingsManager::getInstance()->set(static_cast<SettingsManager::StrSetting>(key), boost::get<string>(profileValue));
	} else if(key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
		return SettingsManager::getInstance()->set(static_cast<SettingsManager::IntSetting>(key), boost::get<int>(profileValue));
	} else if(key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
		return SettingsManager::getInstance()->set(static_cast<SettingsManager::BoolSetting>(key), boost::get<bool>(profileValue));
	} else {
		dcassert(0);
	}
}*/

const string& SettingItem::getDescription() const noexcept {
	return ResourceManager::getInstance()->getString(desc);
}

string SettingItem::currentToString() const noexcept {
	auto cur = getCurValue(true);
	return boost::apply_visitor(ToString(key), cur);
}

string SettingItem::ToString::operator()(const string& s) const noexcept {
	return s;
}

string SettingItem::ToString::operator()(int val) const noexcept {
	auto enumStrings = SettingsManager::getEnumStrings(val, true);
	if (!enumStrings.empty()) {
		return ResourceManager::getInstance()->getString(enumStrings[val]);
	}

	return Util::toString(val);
}

string SettingItem::ToString::operator()(double d) const noexcept {
	return Util::toString(d);
}

string SettingItem::ToString::operator()(bool b) const noexcept {
	return b ? STRING(ENABLED) : STRING(DISABLED);
}

ProfileSettingItem::ProfileSettingItem(int aKey, const SettingValue& aProfileValue, ResourceManager::Strings aName) :
profileValue(aProfileValue), SettingItem({ aKey, aName }) {

}


bool ProfileSettingItem::isProfileCurrent() const noexcept {
	return profileValue == getCurValue(false);
}

string ProfileSettingItem::profileToString() const noexcept {
	return boost::apply_visitor(ToString(key), profileValue);
}

void ProfileSettingItem::setProfileToDefault(bool reset) const noexcept {
	if (reset)
		SettingsManager::getInstance()->unsetKey(key);

	if (key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
		SettingsManager::getInstance()->setDefault(static_cast<SettingsManager::StrSetting>(key), boost::get<string>(profileValue));
	} else if (key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
		SettingsManager::getInstance()->setDefault(static_cast<SettingsManager::IntSetting>(key), boost::get<int>(profileValue));
	} else if (key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
		SettingsManager::getInstance()->setDefault(static_cast<SettingsManager::BoolSetting>(key), boost::get<bool>(profileValue));
	} else {
		dcassert(0);
	}
}

}