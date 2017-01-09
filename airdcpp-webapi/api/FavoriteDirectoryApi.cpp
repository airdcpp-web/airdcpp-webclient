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
#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/FavoriteManager.h>

namespace webserver {
	FavoriteDirectoryApi::FavoriteDirectoryApi(Session* aSession) : SubscribableApiModule(aSession, Access::ANY) {
		// TODO: fix method naming in the next major version
		METHOD_HANDLER("directories", Access::ANY, ApiRequest::METHOD_GET, (), false, FavoriteDirectoryApi::handleGetGroupedDirectories);
		METHOD_HANDLER("directories_flat", Access::ANY, ApiRequest::METHOD_GET, (), false, FavoriteDirectoryApi::handleGetDirectories);

		METHOD_HANDLER("directory", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (), true, FavoriteDirectoryApi::handleAddDirectory);
		METHOD_HANDLER("directory", Access::ANY, ApiRequest::METHOD_GET, (TTH_PARAM), false, FavoriteDirectoryApi::handleGetDirectory);
		METHOD_HANDLER("directory", Access::SETTINGS_EDIT, ApiRequest::METHOD_PATCH, (TTH_PARAM), true, FavoriteDirectoryApi::handleUpdateDirectory);
		METHOD_HANDLER("directory", Access::SETTINGS_EDIT, ApiRequest::METHOD_DELETE, (TTH_PARAM), false, FavoriteDirectoryApi::handleRemoveDirectory);

		// DEPRECATED
		METHOD_HANDLER("directory", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("add")), true, FavoriteDirectoryApi::handleAddDirectory);
		METHOD_HANDLER("directory", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("update")), true, FavoriteDirectoryApi::handleUpdateDirectoryLegacy);
		METHOD_HANDLER("directory", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("remove")), true, FavoriteDirectoryApi::handleRemoveDirectoryLegacy);

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
		return Serializer::serializeList(FavoriteManager::getInstance()->getFavoriteDirs(), serializeDirectory);
	}

	json FavoriteDirectoryApi::serializeDirectory(const StringPair& aDirectory) noexcept {
		return {
			{ "id", AirUtil::getPathId(aDirectory.first).toBase32() },
			{ "name", aDirectory.second },
			{ "path", aDirectory.first }
		};
	}

	api_return FavoriteDirectoryApi::handleAddDirectory(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = Util::validatePath(JsonUtil::getField<string>("path", reqJson, false), true);
		if (FavoriteManager::getInstance()->hasFavoriteDir(path)) {
			JsonUtil::throwError("path", JsonUtil::ERROR_EXISTS, "Path exists already");
		}

		auto info = updatePath(path, reqJson);
		aRequest.setResponseBody(serializeDirectory(info));
		return websocketpp::http::status_code::no_content;
	}

	api_return FavoriteDirectoryApi::handleGetDirectory(ApiRequest& aRequest) {
		auto path = getPath(aRequest);

		auto info = FavoriteManager::getInstance()->getFavoriteDirectory(path);
		aRequest.setResponseBody(serializeDirectory(info));
		return websocketpp::http::status_code::ok;
	}

	api_return FavoriteDirectoryApi::handleUpdateDirectory(ApiRequest& aRequest) {
		auto path = getPath(aRequest);


		auto info = updatePath(path, aRequest.getRequestBody());
		aRequest.setResponseBody(serializeDirectory(info));
		return websocketpp::http::status_code::ok;
	}

	api_return FavoriteDirectoryApi::handleRemoveDirectory(ApiRequest& aRequest) {
		auto path = getPath(aRequest);
		FavoriteManager::getInstance()->removeFavoriteDir(path);
		return websocketpp::http::status_code::no_content;
	}

	string FavoriteDirectoryApi::getPath(const ApiRequest& aRequest) {
		auto tth = Deserializer::parseTTH(aRequest.getStringParam(0));
		auto dirs = FavoriteManager::getInstance()->getFavoriteDirs();
		auto p = boost::find_if(dirs | map_keys, [&](const string& aPath) {
			return AirUtil::getPathId(aPath) == tth;
		});

		if (p.base() == dirs.end()) {
			throw RequestException(websocketpp::http::status_code::not_found, "Directory not found");
		}

		return *p;
	}

	StringPair FavoriteDirectoryApi::updatePath(const string& aPath, const json& aRequestJson) {
		auto virtualName = JsonUtil::getOptionalFieldDefault<string>("name", aRequestJson, Util::getLastDir(aPath), false);
		FavoriteManager::getInstance()->setFavoriteDir(aPath, virtualName);
		return { aPath, virtualName };
	}

	void FavoriteDirectoryApi::on(FavoriteManagerListener::FavoriteDirectoriesUpdated) noexcept {
		maybeSend("favorite_directories_updated", [&] { return serializeDirectories(); });
	}




	api_return FavoriteDirectoryApi::handleUpdateDirectoryLegacy(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = JsonUtil::getField<string>("path", reqJson, false);
		if (!FavoriteManager::getInstance()->hasFavoriteDir(path)) {
			JsonUtil::throwError("path", JsonUtil::ERROR_INVALID, "Path doesn't exist");
		}

		updatePath(path, reqJson);
		return websocketpp::http::status_code::no_content;
	}

	api_return FavoriteDirectoryApi::handleRemoveDirectoryLegacy(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = JsonUtil::getField<string>("path", reqJson, false);
		if (!FavoriteManager::getInstance()->removeFavoriteDir(path)) {
			aRequest.setResponseErrorStr("Path not found");
			return websocketpp::http::status_code::not_found;
		}

		return websocketpp::http::status_code::no_content;
	}
}