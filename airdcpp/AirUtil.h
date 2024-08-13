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

#ifndef DCPLUSPLUS_DCPP_AIRUTIL_H
#define DCPLUSPLUS_DCPP_AIRUTIL_H

#include "compiler.h"

#include "Priority.h"
#include "SettingsManager.h"

namespace dcpp {

class AirUtil {
	
public:
	class TimeCounter {
	public:
		TimeCounter(string aMsg);
		~TimeCounter();
	private:
		time_t start;
		string msg;
	};

	static int getSlotsPerUser(bool download, double value=0, int aSlots=0, SettingsManager::SettingProfile aProfile = static_cast<SettingsManager::SettingProfile>(SETTING(SETTINGS_PROFILE)));
	static int getSlots(bool download, double value=0, SettingsManager::SettingProfile aProfile = static_cast<SettingsManager::SettingProfile>(SETTING(SETTINGS_PROFILE)));

	// Maximum wanted download/upload speed. Uses set connection values by default.
	static int getSpeedLimit(bool download, double value=0);
	static int getMaxAutoOpened(double value = 0);

	static string getPrioText(Priority aPriority) noexcept;

	static string formatMatchResults(int aMatchingFiles, int aNewFiles, const BundleList& aBundles) noexcept;
};

}
#endif