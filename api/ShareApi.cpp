/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#include "stdinc.h"

#include <api/ShareApi.h>

#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ClientManager.h>
#include <airdcpp/HashManager.h>
#include <airdcpp/HubEntry.h>
#include <airdcpp/Magnet.h>
#include <airdcpp/SearchResult.h>
#include <airdcpp/ShareManager.h>
#include <airdcpp/SharePathValidator.h>

namespace webserver {
	ShareApi::ShareApi(Session* aSession) : 
		HookApiModule(
			aSession, 
			Access::SETTINGS_VIEW, 
			{
				"share_refresh_queued",
				"share_refresh_started",
				"share_refresh_completed",
				
				"share_exclude_added",
				"share_exclude_removed",

				"share_temp_item_added",
				"share_temp_item_removed",
			},
			Access::SETTINGS_EDIT
		) 
	{
		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("grouped_root_paths")),				ShareApi::handleGetGroupedRootPaths);
		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_GET,		(EXACT_PARAM("stats")),								ShareApi::handleGetStats);
		METHOD_HANDLER(Access::ANY,				METHOD_POST,	(EXACT_PARAM("find_dupe_paths")),					ShareApi::handleFindDupePaths);
		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_POST,	(EXACT_PARAM("search")),							ShareApi::handleSearch);
		METHOD_HANDLER(Access::ANY,				METHOD_POST,	(EXACT_PARAM("validate_path")),						ShareApi::handleValidatePath);

		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("refresh")),							ShareApi::handleRefreshShare);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(EXACT_PARAM("refresh")),							ShareApi::handleAbortRefreshShare);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("refresh"), EXACT_PARAM("paths")),		ShareApi::handleRefreshPaths);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("refresh"), EXACT_PARAM("virtual")),	ShareApi::handleRefreshVirtual);

		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_GET,		(EXACT_PARAM("refresh"), EXACT_PARAM("tasks")),					ShareApi::handleGetRefreshTasks);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(EXACT_PARAM("refresh"), EXACT_PARAM("tasks"), TOKEN_PARAM),	ShareApi::handleAbortRefreshTask);

		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_GET,		(EXACT_PARAM("excludes")),							ShareApi::handleGetExcludes);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("excludes"), EXACT_PARAM("add")),		ShareApi::handleAddExclude);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("excludes"), EXACT_PARAM("remove")),	ShareApi::handleRemoveExclude);

		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_GET,		(EXACT_PARAM("temp_shares")),						ShareApi::handleGetTempShares);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("temp_shares")),						ShareApi::handleAddTempShare);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(EXACT_PARAM("temp_shares"), TOKEN_PARAM),			ShareApi::handleRemoveTempShare);

		createHook("share_file_validation_hook", [this](ActionHookSubscriber&& aSubscriber) {
			return ShareManager::getInstance()->getValidator().fileValidationHook.addSubscriber(std::move(aSubscriber), HOOK_HANDLER(ShareApi::fileValidationHook));
		}, [this](const string& aId) {
			ShareManager::getInstance()->getValidator().fileValidationHook.removeSubscriber(aId);
		});

		createHook("share_directory_validation_hook", [this](ActionHookSubscriber&& aSubscriber) {
			return ShareManager::getInstance()->getValidator().directoryValidationHook.addSubscriber(std::move(aSubscriber), HOOK_HANDLER(ShareApi::directoryValidationHook));
		}, [this](const string& aId) {
			ShareManager::getInstance()->getValidator().directoryValidationHook.removeSubscriber(aId);
		});

		createHook("new_share_directory_validation_hook", [this](ActionHookSubscriber&& aSubscriber) {
			return ShareManager::getInstance()->getValidator().newDirectoryValidationHook.addSubscriber(std::move(aSubscriber), HOOK_HANDLER(ShareApi::newDirectoryValidationHook));
		}, [this](const string& aId) {
			ShareManager::getInstance()->getValidator().newDirectoryValidationHook.removeSubscriber(aId);
		});

		createHook("new_share_file_validation_hook", [this](ActionHookSubscriber&& aSubscriber) {
			return ShareManager::getInstance()->getValidator().newFileValidationHook.addSubscriber(std::move(aSubscriber), HOOK_HANDLER(ShareApi::newFileValidationHook));
		}, [this](const string& aId) {
			ShareManager::getInstance()->getValidator().newFileValidationHook.removeSubscriber(aId);
		});

		ShareManager::getInstance()->addListener(this);
	}

	ShareApi::~ShareApi() {
		ShareManager::getInstance()->removeListener(this);
	}

	ActionHookResult<> ShareApi::fileValidationHook(const string& aPath, int64_t aSize, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			fireHook("share_file_validation_hook", 30, [&]() {
				return json({
					{ "path", aPath },
					{ "size", aSize },
				});
			}),
			aResultGetter
		);
	}

	ActionHookResult<> ShareApi::directoryValidationHook(const string& aPath, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			fireHook("share_directory_validation_hook", 30, [&]() {
				return json({
					{ "path", aPath },
				});
			}),
			aResultGetter
		);
	}

	ActionHookResult<> ShareApi::newDirectoryValidationHook(const string& aPath, bool aNewParent, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			fireHook("new_share_directory_validation_hook", 60, [&]() {
				return json({
					{ "path", aPath },
					{ "new_parent", aNewParent },
				});
			}),
			aResultGetter
		);
	}


	ActionHookResult<> ShareApi::newFileValidationHook(const string& aPath, int64_t aSize, bool aNewParent, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			fireHook("new_share_file_validation_hook", 60, [&]() {
				return json({
					{ "path", aPath },
					{ "size", aSize },
					{ "new_parent", aNewParent },
				});
			}),
			aResultGetter
		);
	}

	json ShareApi::serializeShareItem(const SearchResultPtr& aSR) noexcept {
		auto isDirectory = aSR->getType() == SearchResult::TYPE_DIRECTORY;
		auto path = aSR->getAdcPath();

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
			{ "type", isDirectory ? Serializer::serializeFolderType(aSR->getContentInfo()) : Serializer::serializeFileType(aSR->getAdcPath()) },
			{ "size", aSR->getSize() },
			{ "tth", isDirectory ? Util::emptyString : aSR->getTTH().toBase32() },
		};
	}

	json ShareApi::serializeRefreshQueueInfo(const ShareManager::RefreshTaskQueueInfo& aRefreshQueueInfo) noexcept {
		return {
			{ "task", !aRefreshQueueInfo.token ? JsonUtil::emptyJson : json({
				{ "id", json(*aRefreshQueueInfo.token) },
			}) },
			{ "result", refreshResultToString(aRefreshQueueInfo.result) },
		};
	}


	json ShareApi::serializeRefreshTask(const ShareRefreshTask& aRefreshTask) noexcept {
		return {
			{ "id", aRefreshTask.token },
			{ "real_paths", aRefreshTask.dirs },
			{ "type", refreshTypeToString(aRefreshTask.type) },
			{ "canceled", aRefreshTask.canceled },
			{ "running", aRefreshTask.running },
			{ "priority_type", refreshPriorityToString(aRefreshTask.priority) },
		};
	}

	string ShareApi::refreshResultToString(ShareManager::RefreshTaskQueueResult aRefreshQueueResult) noexcept {
		switch (aRefreshQueueResult) {
			case ShareManager::RefreshTaskQueueResult::EXISTS: return "exists";
			case ShareManager::RefreshTaskQueueResult::QUEUED: return "queued";
			case ShareManager::RefreshTaskQueueResult::STARTED: return "started";
		}

		dcassert(0);
		return Util::emptyString;
	}

	string ShareApi::refreshPriorityToString(ShareRefreshPriority aPriority) noexcept {
		switch (aPriority) {
			case ShareRefreshPriority::BLOCKING:
			case ShareRefreshPriority::NORMAL: return "normal";
			case ShareRefreshPriority::MANUAL: return "manual";
			case ShareRefreshPriority::SCHEDULED: return "scheduled";
		}

		dcassert(0);
		return Util::emptyString;
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

	api_return ShareApi::handleAddTempShare(ApiRequest& aRequest) {
		const auto fileId = JsonUtil::getField<string>("file_id", aRequest.getRequestBody(), false);
		const auto name = JsonUtil::getField<string>("name", aRequest.getRequestBody(), false);
		const auto user = Deserializer::deserializeUser(aRequest.getRequestBody(), false, true);
		const auto client = Deserializer::deserializeClient(aRequest.getRequestBody());

		const auto filePath = aRequest.getSession()->getServer()->getFileServer().getTempFilePath(fileId);
		if (filePath.empty() || !Util::fileExists(filePath)) {
			aRequest.setResponseErrorStr("File with an ID " + fileId + " was not found");
			return websocketpp::http::status_code::bad_request;
		}

		const auto size = File::getSize(filePath);
		TTHValue tth;

		{
			int64_t sizeLeft = 0;
			bool cancelHashing = false;

			// Calculate TTH
			try {
				HashManager::getInstance()->getFileTTH(filePath, size, true, tth, sizeLeft, cancelHashing);
			} catch (const Exception& e) {
				aRequest.setResponseErrorStr("Failed to calculate file TTH: " + e.getError());
				return websocketpp::http::status_code::internal_server_error;
			}
		}

		auto item = ShareManager::getInstance()->addTempShare(tth, name, filePath, size, client->get(HubSettings::ShareProfile), user);

		aRequest.setResponseBody({
			{ "magnet", Magnet::makeMagnet(tth, name, size) },
			{ "item", !item ? json() : serializeTempShare(*item) }
		});

		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleRemoveTempShare(ApiRequest& aRequest) {
		auto token = aRequest.getTokenParam();
		if (!ShareManager::getInstance()->removeTempShare(token)) {
			aRequest.setResponseErrorStr("Temp share was not found");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::no_content;
	}

	json ShareApi::serializeTempShare(const TempShareInfo& aInfo) noexcept {
		return {
			{ "id", aInfo.id },
			{ "name", aInfo.name },
			{ "path", aInfo.path },
			{ "size", aInfo.size },
			{ "tth", aInfo.tth.toBase32() },
			{ "time_added", aInfo.timeAdded },
			{ "type", Serializer::serializeFileType(aInfo.name) },
			{ "user", aInfo.user ? Serializer::serializeUser(aInfo.user) : json() }
		};
	}

	api_return ShareApi::handleGetTempShares(ApiRequest& aRequest) {
		const auto tempShares = ShareManager::getInstance()->getTempShares();

		aRequest.setResponseBody(Serializer::serializeList(tempShares, serializeTempShare));
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


	void ShareApi::on(ShareManagerListener::TempFileAdded, const TempShareInfo& aFile) noexcept {
		maybeSend("share_temp_item_added", [&] {
			return serializeTempShare(aFile);
		});
	}

	void ShareApi::on(ShareManagerListener::TempFileRemoved, const TempShareInfo& aFile) noexcept {
		maybeSend("share_temp_item_removed", [&] {
			return serializeTempShare(aFile);
		});
	}

	api_return ShareApi::handleAbortRefreshShare(ApiRequest& aRequest) {
		ShareManager::getInstance()->abortRefresh();
		return websocketpp::http::status_code::no_content;
	}

	api_return ShareApi::handleGetRefreshTasks(ApiRequest& aRequest) {
		auto tasks = ShareManager::getInstance()->getRefreshTasks();
		aRequest.setResponseBody(Serializer::serializeList(tasks, serializeRefreshTask));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleAbortRefreshTask(ApiRequest& aRequest) {
		const auto token = aRequest.getTokenParam();
		if (!ShareManager::getInstance()->abortRefresh(token)) {
			aRequest.setResponseErrorStr("Refresh task was not found");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::no_content;
	}

	api_return ShareApi::handleRefreshShare(ApiRequest& aRequest) {
		auto incoming = JsonUtil::getOptionalFieldDefault<bool>("incoming", aRequest.getRequestBody(), false);
		auto priority = parseRefreshPriority(aRequest.getRequestBody());

		auto refreshInfo = ShareManager::getInstance()->refresh(incoming ? ShareRefreshType::REFRESH_INCOMING : ShareRefreshType::REFRESH_ALL, priority);
		aRequest.setResponseBody(serializeRefreshQueueInfo(refreshInfo));

		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleRefreshPaths(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		addAsyncTask([
			paths = JsonUtil::getField<StringList>("paths", reqJson, false),
			priority = parseRefreshPriority(reqJson),
			complete = aRequest.defer(),
			callerPtr = aRequest.getOwnerPtr()
		] {
			try {
				auto refreshInfo = ShareManager::getInstance()->refreshPathsHookedThrow(priority, paths, callerPtr);
				complete(websocketpp::http::status_code::ok, serializeRefreshQueueInfo(refreshInfo), nullptr);
			} catch (const Exception& e) {
				complete(websocketpp::http::status_code::bad_request, nullptr, e.getError());
			}
		});

		return CODE_DEFERRED;
	}

	api_return ShareApi::handleRefreshVirtual(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		addAsyncTask([
			virtualPath = JsonUtil::getField<string>("path", reqJson, false),
			priority = parseRefreshPriority(reqJson),
			complete = aRequest.defer(),
			callerPtr = aRequest.getOwnerPtr()
		] {
			StringList refreshPaths;
			try {
				ShareManager::getInstance()->getRealPaths(virtualPath, refreshPaths);

				auto refreshInfo = ShareManager::getInstance()->refreshPathsHookedThrow(priority, refreshPaths, callerPtr);
				complete(websocketpp::http::status_code::ok, serializeRefreshQueueInfo(refreshInfo), nullptr);
			} catch (const ShareException& e) {
				complete(websocketpp::http::status_code::bad_request, nullptr, e.getError());
			}
		});

		return CODE_DEFERRED;
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
			{ "total_size", itemStats.totalSize },
			{ "unique_file_count", itemStats.uniqueFileCount },
			{ "average_file_age", itemStats.averageFileAge },
			{ "profile_count", itemStats.profileCount },
			{ "root_count", itemStats.rootDirectoryCount },

			{ "total_searches", searchStats.totalSearches },
			{ "total_searches_per_second", searchStats.totalSearchesPerSecond },

			{ "auto_searches", searchStats.autoSearches },
			{ "tth_searches", searchStats.tthSearches },

			{ "unfiltered_recursive_searches_per_second", searchStats.unfilteredRecursiveSearchesPerSecond },
			{ "filtered_searches", searchStats.filteredSearches },

			{ "recursive_searches", searchStats.recursiveSearches },
			{ "recursive_searches_responded", searchStats.recursiveSearchesResponded },
			{ "average_match_ms", searchStats.averageSearchMatchMs },

			{ "average_search_token_count", searchStats.averageSearchTokenCount },
			{ "average_search_token_length", searchStats.averageSearchTokenLength },
		};

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleGetGroupedRootPaths(ApiRequest& aRequest) {
		auto roots = ShareManager::getInstance()->getGroupedDirectories();
		aRequest.setResponseBody(Serializer::serializeList(roots, Serializer::serializeGroupedPaths));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleValidatePath(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		addAsyncTask([
			path = JsonUtil::getField<string>("path", reqJson),
			skipCheckQueue = JsonUtil::getOptionalFieldDefault<bool>("skip_check_queue", reqJson, false),
			complete = aRequest.defer(),
			callerPtr = aRequest.getOwnerPtr()
		] {
			try {
				ShareManager::getInstance()->validatePathHooked(path, skipCheckQueue, callerPtr);
			} catch (const QueueException& e) {
				// Queued bundle
				complete(websocketpp::http::status_code::conflict, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
				return;
			} catch (const ShareValidatorException& e) {
				// Validation error
				complete(websocketpp::http::status_code::forbidden, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
				return;
			} catch (const ShareException& e) {
				// Path not inside a shared directory
				complete(websocketpp::http::status_code::expectation_failed, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
				return;
			} catch (const FileException& e) {
				// File doesn't exist
				complete(websocketpp::http::status_code::not_found, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
				return;
			}

			complete(websocketpp::http::status_code::no_content, nullptr, nullptr);
		});

		return CODE_DEFERRED;
	}

	api_return ShareApi::handleFindDupePaths(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto ret = json::array();

		auto path = JsonUtil::getOptionalField<string>("path", reqJson);
		if (path) {
			ret = ShareManager::getInstance()->getAdcDirectoryPaths(*path);
		} else {
			auto tth = Deserializer::deserializeTTH(reqJson);
			ret = ShareManager::getInstance()->getRealPaths(tth);
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	string ShareApi::refreshTypeToString(ShareRefreshType aType) noexcept {
		switch (aType) {
			case ShareRefreshType::ADD_DIR: return "add_directory";
			case ShareRefreshType::STARTUP:
			case ShareRefreshType::REFRESH_ALL: return "refresh_all";
			case ShareRefreshType::REFRESH_DIRS: return "refresh_directories";
			case ShareRefreshType::REFRESH_INCOMING: return "refresh_incoming";
			case ShareRefreshType::BUNDLE: return "add_bundle";
		}

		dcassert(0);
		return Util::emptyString;
	}

	ShareRefreshPriority ShareApi::parseRefreshPriority(const json& aJson) {
		auto priority = JsonUtil::getOptionalFieldDefault<string>("priority_type", aJson, "normal");
		if (priority == "normal") {
			return ShareRefreshPriority::NORMAL;
		} else if (priority == "scheduled") {
			return ShareRefreshPriority::SCHEDULED;
		} else if (priority == "manual") {
			return ShareRefreshPriority::MANUAL;
		}

		JsonUtil::throwError("priority_type", JsonUtil::ERROR_INVALID, "Refresh priority " + priority + "doesn't exist");
		return ShareRefreshPriority::NORMAL;
	}

	void ShareApi::on(ShareManagerListener::RefreshQueued, const ShareRefreshTask& aTask) noexcept {
		maybeSend("share_refresh_queued", [&] {
			return json({
				{ "task", serializeRefreshTask(aTask) },

				{ "real_paths", aTask.dirs }, // DEPRECATED
				{ "type", refreshTypeToString(aTask.type) }, // DEPRECATED
			});
		});
	}

	void ShareApi::on(ShareManagerListener::RefreshStarted, const ShareRefreshTask& aTask) noexcept {
		maybeSend("share_refresh_started", [&] {
			return json({
				{ "task", serializeRefreshTask(aTask) },
			});
		});
	}

	void ShareApi::on(ShareManagerListener::RefreshCompleted, const ShareRefreshTask& aTask, bool aSucceed, int64_t aTotalHash) noexcept {
		maybeSend("share_refresh_completed", [&] {
			return json({
				{ "task", serializeRefreshTask(aTask) },
				{ "hash_bytes_queued", aTotalHash },

				{ "real_paths", aTask.dirs }, // DEPRECATED 
				{ "type", refreshTypeToString(aTask.type) }, // DEPRECATED
			});
		});
	}
}