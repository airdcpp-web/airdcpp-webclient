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

#include <api/FavoriteHubUtils.h>

#include <api/ApiModule.h>
#include <api/common/ListViewController.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/FavoriteManagerListener.h>


namespace webserver {
	class FavoriteHubApi : public SubscribableApiModule, private FavoriteManagerListener {
	public:
		FavoriteHubApi(Session* aSession);
		~FavoriteHubApi();
	private:
		api_return handleAddHub(ApiRequest& aRequest);
		api_return handleRemoveHub(ApiRequest& aRequest);
		api_return handleUpdateHub(ApiRequest& aRequest);

		api_return handleGetHubs(ApiRequest& aRequest);
		api_return handleGetHub(ApiRequest& aRequest);

		void updateProperties(FavoriteHubEntryPtr& aEntry, const json& j, bool aNewHub);

		void on(FavoriteManagerListener::FavoriteHubAdded, const FavoriteHubEntryPtr& /*e*/)  noexcept override;
		void on(FavoriteManagerListener::FavoriteHubRemoved, const FavoriteHubEntryPtr& e) noexcept override;
		void on(FavoriteManagerListener::FavoriteHubUpdated, const FavoriteHubEntryPtr& e) noexcept override;

		typedef ListViewController<FavoriteHubEntryPtr, FavoriteHubUtils::PROP_LAST> HubView;
		HubView view;

		static FavoriteHubEntryList getEntryList() noexcept;
		static optional<int> deserializeIntHubSetting(const string& aFieldName, const json& aJson);
	};
}

#endif