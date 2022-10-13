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

#ifndef DCPLUSPLUS_DCPP_FAVORITEHUBUTILS_H
#define DCPLUSPLUS_DCPP_FAVORITEHUBUTILS_H

#include <api/common/Property.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/HubEntry.h>


namespace webserver {
	class FavoriteHubUtils {
	public:
		static const PropertyList properties;
		static const PropertyItemHandler<FavoriteHubEntryPtr> propertyHandler;

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_HUB_URL,
			PROP_HUB_DESCRIPTION,
			PROP_AUTO_CONNECT,
			PROP_SHARE_PROFILE,
			PROP_CONNECT_STATE,

			PROP_NICK,
			PROP_HAS_PASSWORD,
			PROP_USER_DESCRIPTION,
			PROP_NMDC_ENCODING,
			PROP_AWAY_MESSAGE,

			PROP_CONN_MODE4,
			PROP_CONN_MODE6,
			PROP_IP4,
			PROP_IP6,

			PROP_SHOW_JOINS,
			PROP_FAV_SHOW_JOINS,
			PROP_CHAT_NOTIFY,
			PROP_LOG_HUB_CHAT,
			PROP_LAST
		};

		static json serializeHub(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept;

		static int compareEntries(const FavoriteHubEntryPtr& a, const FavoriteHubEntryPtr& b, int aPropertyName) noexcept;
		static std::string getStringInfo(const FavoriteHubEntryPtr& a, int aPropertyName) noexcept;
		static double getNumericInfo(const FavoriteHubEntryPtr& a, int aPropertyName) noexcept;
	private:
		static string getConnectStateStr(const FavoriteHubEntryPtr& aEntry) noexcept;
		static string getConnectStateId(const FavoriteHubEntryPtr& aEntry) noexcept;
	};
}

#endif