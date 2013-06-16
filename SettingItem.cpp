/*
 * Copyright (C) 2013 AirDC++ Project
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

SettingItem::SettingValue SettingItem::getCurValue() const {
	if(key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(key), false);
	} else if(key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
		return SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(key), false);
	} else if(key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
			return SettingsManager::getInstance()->get(static_cast<SettingsManager::BoolSetting>(key), false);
	} else {
		dcassert(0);
	}
	return 0;
}

bool SettingItem::isSet() const {
	return SettingsManager::getInstance()->isset(key);
}

bool SettingItem::isDefault() const {
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

void SettingItem::setDefault(bool reset) const {
	if (reset)
		SettingsManager::getInstance()->unset(key);

	if(key >= SettingsManager::STR_FIRST && key < SettingsManager::STR_LAST) {
		SettingsManager::getInstance()->setDefault(static_cast<SettingsManager::StrSetting>(key), boost::get<string>(profileValue));
	} else if(key >= SettingsManager::INT_FIRST && key < SettingsManager::INT_LAST) {
		SettingsManager::getInstance()->setDefault(static_cast<SettingsManager::IntSetting>(key), boost::get<int>(profileValue));
	} else if(key >= SettingsManager::BOOL_FIRST && key < SettingsManager::BOOL_LAST) {
		SettingsManager::getInstance()->setDefault(static_cast<SettingsManager::BoolSetting>(key), boost::get<bool>(profileValue));
	} else {
		dcassert(0);
	}
}

const string& SettingItem::getName() const {
	return ResourceManager::getInstance()->getString(name);
}

bool SettingItem::isProfileCurrent() const {
	return profileValue == getCurValue();
}

string SettingItem::profileToString() const {
	return boost::apply_visitor(ToString(key), profileValue);
}

string SettingItem::currentToString() const {
	auto cur = getCurValue();
	return boost::apply_visitor(ToString(key), cur);
}

string SettingItem::ToString::operator()(const string& s) const {
	return s;
}

string SettingItem::ToString::operator()(int s) const {
	return Util::toString(s);
}

string SettingItem::ToString::operator()(double d) const {
	return Util::toString(d);
}

string SettingItem::ToString::operator()(bool b) const {
	return b ? STRING(ENABLED) : STRING(DISABLED);
}

}