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

#ifndef DCPLUSPLUS_DCPP_FAVORITEHUBAPI_H
#define DCPLUSPLUS_DCPP_FAVORITEHUBAPI_H

#include <web-server/stdinc.h>

#include <api/ApiModule.h>
#include <api/common/ListViewController.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/FavoriteManagerListener.h>
#include <airdcpp/HubEntry.h>

namespace webserver {
	class FavoriteHubApi : public ApiModule, private FavoriteManagerListener {
	public:
		FavoriteHubApi(Session* aSession);
		~FavoriteHubApi();

		int getVersion() const noexcept {
			return 0;
		}

		const PropertyList properties = {
			{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
			{ PROP_HUB_URL, "hub_url", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
			{ PROP_HUB_DESCRIPTION, "hub_description", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
			{ PROP_AUTO_CONNECT, "auto_connect", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
			{ PROP_SHARE_PROFILE, "share_profile", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
			{ PROP_CONNECT_STATE, "connect_state", TYPE_NUMERIC_OTHER, SERIALIZE_CUSTOM, SORT_NUMERIC },
			{ PROP_NICK, "nick", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
			{ PROP_HAS_PASSWORD, "has_password", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
			{ PROP_USER_DESCRIPTION, "user_description", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		};

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
			PROP_LAST
		};
	private:
		api_return handleAddHub(ApiRequest& aRequest);
		api_return handleRemoveHub(ApiRequest& aRequest);
		api_return handleUpdateHub(ApiRequest& aRequest);
		api_return handleGetHub(ApiRequest& aRequest);

		// Returns error if there are invalid properties
		string updateValidatedProperties(FavoriteHubEntryPtr& aEntry, json& j, bool aNewHub);

		// Values that don't need to be validated
		void updateSimpleProperties(FavoriteHubEntryPtr& aEntry, json& j);

		void on(FavoriteManagerListener::FavoriteHubAdded, const FavoriteHubEntryPtr& /*e*/)  noexcept;
		void on(FavoriteManagerListener::FavoriteHubRemoved, const FavoriteHubEntryPtr& e) noexcept;
		void on(FavoriteManagerListener::FavoriteHubUpdated, const FavoriteHubEntryPtr& e) noexcept;

		PropertyItemHandler<FavoriteHubEntryPtr> itemHandler;

		typedef ListViewController<FavoriteHubEntryPtr, PROP_LAST> HubView;
		HubView view;
	};
}

#endif