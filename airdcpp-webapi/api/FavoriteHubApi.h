/*
* Copyright (C) 2011-2016 AirDC++ Project
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

		static const PropertyList properties;

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
			PROP_IGNORE_PM,
			PROP_LAST
		};
	private:
		api_return handleAddHub(ApiRequest& aRequest);
		api_return handleRemoveHub(ApiRequest& aRequest);
		api_return handleUpdateHub(ApiRequest& aRequest);

		api_return handleGetHubs(ApiRequest& aRequest);
		api_return handleGetHub(ApiRequest& aRequest);

		void updateProperties(FavoriteHubEntryPtr& aEntry, const json& j, bool aNewHub);

		void on(FavoriteManagerListener::FavoriteHubAdded, const FavoriteHubEntryPtr& /*e*/)  noexcept;
		void on(FavoriteManagerListener::FavoriteHubRemoved, const FavoriteHubEntryPtr& e) noexcept;
		void on(FavoriteManagerListener::FavoriteHubUpdated, const FavoriteHubEntryPtr& e) noexcept;

		static const PropertyItemHandler<FavoriteHubEntryPtr> itemHandler;

		typedef ListViewController<FavoriteHubEntryPtr, PROP_LAST> HubView;
		HubView view;
	};
}

#endif