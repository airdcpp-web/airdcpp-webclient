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

#include <api/ShareApi.h>
#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>
#include <api/ShareUtils.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ShareManager.h>
#include <airdcpp/HubEntry.h>

namespace webserver {
	ShareApi::ShareApi(Session* aSession) : SubscribableApiModule(aSession, Access::SETTINGS_VIEW) {

		METHOD_HANDLER("grouped_root_paths", Access::ANY, ApiRequest::METHOD_GET, (), false, ShareApi::handleGetGroupedRootPaths);
		METHOD_HANDLER("stats", Access::ANY, ApiRequest::METHOD_GET, (), false, ShareApi::handleGetStats);
		METHOD_HANDLER("find_dupe_paths", Access::ANY, ApiRequest::METHOD_POST, (), true, ShareApi::handleFindDupePaths);

		METHOD_HANDLER("refresh", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (), false, ShareApi::handleRefreshShare);
		METHOD_HANDLER("refresh", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("paths")), true, ShareApi::handleRefreshPaths);
		METHOD_HANDLER("refresh", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("virtual")), true, ShareApi::handleRefreshVirtual);

		METHOD_HANDLER("excludes", Access::SETTINGS_VIEW, ApiRequest::METHOD_GET, (), false, ShareApi::handleGetExcludes);
		METHOD_HANDLER("exclude", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("add")), true, ShareApi::handleAddExclude);
		METHOD_HANDLER("exclude", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("remove")), true, ShareApi::handleRemoveExclude);

		createSubscription("share_refreshed");

		createSubscription("share_exclude_added");
		createSubscription("share_exclude_removed");

		ShareManager::getInstance()->addListener(this);
	}

	ShareApi::~ShareApi() {
		ShareManager::getInstance()->removeListener(this);
	}

	api_return ShareApi::handleGetExcludes(ApiRequest& aRequest) {
		aRequest.setResponseBody(ShareManager::getInstance()->getExcludedPaths());
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleAddExclude(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody(), false);

		try {
			ShareManager::getInstance()->addExcludedPath(path);
		} catch (const ShareException& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleRemoveExclude(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody(), false);
		if (!ShareManager::getInstance()->removeExcludedPath(path)) {
			aRequest.setResponseErrorStr("Excluded path was not found");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
	}

	void ShareApi::on(ShareManagerListener::ExcludeAdded, const string& aPath) noexcept {
		send("share_exclude_added", {
			{ "path", aPath }
		});
	}

	void ShareApi::on(ShareManagerListener::ExcludeRemoved, const string& aPath) noexcept {
		send("share_exclude_removed", {
			{ "path", aPath }
		});
	}

	api_return ShareApi::handleRefreshShare(ApiRequest& aRequest) {
		auto incoming = JsonUtil::getOptionalFieldDefault<bool>("incoming", aRequest.getRequestBody(), false);
		ShareManager::getInstance()->refresh(incoming);

		//aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleRefreshPaths(ApiRequest& aRequest) {
		auto paths = JsonUtil::getField<StringList>("paths", aRequest.getRequestBody(), false);

		auto ret = ShareManager::getInstance()->refreshPaths(paths);
		if (ret == ShareManager::RefreshResult::REFRESH_PATH_NOT_FOUND) {
			aRequest.setResponseErrorStr("Invalid paths were supplied");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleRefreshVirtual(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody(), false);

		StringList refreshPaths;
		try {
			ShareManager::getInstance()->getRealPaths(path, refreshPaths);
		} catch (const ShareException& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::bad_request;
		}

		ShareManager::getInstance()->refreshPaths(refreshPaths);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleGetStats(ApiRequest& aRequest) {
		auto optionalStats = ShareManager::getInstance()->getShareStats();
		if (!optionalStats) {
			return websocketpp::http::status_code::no_content;
		}

		auto stats = *optionalStats;

		json j = {
			{ "total_file_count", stats.totalFileCount },
			{ "total_directory_count", stats.totalDirectoryCount },
			{ "files_per_directory", stats.filesPerDirectory },
			{ "total_size", stats.totalSize },
			{ "unique_file_percentage", stats.uniqueFilePercentage },
			{ "unique_files", stats.uniqueFileCount },
			{ "average_file_age", stats.averageFileAge },
			{ "profile_count", stats.profileCount },
			{ "profile_root_count", stats.profileDirectoryCount},
		};

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleGetGroupedRootPaths(ApiRequest& aRequest) {
		auto ret = json::array();

		auto roots = ShareManager::getInstance()->getGroupedDirectories();
		for (const auto& vPath : roots) {
			ret.push_back({
				{ "name", vPath.first },
				{ "paths", vPath.second }
			});
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleFindDupePaths(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto ret = json::array();

		auto path = JsonUtil::getOptionalField<string>("path", reqJson, false, false);
		if (path) {
			ret = ShareManager::getInstance()->getNmdcDirPaths(Util::toNmdcFile(*path));
		} else {
			auto tth = Deserializer::deserializeTTH(reqJson);
			ret = ShareManager::getInstance()->getRealPaths(tth);
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	string ShareApi::refreshTypeToString(uint8_t aTaskType) noexcept {
		switch (aTaskType) {
			case ShareManager::ADD_DIR: return "add_directory";
			case ShareManager::REFRESH_ALL: return "refresh_all";
			case ShareManager::REFRESH_DIRS: return "refresh_directories";
			case ShareManager::REFRESH_INCOMING: return "refresh_incoming";
			case ShareManager::ADD_BUNDLE: return "add_bundle";
		}

		dcassert(0);
		return Util::emptyString;
	}

	void ShareApi::onShareRefreshed(const RefreshPathList& aRealPaths, uint8_t aTaskType) noexcept {
		if (!subscriptionActive("share_refreshed")) {
			return;
		}

		send("share_refreshed", {
			{ "real_paths", aRealPaths },
			{ "type", refreshTypeToString(aTaskType) }
		});
	}

	void ShareApi::on(ShareManagerListener::DirectoriesRefreshed, uint8_t aTaskType, const RefreshPathList& aPaths) noexcept {
		onShareRefreshed(aPaths, aTaskType);
	}
}