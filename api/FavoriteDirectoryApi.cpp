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

#include <api/FavoriteDirectoryApi.h>
#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/favorites/FavoriteManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/util/ValueGenerator.h>

namespace webserver {
	FavoriteDirectoryApi::FavoriteDirectoryApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::ANY) 
	{
		createSubscriptions({ "favorite_directories_updated" });

		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("grouped_paths")),			FavoriteDirectoryApi::handleGetGroupedDirectories);
		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(),										FavoriteDirectoryApi::handleGetDirectories);

		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(),										FavoriteDirectoryApi::handleAddDirectory);
		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(TTH_PARAM),							FavoriteDirectoryApi::handleGetDirectory);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_PATCH,	(TTH_PARAM),							FavoriteDirectoryApi::handleUpdateDirectory);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(TTH_PARAM),							FavoriteDirectoryApi::handleRemoveDirectory);

		FavoriteManager::getInstance()->addListener(this);
	}

	FavoriteDirectoryApi::~FavoriteDirectoryApi() {
		FavoriteManager::getInstance()->removeListener(this);
	}

	api_return FavoriteDirectoryApi::handleGetGroupedDirectories(ApiRequest& aRequest) {
		auto directories = FavoriteManager::getInstance()->getGroupedFavoriteDirs();
		aRequest.setResponseBody(Serializer::serializeList(directories, Serializer::serializeGroupedPaths));
		return http_status::ok;
	}

	api_return FavoriteDirectoryApi::handleGetDirectories(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeDirectories());
		return http_status::ok;
	}

	json FavoriteDirectoryApi::serializeDirectories() noexcept {
		return Serializer::serializeList(FavoriteManager::getInstance()->getFavoriteDirs(), serializeDirectory);
	}

	json FavoriteDirectoryApi::serializeDirectory(const StringPair& aDirectory) noexcept {
		return {
			{ "id", ValueGenerator::generatePathId(aDirectory.first).toBase32() },
			{ "name", aDirectory.second },
			{ "path", aDirectory.first }
		};
	}

	api_return FavoriteDirectoryApi::handleAddDirectory(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = PathUtil::validateDirectoryPath(JsonUtil::getField<string>("path", reqJson, false));
		if (FavoriteManager::getInstance()->hasFavoriteDir(path)) {
			JsonUtil::throwError("path", JsonException::ERROR_EXISTS, "Path exists already");
		}

		auto info = updatePath(path, reqJson);
		aRequest.setResponseBody(serializeDirectory(info));
		return http_status::no_content;
	}

	api_return FavoriteDirectoryApi::handleGetDirectory(ApiRequest& aRequest) {
		auto path = getPath(aRequest);

		auto info = FavoriteManager::getInstance()->getFavoriteDirectory(path);
		aRequest.setResponseBody(serializeDirectory(info));
		return http_status::ok;
	}

	api_return FavoriteDirectoryApi::handleUpdateDirectory(ApiRequest& aRequest) {
		auto path = getPath(aRequest);

		auto info = updatePath(path, aRequest.getRequestBody());
		aRequest.setResponseBody(serializeDirectory(info));
		return http_status::ok;
	}

	api_return FavoriteDirectoryApi::handleRemoveDirectory(ApiRequest& aRequest) {
		auto path = getPath(aRequest);
		FavoriteManager::getInstance()->removeFavoriteDir(path);
		return http_status::no_content;
	}

	string FavoriteDirectoryApi::getPath(const ApiRequest& aRequest) {
		auto tth = aRequest.getTTHParam();
		auto dirs = FavoriteManager::getInstance()->getFavoriteDirs();
		auto p = ranges::find_if(dirs | views::keys, [&](const string& aPath) {
			return ValueGenerator::generatePathId(aPath) == tth;
		});

		if (p.base() == dirs.end()) {
			throw RequestException(http_status::not_found, "Favorite directory " + tth.toBase32() + " was not found");
		}

		return *p;
	}

	StringPair FavoriteDirectoryApi::updatePath(const string& aPath, const json& aRequestJson) {
		auto virtualName = JsonUtil::getOptionalFieldDefault<string>("name", aRequestJson, PathUtil::getLastDir(aPath));
		FavoriteManager::getInstance()->setFavoriteDir(aPath, virtualName);
		return { aPath, virtualName };
	}

	void FavoriteDirectoryApi::on(FavoriteManagerListener::FavoriteDirectoriesUpdated) noexcept {
		maybeSend("favorite_directories_updated", [&] { return serializeDirectories(); });
	}
}