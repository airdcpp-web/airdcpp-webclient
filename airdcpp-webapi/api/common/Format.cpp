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

#include "Format.h"

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/core/geo/GeoManager.h>
#include <airdcpp/user/HintedUser.h>
#include <airdcpp/core/localization/ResourceManager.h>

namespace webserver {
	std::string Format::nicksToString(const HintedUser& aUser) noexcept {
		return Util::listToString(ClientManager::getInstance()->getNicks(aUser));
	}

	std::string Format::hubsToString(const HintedUser& aUser) noexcept {
		return Util::listToString(ClientManager::getInstance()->getHubNames(aUser));
	}

	std::string Format::formatIp(const string& aIP, const string& aCountryCode) noexcept {
		if (!aCountryCode.empty()) {
			return aCountryCode + " (" + aIP + ")";
		}

		return aIP;
	}

	std::string Format::formatIp(const string& aIP) noexcept {
		return formatIp(aIP, GeoManager::getInstance()->getCountry(aIP));
	}
}