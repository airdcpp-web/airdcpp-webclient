/*
* Copyright (C) 2013-2024 AirDC++ Project
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

#include "SettingHolder.h"

#include "ClientManager.h"


namespace dcpp {

SettingHolder::SettingHolder(MessageCallback aErrorF) : errorF(aErrorF)  {
	for (const auto& changeHandler : SettingsManager::getInstance()->getChangeCallbacks()) {
		SettingsManager::SettingValueList settingValues;

		for (auto settingKey : changeHandler.settingKeys) {
			settingValues.push_back(SettingsManager::getInstance()->getSettingValue(settingKey));
		}

		valueHolders.emplace_back(SettingValueListHolder(changeHandler, settingValues));
	}
}

SettingHolder::~SettingHolder() {

}

void SettingHolder::apply() {
	for (const auto& valueHolder: valueHolders) {
		for (auto i = 0; i < valueHolder.handler.settingKeys.size(); ++i) {
			SettingsManager::SettingKeyList changedValues;

			auto settingKey = valueHolder.handler.settingKeys[i];
			if (SettingsManager::getInstance()->getSettingValue(settingKey) != valueHolder.values[i]) {
				changedValues.push_back(settingKey);
			}

			if (!changedValues.empty()) {
				valueHolder.handler.onChanged([this](auto... params) { showError(params...); }, changedValues);
			}
		}
	}

	ClientManager::getInstance()->infoUpdated();
}

void SettingHolder::showError(const string& aError) const noexcept{
	if (errorF)
		errorF(aError);
}

}