/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_FAVORITEHUBUTILS_H
#define DCPLUSPLUS_DCPP_FAVORITEHUBUTILS_H

#include <api/FavoriteHubApi.h>

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>

namespace webserver {
	class FavoriteHubUtils {
	public:
		static FavoriteHubEntryList getEntryList() noexcept;
		static json serializeHub(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept;

		static int compareEntries(const FavoriteHubEntryPtr& a, const FavoriteHubEntryPtr& b, int aPropertyName) noexcept;
		static std::string getStringInfo(const FavoriteHubEntryPtr& a, int aPropertyName) noexcept;
		static double getNumericInfo(const FavoriteHubEntryPtr& a, int aPropertyName) noexcept;

		static optional<int> deserializeIntHubSetting(const string& aFieldName, const json& aJson);
	private:
		static string formatConnectState(const FavoriteHubEntryPtr& aEntry) noexcept;
		static json serializeHubSetting(tribool aSetting) noexcept;
		static json serializeHubSetting(int aSetting) noexcept;
	};
}

#endif