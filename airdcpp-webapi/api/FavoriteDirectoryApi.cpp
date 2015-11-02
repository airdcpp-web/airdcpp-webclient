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

#include <api/FavoriteDirectoryApi.h>
//#include <web-server/JsonUtil.h>

#include <airdcpp/FavoriteManager.h>

namespace webserver {
	FavoriteDirectoryApi::FavoriteDirectoryApi(Session* aSession) : ApiModule(aSession) {
		METHOD_HANDLER("directories", ApiRequest::METHOD_GET, (), false, FavoriteDirectoryApi::handleGetDirectories);
	}

	FavoriteDirectoryApi::~FavoriteDirectoryApi() {
	}

	api_return FavoriteDirectoryApi::handleGetDirectories(ApiRequest& aRequest) {
		json ret;

		auto directories = FavoriteManager::getInstance()->getFavoriteDirs();
		if (!directories.empty()) {
			for (const auto& vPath : directories) {
				json parentJson;
				parentJson["name"] = vPath.first;
				for (const auto& realPath : vPath.second) {
					parentJson["paths"].push_back(realPath);
				}

				ret.push_back(parentJson);
			}
		} else {
			ret = json::array();
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}
}