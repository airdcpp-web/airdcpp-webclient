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

#include <api/FavoriteDirectoryApi.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/FavoriteManager.h>

namespace webserver {
	FavoriteDirectoryApi::FavoriteDirectoryApi(Session* aSession) : SubscribableApiModule(aSession, Access::ANY) {
		// TODO: fix method naming in the next major version
		METHOD_HANDLER("directories", Access::ANY, ApiRequest::METHOD_GET, (), false, FavoriteDirectoryApi::handleGetGroupedDirectories);
		METHOD_HANDLER("directories_flat", Access::ANY, ApiRequest::METHOD_GET, (), false, FavoriteDirectoryApi::handleGetDirectories);

		METHOD_HANDLER("directory", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("add")), true, FavoriteDirectoryApi::handleAddDirectory);
		METHOD_HANDLER("directory", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("update")), true, FavoriteDirectoryApi::handleUpdateDirectory);
		METHOD_HANDLER("directory", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("remove")), true, FavoriteDirectoryApi::handleRemoveDirectory);

		FavoriteManager::getInstance()->addListener(this);

		createSubscription("favorite_directories_updated");
	}

	FavoriteDirectoryApi::~FavoriteDirectoryApi() {
		FavoriteManager::getInstance()->removeListener(this);
	}

	api_return FavoriteDirectoryApi::handleGetGroupedDirectories(ApiRequest& aRequest) {
		auto ret = json::array();

		auto directories = FavoriteManager::getInstance()->getGroupedFavoriteDirs();
		for (const auto& vPath : directories) {
			ret.push_back({
				{ "name", vPath.first },
				{ "paths", vPath.second }
			});
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	api_return FavoriteDirectoryApi::handleGetDirectories(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeDirectories());
		return websocketpp::http::status_code::ok;
	}

	json FavoriteDirectoryApi::serializeDirectories() noexcept {
		auto ret = json::array();

		auto directories = FavoriteManager::getInstance()->getFavoriteDirs();
		for (const auto& vPath : directories) {
			ret.push_back({
				{ "name", vPath.second },
				{ "path", vPath.first }
			});
		}

		return ret;
	}

	api_return FavoriteDirectoryApi::handleSetDirectory(ApiRequest& aRequest, bool aExisting) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = Util::validatePath(JsonUtil::getField<string>("path", reqJson, false), true);

		auto hasDirectory = FavoriteManager::getInstance()->hasFavoriteDir(path);
		if (!hasDirectory && aExisting) {
			JsonUtil::throwError("path", JsonUtil::ERROR_INVALID, "Path doesn't exist");
		} else if (hasDirectory && !aExisting) {
			JsonUtil::throwError("path", JsonUtil::ERROR_EXISTS, "Path exists already");
		}

		auto groupName = JsonUtil::getField<string>("name", reqJson, false);

		FavoriteManager::getInstance()->setFavoriteDir(path, groupName);

		return websocketpp::http::status_code::ok;
	}

	api_return FavoriteDirectoryApi::handleAddDirectory(ApiRequest& aRequest) {
		return handleSetDirectory(aRequest, false);
	}

	api_return FavoriteDirectoryApi::handleUpdateDirectory(ApiRequest& aRequest) {
		return handleSetDirectory(aRequest, true);
	}

	api_return FavoriteDirectoryApi::handleRemoveDirectory(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = JsonUtil::getField<string>("path", reqJson, false);
		if (!FavoriteManager::getInstance()->removeFavoriteDir(path)) {
			aRequest.setResponseErrorStr("Path not found");
			return websocketpp::http::status_code::not_found;
		}

		return websocketpp::http::status_code::ok;
	}

	void FavoriteDirectoryApi::on(FavoriteManagerListener::FavoriteDirectoriesUpdated) noexcept {
		maybeSend("favorite_directories_updated", [&] { return serializeDirectories(); });
	}
}