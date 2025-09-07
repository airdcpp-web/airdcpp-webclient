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

#include <api/ShareApi.h>

#include <web-server/HttpManager.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>
#include <api/common/Serializer.h>
#include <api/common/Validation.h>

#include <web-server/JsonUtil.h>
#include <web-server/WebServerSettings.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/hash/HashManager.h>
#include <airdcpp/favorites/HubEntry.h>
#include <airdcpp/core/classes/Magnet.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/search/SearchQuery.h>
#include <airdcpp/search/SearchResult.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/share/SharePathValidator.h>
#include <airdcpp/util/text/StringTokenizer.h>
#include <airdcpp/share/temp_share/TempShareManager.h>


#define HOOK_FILE_VALIDATION "share_file_validation_hook"
#define HOOK_DIRECTORY_VALIDATION "share_directory_validation_hook"

#define HOOK_NEW_FILE_VALIDATION "new_share_file_validation_hook"
#define HOOK_NEW_DIRECTORY_VALIDATION "new_share_directory_validation_hook"

namespace webserver {
	ShareApi::ShareApi(Session* aSession) : HookApiModule(aSession, Access::SHARE_VIEW, Access::SHARE_EDIT) {
		createSubscriptions({
			"share_refresh_queued",
			"share_refresh_started",
			"share_refresh_completed",

			"share_exclude_added",
			"share_exclude_removed",

			"share_temp_item_added",
			"share_temp_item_removed",
		});

		// Methods
		METHOD_HANDLER(Access::ANY,			METHOD_GET,		(EXACT_PARAM("grouped_root_paths")),				ShareApi::handleGetGroupedRootPaths);
		METHOD_HANDLER(Access::SHARE_VIEW,	METHOD_GET,		(EXACT_PARAM("stats")),								ShareApi::handleGetStats);
		METHOD_HANDLER(Access::ANY,			METHOD_POST,	(EXACT_PARAM("find_dupe_paths")),					ShareApi::handleFindDupePaths);
		METHOD_HANDLER(Access::SHARE_VIEW,	METHOD_POST,	(EXACT_PARAM("search")),							ShareApi::handleSearch);
		METHOD_HANDLER(Access::ANY,			METHOD_POST,	(EXACT_PARAM("validate_path")),						ShareApi::handleValidatePath);
		METHOD_HANDLER(Access::ANY,			METHOD_POST,	(EXACT_PARAM("check_path_shared")),					ShareApi::handleIsPathShared);

		METHOD_HANDLER(Access::SHARE_VIEW,	METHOD_POST,	(EXACT_PARAM("directories"), EXACT_PARAM("by_real"), EXACT_PARAM("content"), RANGE_START_PARAM, RANGE_MAX_PARAM),	ShareApi::handleGetDirectoryContentByReal);
		METHOD_HANDLER(Access::SHARE_VIEW,	METHOD_POST,	(EXACT_PARAM("directories"), EXACT_PARAM("by_real")),		ShareApi::handleGetDirectoryByReal);
		METHOD_HANDLER(Access::SHARE_VIEW,	METHOD_POST,	(EXACT_PARAM("files"), EXACT_PARAM("by_real")),				ShareApi::handleGetFileByReal);
		METHOD_HANDLER(Access::SHARE_VIEW,	METHOD_GET,		(EXACT_PARAM("files"), TTH_PARAM),							ShareApi::handleGetFilesByTTH);


		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_POST,	(EXACT_PARAM("refresh")),							ShareApi::handleRefreshShare);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_DELETE,	(EXACT_PARAM("refresh")),							ShareApi::handleAbortRefreshShare);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_POST,	(EXACT_PARAM("refresh"), EXACT_PARAM("paths")),		ShareApi::handleRefreshPaths);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_POST,	(EXACT_PARAM("refresh"), EXACT_PARAM("virtual")),	ShareApi::handleRefreshVirtualPath);

		METHOD_HANDLER(Access::SHARE_VIEW,	METHOD_GET,		(EXACT_PARAM("refresh"), EXACT_PARAM("tasks")),					ShareApi::handleGetRefreshTasks);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_DELETE,	(EXACT_PARAM("refresh"), EXACT_PARAM("tasks"), TOKEN_PARAM),	ShareApi::handleAbortRefreshTask);

		METHOD_HANDLER(Access::SHARE_VIEW,	METHOD_GET,		(EXACT_PARAM("excludes")),							ShareApi::handleGetExcludes);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_POST,	(EXACT_PARAM("excludes"), EXACT_PARAM("add")),		ShareApi::handleAddExclude);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_POST,	(EXACT_PARAM("excludes"), EXACT_PARAM("remove")),	ShareApi::handleRemoveExclude);

		METHOD_HANDLER(Access::SHARE_VIEW,	METHOD_GET,		(EXACT_PARAM("temp_shares")),						ShareApi::handleGetTempShares);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_POST,	(EXACT_PARAM("temp_shares")),						ShareApi::handleAddTempShare);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_DELETE,	(EXACT_PARAM("temp_shares"), TOKEN_PARAM),			ShareApi::handleRemoveTempShare);

		// Hooks
		HOOK_HANDLER(HOOK_FILE_VALIDATION,			ShareManager::getInstance()->getValidator().fileValidationHook,			ShareApi::fileValidationHook);
		HOOK_HANDLER(HOOK_DIRECTORY_VALIDATION,		ShareManager::getInstance()->getValidator().directoryValidationHook,	ShareApi::directoryValidationHook);
		HOOK_HANDLER(HOOK_NEW_FILE_VALIDATION,		ShareManager::getInstance()->getValidator().newFileValidationHook,		ShareApi::newFileValidationHook);
		HOOK_HANDLER(HOOK_NEW_DIRECTORY_VALIDATION, ShareManager::getInstance()->getValidator().newDirectoryValidationHook, ShareApi::newDirectoryValidationHook);

		// Listeners
		ShareManager::getInstance()->addListener(this);
		TempShareManager::getInstance()->addListener(this);
	}

	ShareApi::~ShareApi() {
		ShareManager::getInstance()->removeListener(this);
		TempShareManager::getInstance()->removeListener(this);
	}

	ActionHookResult<> ShareApi::fileValidationHook(const string& aPath, int64_t aSize, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_FILE_VALIDATION, WEBCFG(SHARE_FILE_VALIDATION_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "path", aPath },
					{ "size", aSize },
				});
			}),
			aResultGetter,
			this
		);
	}

	ActionHookResult<> ShareApi::directoryValidationHook(const string& aPath, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_DIRECTORY_VALIDATION, WEBCFG(SHARE_DIRECTORY_VALIDATION_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "path", aPath },
				});
			}),
			aResultGetter,
			this
		);
	}

	ActionHookResult<> ShareApi::newFileValidationHook(const string& aPath, int64_t aSize, bool aNewParent, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_NEW_FILE_VALIDATION, WEBCFG(NEW_SHARE_FILE_VALIDATION_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "path", aPath },
					{ "size", aSize },
					{ "new_parent", aNewParent },
				});
			}),
			aResultGetter,
			this
		);
	}

	ActionHookResult<> ShareApi::newDirectoryValidationHook(const string& aPath, bool aNewParent, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_NEW_DIRECTORY_VALIDATION, WEBCFG(NEW_SHARE_DIRECTORY_VALIDATION_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "path", aPath },
					{ "new_parent", aNewParent },
					});
				}),
			aResultGetter,
			this
		);
	}

	json ShareApi::serializeShareItem(const ShareItem& aItem) noexcept {
		if (aItem.directory) {
			return serializeDirectory(aItem.directory);
		} else {
			return serializeFile(aItem.file);
		}
	}

	json ShareApi::serializeFile(const ShareDirectory::File* aFile) noexcept {
		auto realPath = aFile->getRealPath();
		return {
			{ "id", ValueGenerator::generatePathId(realPath) },
			{ "name", aFile->getName().getNormal() },
			{ "path", realPath },
			{ "virtual_path", aFile->getAdcPath() },
			{ "size", aFile->getSize() },
			{ "tth", aFile->getTTH().toBase32() },
			{ "time", aFile->getLastWrite() },
			{ "type", Serializer::serializeFileType(aFile->getName().getLower()) },
			{ "profiles", aFile->getParent()->getRootProfiles() },
		};
	}

	json ShareApi::serializeDirectory(const ShareDirectory::Ptr& aDirectory) noexcept {
		auto contentInfo(DirectoryContentInfo::empty());
		int64_t totalSize = 0;
		aDirectory->getContentInfo(totalSize, contentInfo);

		auto realPath = aDirectory->getRealPathUnsafe();
		return {
			{ "id", ValueGenerator::generatePathId(realPath) },
			{ "name", aDirectory->getRealName().getNormal() },
			{ "path", realPath },
			{ "virtual_path", aDirectory->getAdcPathUnsafe() },
			{ "size", totalSize },
			{ "tth", Util::emptyString },
			{ "time", aDirectory->getLastWrite() },
			{ "type", Serializer::serializeFolderType(contentInfo) },
			{ "profiles", aDirectory->getRootProfiles() },
		};
	}


	json ShareApi::serializeVirtualItem(const SearchResultPtr& aSR) noexcept {
		auto isDirectory = aSR->getType() == SearchResult::Type::DIRECTORY;
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

	json ShareApi::serializeRefreshQueueInfo(const RefreshTaskQueueInfo& aRefreshQueueInfo) noexcept {
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

	string ShareApi::refreshResultToString(RefreshTaskQueueResult aRefreshQueueResult) noexcept {
		switch (aRefreshQueueResult) {
			case RefreshTaskQueueResult::EXISTS: return "exists";
			case RefreshTaskQueueResult::QUEUED: return "queued";
			case RefreshTaskQueueResult::STARTED: return "started";
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
		auto s = FileSearchParser::parseSearch(reqJson, true);

		// Search
		SearchResultList results;
		
		{
			unique_ptr<SearchQuery> matcher(SearchQuery::fromSearch(s));
			ShareSearch search(*matcher, profile, nullptr, s->path);
			try {
				ShareManager::getInstance()->search(results, search);
			} catch (...) {}
		}

		// Serialize results
		aRequest.setResponseBody(Serializer::serializeList(results, serializeVirtualItem));
		return http_status::ok;
	}

	api_return ShareApi::handleGetFilesByTTH(ApiRequest& aRequest) {
		auto tth = aRequest.getTTHParam();

		const auto files = ShareManager::getInstance()->findFiles(tth);
		aRequest.setResponseBody(Serializer::serializeList(files, serializeFile));
		return http_status::ok;
	}

	api_return ShareApi::handleGetDirectoryContentByReal(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody(), false);
		auto start = aRequest.getRangeParam(START_POS);
		auto count = aRequest.getRangeParam(MAX_COUNT);
		if (!ShareManager::getInstance()->findDirectoryByRealPath(path, [&](const ShareDirectory::Ptr& aDirectory) {
			ShareItem::List items;
			for (const auto& d : aDirectory->getDirectories()) {
				items.emplace_back(d);
			}
			for (const auto& f : aDirectory->getFiles()) {
				items.emplace_back(f);
			}

			auto j = Serializer::serializeFromPosition(start, count, items, serializeShareItem);
			aRequest.setResponseBody(j);
		})) {
			JsonUtil::throwError("path", JsonException::ERROR_INVALID, "Path was not found");
		}

		return http_status::ok;
	}

	api_return ShareApi::handleGetFileByReal(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody(), false);
		if (!ShareManager::getInstance()->findFileByRealPath(path, [&](const ShareDirectory::File& aFile) {
			aRequest.setResponseBody(serializeFile(&aFile));
		})) {
			JsonUtil::throwError("path", JsonException::ERROR_INVALID, "Path was not found");
		}

		return http_status::ok;
	}

	api_return ShareApi::handleGetDirectoryByReal(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody(), false);
		if (!ShareManager::getInstance()->findDirectoryByRealPath(path, [&](const ShareDirectory::Ptr& aDirectory) {
			aRequest.setResponseBody(serializeDirectory(aDirectory));
		})) {
			JsonUtil::throwError("path", JsonException::ERROR_INVALID, "Path was not found");
		}

		return http_status::ok;
	}

	api_return ShareApi::handleAddTempShare(ApiRequest& aRequest) {
		const auto fileId = JsonUtil::getField<string>("file_id", aRequest.getRequestBody(), false);
		const auto name = JsonUtil::getField<string>("name", aRequest.getRequestBody(), false);
		const auto user = Deserializer::deserializeUser(aRequest.getRequestBody(), true, true);
		const auto optionalClient = Deserializer::deserializeClient(aRequest.getRequestBody(), true);

		const auto filePath = aRequest.getSession()->getServer()->getHttpManager().getFileServer().getTempFilePath(fileId);
		if (filePath.empty() || !PathUtil::fileExists(filePath)) {
			JsonUtil::throwError("file_id", JsonException::ERROR_INVALID, "Source file was not found");
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
				return http_status::internal_server_error;
			}
		}

		auto shareProfileToken = optionalClient ? optionalClient->get(HubSettings::ShareProfile) : SETTING(DEFAULT_SP);
		auto item = TempShareManager::getInstance()->addTempShare(tth, name, filePath, size, shareProfileToken, user);

		aRequest.setResponseBody({
			{ "magnet", Magnet::makeMagnet(tth, name, size) },
			{ "item", !item ? json() : serializeTempShare(*item) }
		});

		return http_status::ok;
	}

	api_return ShareApi::handleRemoveTempShare(ApiRequest& aRequest) {
		auto token = aRequest.getTokenParam();
		if (!TempShareManager::getInstance()->removeTempShare(token)) {
			aRequest.setResponseErrorStr("Temp share item " + Util::toString(token) + " was not found");
			return http_status::bad_request;
		}

		return http_status::no_content;
	}

	json ShareApi::serializeTempShare(const TempShareInfo& aInfo) noexcept {
		return {
			{ "id", aInfo.id },
			{ "name", aInfo.name },
			{ "path", aInfo.realPath },
			{ "size", aInfo.size },
			{ "tth", aInfo.tth.toBase32() },
			{ "time_added", aInfo.timeAdded },
			{ "type", Serializer::serializeFileType(aInfo.name) },
			{ "user", aInfo.user ? Serializer::serializeUser(aInfo.user) : json() }
		};
	}

	api_return ShareApi::handleGetTempShares(ApiRequest& aRequest) {
		const auto tempShares = TempShareManager::getInstance()->getTempShares();

		aRequest.setResponseBody(Serializer::serializeList(tempShares, serializeTempShare));
		return http_status::ok;
	}

	api_return ShareApi::handleGetExcludes(ApiRequest& aRequest) {
		aRequest.setResponseBody(ShareManager::getInstance()->getExcludedPaths());
		return http_status::ok;
	}

	api_return ShareApi::handleAddExclude(ApiRequest& aRequest) {
		auto path = PathUtil::validateDirectoryPath(JsonUtil::getField<string>("path", aRequest.getRequestBody(), false));

		try {
			ShareManager::getInstance()->addExcludedPath(path);
		} catch (const ShareException& e) {
			aRequest.setResponseErrorStr(e.getError());
			return http_status::bad_request;
		}

		return http_status::no_content;
	}

	api_return ShareApi::handleRemoveExclude(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody(), false);
		if (!ShareManager::getInstance()->removeExcludedPath(path)) {
			JsonUtil::throwError("path", JsonException::ERROR_INVALID, "Excluded path was not found");
		}

		return http_status::no_content;
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


	void ShareApi::on(TempShareManagerListener::TempFileAdded, const TempShareInfo& aFile) noexcept {
		maybeSend("share_temp_item_added", [&] {
			return serializeTempShare(aFile);
		});
	}

	void ShareApi::on(TempShareManagerListener::TempFileRemoved, const TempShareInfo& aFile) noexcept {
		maybeSend("share_temp_item_removed", [&] {
			return serializeTempShare(aFile);
		});
	}

	api_return ShareApi::handleAbortRefreshShare(ApiRequest& aRequest) {
		ShareManager::getInstance()->abortRefresh();
		return http_status::no_content;
	}

	api_return ShareApi::handleGetRefreshTasks(ApiRequest& aRequest) {
		auto tasks = ShareManager::getInstance()->getRefreshTasks();
		aRequest.setResponseBody(Serializer::serializeList(tasks, serializeRefreshTask));
		return http_status::ok;
	}

	api_return ShareApi::handleAbortRefreshTask(ApiRequest& aRequest) {
		const auto token = aRequest.getTokenParam();
		if (!ShareManager::getInstance()->abortRefresh(token)) {
			aRequest.setResponseErrorStr("Refresh task " + Util::toString(token) + " was not found");
			return http_status::bad_request;
		}

		return http_status::no_content;
	}

	api_return ShareApi::handleRefreshShare(ApiRequest& aRequest) {
		auto incoming = JsonUtil::getOptionalFieldDefault<bool>("incoming", aRequest.getRequestBody(), false);
		auto priority = parseRefreshPriority(aRequest.getRequestBody());

		auto refreshInfo = ShareManager::getInstance()->refresh(incoming ? ShareRefreshType::REFRESH_INCOMING : ShareRefreshType::REFRESH_ALL, priority);
		aRequest.setResponseBody(serializeRefreshQueueInfo(refreshInfo));

		return http_status::ok;
	}

	api_return ShareApi::handleRefreshPaths(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		addAsyncTask([
			paths = Deserializer::deserializeList<string>("paths", reqJson, Deserializer::directoryPathArrayValueParser, false),
			priority = parseRefreshPriority(reqJson),
			complete = aRequest.defer(),
			callerPtr = aRequest.getOwnerPtr()
		] {
			RefreshTaskQueueInfo refreshInfo;
			const auto refreshF = [&] {
				refreshInfo = ShareManager::getInstance()->refreshPathsHookedThrow(priority, paths, callerPtr);
			};

			if (runPathValidatorF(refreshF, complete)) {
				complete(http_status::ok, serializeRefreshQueueInfo(refreshInfo), nullptr);
			}
		});

		return CODE_DEFERRED;
	}

	string ShareApi::formatVirtualPath(const string& aVirtualPath) {
		auto tokens = StringTokenizer<string>(aVirtualPath, ADC_SEPARATOR).getTokens();
		if (tokens.size() == 1) {
			return tokens.front();
		}

		return aVirtualPath;
	}

	api_return ShareApi::handleRefreshVirtualPath(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		addAsyncTask([
			virtualPath = Validation::validateAdcDirectoryPath(JsonUtil::getField<string>("path", reqJson, false)),
			priority = parseRefreshPriority(reqJson),
			complete = aRequest.defer(),
			callerPtr = aRequest.getOwnerPtr()
		] {
			StringList refreshPaths;
			try {
				ShareManager::getInstance()->getRealPaths(virtualPath, refreshPaths);

				auto refreshInfo = ShareManager::getInstance()->refreshPathsHookedThrow(priority, refreshPaths, callerPtr, formatVirtualPath(virtualPath));
				complete(http_status::ok, serializeRefreshQueueInfo(refreshInfo), nullptr);
			} catch (const ShareException& e) {
				complete(http_status::bad_request, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
			}
		});

		return CODE_DEFERRED;
	}

	api_return ShareApi::handleGetStats(ApiRequest& aRequest) {
		auto optionalItemStats = ShareManager::getInstance()->getShareItemStats();
		if (!optionalItemStats) {
			return http_status::no_content;
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
		return http_status::ok;
	}

	api_return ShareApi::handleGetGroupedRootPaths(ApiRequest& aRequest) {
		auto roots = ShareManager::getInstance()->getGroupedDirectories();
		aRequest.setResponseBody(Serializer::serializeList(roots, Serializer::serializeGroupedPaths));
		return http_status::ok;
	}

	api_return ShareApi::handleIsPathShared(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody());
		auto isShared = ShareManager::getInstance()->isRealPathShared(path);
		aRequest.setResponseBody({
			{ "is_shared", isShared },
		});

		return http_status::ok;
	}

	bool ShareApi::runPathValidatorF(const Callback& aValidationF, const ApiCompletionF& aErrorF) noexcept {
		try {
			aValidationF();
		} catch (const QueueException& e) {
			// Queued bundle
			aErrorF(http_status::conflict, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
			return false;
		} catch (const ShareValidatorException& e) {
			// Validation error
			aErrorF(http_status::forbidden, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
			return false;
		} catch (const ShareException& e) {
			// Path not inside a shared directory
			aErrorF(http_status::expectation_failed, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
			return false;
		} catch (const FileException& e) {
			// File doesn't exist
			aErrorF(http_status::not_found, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
			return false;
		}

		return true;
	}

	api_return ShareApi::handleValidatePath(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		addAsyncTask([
			path = JsonUtil::getField<string>("path", reqJson), // File/directory path
				skipCheckQueue = JsonUtil::getOptionalFieldDefault<bool>("skip_check_queue", reqJson, false),
				complete = aRequest.defer(),
				callerPtr = aRequest.getOwnerPtr()
		]{
			const auto validateF = [&] {
				ShareManager::getInstance()->validatePathHooked(path, skipCheckQueue, callerPtr);
			};

			if (runPathValidatorF(validateF, complete)) {
				complete(http_status::no_content, nullptr, nullptr);
			}
		});

		return CODE_DEFERRED;
	}

	api_return ShareApi::handleFindDupePaths(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto ret = json::array();

		auto path = JsonUtil::getOptionalField<string>("path", reqJson);
		if (path) {
			// Note: non-standard/partial paths are allowed, no strict directory path validation
			ret = ShareManager::getInstance()->getAdcDirectoryDupePaths(*path);
		} else {
			auto tth = Deserializer::deserializeTTH(reqJson);
			ret = ShareManager::getInstance()->getRealPaths(tth);
		}

		aRequest.setResponseBody(ret);
		return http_status::ok;
	}

	string ShareApi::refreshTypeToString(ShareRefreshType aType) noexcept {
		switch (aType) {
			case ShareRefreshType::ADD_ROOT_DIRECTORY: return "add_directory";
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

		JsonUtil::throwError("priority_type", JsonException::ERROR_INVALID, "Refresh priority " + priority + "doesn't exist");
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

	void ShareApi::on(ShareManagerListener::RefreshCompleted, const ShareRefreshTask& aTask, bool aSucceed, const ShareRefreshStats& aStats) noexcept {
		maybeSend("share_refresh_completed", [&] {
			return json({
				{ "task", serializeRefreshTask(aTask) },
				{ "results", {
					{ "directory_counts", {
						{ "skipped", aStats.skippedDirectoryCount },
						{ "existing", aStats.existingDirectoryCount },
						{ "new", aStats.newDirectoryCount },
					}},
					{ "file_counts", {
						{ "skipped", aStats.skippedFileCount },
						{ "existing", aStats.existingFileCount },
						{ "new", aStats.newFileCount },
					}},
					{ "hash_bytes_queued", aStats.hashSize },
				}},
				{ "real_paths", aTask.dirs }, // DEPRECATED 
				{ "type", refreshTypeToString(aTask.type) }, // DEPRECATED
			});
		});
	}
}