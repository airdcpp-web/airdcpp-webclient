/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/HubEntry.h>
#include <airdcpp/SearchResult.h>
#include <airdcpp/ShareManager.h>

namespace webserver {
	ShareApi::ShareApi(Session* aSession) : SubscribableApiModule(aSession, Access::SETTINGS_VIEW) {

		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("grouped_root_paths")),				ShareApi::handleGetGroupedRootPaths);
		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("stats")),								ShareApi::handleGetStats);
		METHOD_HANDLER(Access::ANY,				METHOD_POST,	(EXACT_PARAM("find_dupe_paths")),					ShareApi::handleFindDupePaths);
		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_POST,	(EXACT_PARAM("search")),							ShareApi::handleSearch);

		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("refresh")),							ShareApi::handleRefreshShare);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("refresh"), EXACT_PARAM("paths")),		ShareApi::handleRefreshPaths);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("refresh"), EXACT_PARAM("virtual")),	ShareApi::handleRefreshVirtual);

		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_GET,		(EXACT_PARAM("excludes")),							ShareApi::handleGetExcludes);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("excludes"), EXACT_PARAM("add")),		ShareApi::handleAddExclude);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("excludes"), EXACT_PARAM("remove")),	ShareApi::handleRemoveExclude);

		createSubscription("share_refresh_queued");
		createSubscription("share_refresh_completed");

		createSubscription("share_exclude_added");
		createSubscription("share_exclude_removed");

		ShareManager::getInstance()->addListener(this);
	}

	ShareApi::~ShareApi() {
		ShareManager::getInstance()->removeListener(this);
	}

	json ShareApi::serializeShareItem(const SearchResultPtr& aSR) noexcept {
		auto isDirectory = aSR->getType() == SearchResult::TYPE_DIRECTORY;
		auto path = Util::toAdcFile(aSR->getPath());

		StringList realPaths;
		try {
			ShareManager::getInstance()->getRealPaths(path, realPaths);
		} catch (const ShareException&) {
			dcassert(0);
		}

		return {
			{ "id", aSR->getId() },
			{ "name", aSR->getFileName() },
			{ "virtual_path", path },
			{ "real_paths", realPaths },
			{ "time", aSR->getDate() },
			{ "type", isDirectory ? Serializer::serializeFolderType(aSR->getContentInfo()) : Serializer::serializeFileType(aSR->getPath()) },
			{ "size", aSR->getSize() },
			{ "tth", isDirectory ? Util::emptyString : aSR->getTTH().toBase32() },
		};
	}

	api_return ShareApi::handleSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		// Parse share profile and query
		auto profile = Deserializer::deserializeOptionalShareProfile(reqJson);
		auto s = FileSearchParser::parseSearch(reqJson, true, Util::toString(Util::rand()));

		// Search
		SearchResultList results;
		
		{
			unique_ptr<SearchQuery> matcher(SearchQuery::getSearch(s));
			try {
				ShareManager::getInstance()->adcSearch(results, *matcher, profile, CID(), s->path);
			} catch (...) {}
		}

		// Serialize results
		aRequest.setResponseBody(Serializer::serializeList(results, serializeShareItem));
		return websocketpp::http::status_code::ok;
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

		return websocketpp::http::status_code::no_content;
	}

	api_return ShareApi::handleRemoveExclude(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody(), false);
		if (!ShareManager::getInstance()->removeExcludedPath(path)) {
			aRequest.setResponseErrorStr("Excluded path was not found");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::no_content;
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
		return websocketpp::http::status_code::no_content;
	}

	api_return ShareApi::handleRefreshPaths(ApiRequest& aRequest) {
		auto paths = JsonUtil::getField<StringList>("paths", aRequest.getRequestBody(), false);

		auto ret = ShareManager::getInstance()->refreshPaths(paths);
		if (ret == ShareManager::RefreshResult::REFRESH_PATH_NOT_FOUND) {
			aRequest.setResponseErrorStr("Invalid paths were supplied");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::no_content;
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
		return websocketpp::http::status_code::no_content;
	}

	api_return ShareApi::handleGetStats(ApiRequest& aRequest) {
		auto optionalItemStats = ShareManager::getInstance()->getShareItemStats();
		if (!optionalItemStats) {
			return websocketpp::http::status_code::no_content;
		}

		auto itemStats = *optionalItemStats;
		auto searchStats = ShareManager::getInstance()->getSearchMatchingStats();

		json j = {
			{ "total_file_count", itemStats.totalFileCount },
			{ "total_directory_count", itemStats.totalDirectoryCount },
			{ "files_per_directory", itemStats.filesPerDirectory },
			{ "total_size", itemStats.totalSize },
			{ "unique_file_percentage", itemStats.uniqueFilePercentage },
			{ "unique_files", itemStats.uniqueFileCount },
			{ "average_file_age", itemStats.averageFileAge },
			{ "profile_count", itemStats.profileCount },
			{ "root_count", itemStats.rootDirectoryCount },

			{ "total_searches", searchStats.totalSearches },
			{ "total_searches_per_second", searchStats.totalSearchesPerSecond },
			{ "total_recursive_searches", searchStats.recursiveSearches },
			{ "unfiltered_recursive_searches_per_second", searchStats.unfilteredRecursiveSearchesPerSecond },
			{ "filtered_search_percentage", searchStats.filteredSearchPercentage },
			{ "unfiltered_recursive_match_percentage", searchStats.unfilteredRecursiveMatchPercentage },
			{ "average_search_token_count", searchStats.averageSearchTokenCount },
			{ "average_search_token_length", searchStats.averageSearchTokenLength },
			{ "auto_search_percentage", searchStats.autoSearchPercentage },
			{ "tth_search_percentage", searchStats.tthSearchPercentage },
			{ "average_match_ms", searchStats.averageSearchMatchMs },
		};

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleGetGroupedRootPaths(ApiRequest& aRequest) {
		auto roots = ShareManager::getInstance()->getGroupedDirectories();
		aRequest.setResponseBody(Serializer::serializeList(roots, Serializer::serializeGroupedPaths));
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

	void ShareApi::onShareRefreshed(const RefreshPathList& aRealPaths, uint8_t aTaskType, const string& aSubscription) noexcept {
		if (!subscriptionActive(aSubscription)) {
			return;
		}

		send(aSubscription, {
			{ "real_paths", aRealPaths },
			{ "type", refreshTypeToString(aTaskType) }
		});
	}

	void ShareApi::on(ShareManagerListener::RefreshQueued, uint8_t aTaskType, const RefreshPathList& aPaths) noexcept {
		onShareRefreshed(aPaths, aTaskType, "share_refresh_queued");
	}

	void ShareApi::on(ShareManagerListener::RefreshCompleted, uint8_t aTaskType, const RefreshPathList& aPaths) noexcept {
		onShareRefreshed(aPaths, aTaskType, "share_refresh_completed");
	}
}