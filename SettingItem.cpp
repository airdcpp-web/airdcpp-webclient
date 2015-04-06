/*
 * Copyright (C) 2013-2015 AirDC++ Project
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

SettingItem::SettingValue SettingItem::getCurValue(bool useDefault) const {
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

bool SettingItem::isSet() const {
	return SettingsManager::getInstance()->isKeySet(key);
}

void SettingItem::unset() const {
	SettingsManager::getInstance()->unsetKey(key);
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

SettingItem::SettingValue SettingItem::getDefaultValue() const {
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

const string& SettingItem::getDescription() const {
	return ResourceManager::getInstance()->getString(desc);
}

string SettingItem::currentToString() const {
	auto cur = getCurValue(true);
	return boost::apply_visitor(ToString(key), cur);
}

string SettingItem::ToString::operator()(const string& s) const {
	return s;
}

string SettingItem::ToString::operator()(int val) const {
	ResourceManager::Strings s = ResourceManager::LAST;
	if ((key == SettingsManager::INCOMING_CONNECTIONS || key == SettingsManager::INCOMING_CONNECTIONS6) && val < SettingsManager::INCOMING_LAST)
		s = SettingsManager::incomingStrings[val+1];

	if (key == SettingsManager::MONITORING_MODE && val < SettingsManager::MONITORING_LAST)
		s = SettingsManager::monitoringStrings[val];

	if (key == SettingsManager::TLS_MODE && val < SettingsManager::TLS_LAST)
		s = SettingsManager::encryptionStrings[val];

	if (key == SettingsManager::OUTGOING_CONNECTIONS && val < SettingsManager::OUTGOING_LAST)
		s = SettingsManager::outgoingStrings[val];

	if (key == SettingsManager::DL_AUTO_DISCONNECT_MODE && val < SettingsManager::QUEUE_LAST)
		s = SettingsManager::dropStrings[val];

	if (key == SettingsManager::BLOOM_MODE && val < SettingsManager::BLOOM_LAST)
		s = SettingsManager::bloomStrings[val];

	if (key == SettingsManager::DELAY_COUNT_MODE && val < SettingsManager::DELAY_LAST)
		s = SettingsManager::delayStrings[val];

	if (key == SettingsManager::AUTOPRIO_TYPE && val < SettingsManager::PRIO_LAST)
		s = SettingsManager::prioStrings[val];

	if (key == SettingsManager::SETTINGS_PROFILE && val < SettingsManager::PROFILE_LAST)
		s = SettingsManager::profileStrings[val];

	if (s != ResourceManager::LAST) {
		return ResourceManager::getInstance()->getString(s);
	}

	return Util::toString(val);
}

string SettingItem::ToString::operator()(double d) const {
	return Util::toString(d);
}

string SettingItem::ToString::operator()(bool b) const {
	return b ? STRING(ENABLED) : STRING(DISABLED);
}

ProfileSettingItem::ProfileSettingItem(int aKey, const SettingValue& aProfileValue, ResourceManager::Strings aName) :
profileValue(aProfileValue), SettingItem({ aKey, aName }) {

}


bool ProfileSettingItem::isProfileCurrent() const {
	return profileValue == getCurValue(false);
}

string ProfileSettingItem::profileToString() const {
	return boost::apply_visitor(ToString(key), profileValue);
}

void ProfileSettingItem::setProfileToDefault(bool reset) const {
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