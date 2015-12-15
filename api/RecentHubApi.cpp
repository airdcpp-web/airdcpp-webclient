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

#include <api/RecentHubApi.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/FavoriteManager.h>

namespace webserver {
	RecentHubApi::RecentHubApi(Session* aSession) : ApiModule(aSession) {
		METHOD_HANDLER("hubs", Access::HUBS_VIEW, ApiRequest::METHOD_GET, (NUM_PARAM), false, RecentHubApi::handleGetHubs);
		METHOD_HANDLER("search", Access::HUBS_VIEW, ApiRequest::METHOD_POST, (), true, RecentHubApi::handleSearchHubs);
	}

	RecentHubApi::~RecentHubApi() {
	}

	json RecentHubApi::serializeHub(const RecentHubEntryPtr& aHub) noexcept {
		return{
			{ "name", aHub->getName()},
			{ "description", aHub->getDescription() },
			{ "hub_url", aHub->getServer() }
		};
	}

	api_return RecentHubApi::handleSearchHubs(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto pattern = JsonUtil::getField<string>("pattern", reqJson);
		auto maxResults = JsonUtil::getField<size_t>("max_results", reqJson);

		auto hubs = FavoriteManager::getInstance()->searchRecentHubs(pattern, maxResults);

		json retJson;
		for (const auto& h : hubs) {
			retJson.push_back(serializeHub(h));
		}

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
	}

	api_return RecentHubApi::handleGetHubs(ApiRequest& aRequest) {
		
		return websocketpp::http::status_code::ok;
	}
}