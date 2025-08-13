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

#include <api/FilelistApi.h>

#include <api/common/Deserializer.h>
#include <api/common/Validation.h>

#include <web-server/JsonUtil.h>
#include <web-server/WebServerSettings.h>

#include <airdcpp/filelist/DirectoryListingManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/queue/QueueManager.h>

namespace webserver {

#define HOOK_LOAD_DIRECTORY "filelist_load_directory_hook"
#define HOOK_LOAD_FILE "filelist_load_file_hook"

	StringList FilelistApi::subscriptionList = {
		"filelist_created",
		"filelist_removed",
		"filelist_directory_download_added",
		"filelist_directory_download_removed",
		"filelist_directory_download_processed",
		"filelist_directory_download_failed",
	};

	FilelistApi::FilelistApi(Session* aSession) : 
		ParentApiModule(CID_PARAM, Access::FILELISTS_VIEW, aSession, 
			[](const string& aId) { return Deserializer::parseCID(aId); },
			[](const FilelistInfo& aInfo) { return serializeList(aInfo.getList()); },
			Access::FILELISTS_EDIT
		) 
	{
		createSubscriptions(subscriptionList, FilelistInfo::subscriptionList);

		// Hooks
		HOOK_HANDLER(HOOK_LOAD_DIRECTORY,	DirectoryListingManager::getInstance()->loadHooks.directoryLoadHook,	FilelistApi::directoryLoadHook);
		HOOK_HANDLER(HOOK_LOAD_FILE,		DirectoryListingManager::getInstance()->loadHooks.fileLoadHook,			FilelistApi::fileLoadHook);

		// Methods
		METHOD_HANDLER(Access::FILELISTS_EDIT,	METHOD_POST,	(),													FilelistApi::handlePostList);
		METHOD_HANDLER(Access::FILELISTS_EDIT,	METHOD_POST,	(EXACT_PARAM("self")),								FilelistApi::handleOwnList);

		METHOD_HANDLER(Access::DOWNLOAD,		METHOD_GET,		(EXACT_PARAM("directory_downloads")),				FilelistApi::handleGetDirectoryDownloads);
		METHOD_HANDLER(Access::DOWNLOAD,		METHOD_POST,	(EXACT_PARAM("directory_downloads")),				FilelistApi::handlePostDirectoryDownload);
		METHOD_HANDLER(Access::DOWNLOAD,		METHOD_GET ,	(EXACT_PARAM("directory_downloads"), TOKEN_PARAM),	FilelistApi::handleGetDirectoryDownload);
		METHOD_HANDLER(Access::DOWNLOAD,		METHOD_DELETE,	(EXACT_PARAM("directory_downloads"), TOKEN_PARAM),	FilelistApi::handleDeleteDirectoryDownload);

		METHOD_HANDLER(Access::QUEUE_EDIT,		METHOD_POST,	(EXACT_PARAM("match_queue")),						FilelistApi::handleMatchQueue);

		// Listeners
		DirectoryListingManager::getInstance()->addListener(this);;

		// Init
		auto rawLists = DirectoryListingManager::getInstance()->getLists();
		for (const auto& list : rawLists | views::values) {
			addList(list);
		}
	}

	FilelistApi::~FilelistApi() {
		DirectoryListingManager::getInstance()->removeListener(this);
	}

	ActionHookResult<> FilelistApi::directoryLoadHook(const DirectoryListing::Directory::Ptr& aDirectory, const DirectoryListing& aList, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_LOAD_DIRECTORY, WEBCFG(FILELIST_LOAD_DIRECTORY_HOOK_TIMEOUT).num(), [&]() {
				auto info = std::make_shared<FilelistItemInfo>(aDirectory, aList.getShareProfile());

				return json({
					{ "directory", Serializer::serializeItem(info, FilelistUtils::propertyHandler) },
					{ "filelist_id", aList.getToken().toBase32() },
				});
			}),
			aResultGetter,
			this
		);
	}
	ActionHookResult<> FilelistApi::fileLoadHook(const DirectoryListing::File::Ptr& aFile, const DirectoryListing& aList, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_LOAD_FILE, WEBCFG(FILELIST_LOAD_FILE_HOOK_TIMEOUT).num(), [&]() {
				auto info = std::make_shared<FilelistItemInfo>(aFile, aList.getShareProfile());
				return json({
					{ "file", Serializer::serializeItem(info, FilelistUtils::propertyHandler) },
					{ "filelist_id", aList.getToken().toBase32() },
				});
			}),
			aResultGetter,
			this
		);
	}

	void FilelistApi::addList(const DirectoryListingPtr& aList) noexcept {
		addSubModule(aList->getUser()->getCID(), std::make_shared<FilelistInfo>(this, aList));
	}

	api_return FilelistApi::handlePostList(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		addAsyncTask([
			hintedUser = Deserializer::deserializeHintedUser(reqJson),
			directory = Validation::validateAdcDirectoryPath(JsonUtil::getOptionalFieldDefault<string>("directory", reqJson, ADC_ROOT_STR)),
			complete = aRequest.defer(),
			caller = aRequest.getOwnerPtr()
		]{
			DirectoryListingPtr dl = nullptr;
			try {
				auto listData = FilelistAddData(hintedUser, caller, directory);
				dl = DirectoryListingManager::getInstance()->openRemoteFileListHookedThrow(listData, QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_CLIENT_VIEW);
			} catch (const Exception& e) {
				complete(websocketpp::http::status_code::bad_request, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
				return;
			}

			if (!dl) {
				complete(websocketpp::http::status_code::conflict, nullptr, ApiRequest::toResponseErrorStr("Filelist from this user is open already"));
				return;
			}

			complete(websocketpp::http::status_code::ok, serializeList(dl), nullptr);
			return;
		});

		return CODE_DEFERRED;
	}

	api_return FilelistApi::handleMatchQueue(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		addAsyncTask([
			hintedUser = Deserializer::deserializeHintedUser(reqJson),
			directory = Validation::validateAdcDirectoryPath(JsonUtil::getOptionalFieldDefault<string>("directory", reqJson, ADC_ROOT_STR)),
			complete = aRequest.defer(),
			caller = aRequest.getOwnerPtr()
		] {
			QueueItem::Flags flags = QueueItem::FLAG_MATCH_QUEUE;
			if (directory != ADC_ROOT_STR) {
				flags.setFlag(QueueItem::FLAG_RECURSIVE_LIST);
				flags.setFlag(QueueItem::FLAG_PARTIAL_LIST);
			}

			try {
				auto listData = FilelistAddData(hintedUser, caller, directory);
				QueueManager::getInstance()->addListHooked(listData, flags.getFlags());
			} catch (const Exception& e) {
				complete(websocketpp::http::status_code::bad_request, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
				return;
			}

			complete(websocketpp::http::status_code::no_content, nullptr, nullptr);
		});

		return CODE_DEFERRED;
	}

	api_return FilelistApi::handleOwnList(ApiRequest& aRequest) {
		auto profile = Deserializer::deserializeShareProfile(aRequest.getRequestBody());
		auto dl = DirectoryListingManager::getInstance()->openOwnList(profile);
		if (!dl) {
			aRequest.setResponseErrorStr("Own filelist is open already");
			return websocketpp::http::status_code::conflict;
		}

		aRequest.setResponseBody(serializeList(dl));
		return websocketpp::http::status_code::ok;
	}

	api_return FilelistApi::handleDeleteSubmodule(ApiRequest& aRequest) {
		auto list = getSubModule(aRequest);

		DirectoryListingManager::getInstance()->removeList(list->getList()->getUser());
		return websocketpp::http::status_code::no_content;
	}

	void FilelistApi::on(DirectoryListingManagerListener::ListingCreated, const DirectoryListingPtr& aList) noexcept {
		addList(aList);
		if (!subscriptionActive("filelist_created")) {
			return;
		}

		send("filelist_created", serializeList(aList));
	}

	void FilelistApi::on(DirectoryListingManagerListener::ListingClosed, const DirectoryListingPtr& aList) noexcept {
		removeSubModule(aList->getUser()->getCID());

		if (!subscriptionActive("filelist_removed")) {
			return;
		}

		send("filelist_removed", {
			{ "id", aList->getUser()->getCID().toBase32() }
		});
	}

	void FilelistApi::on(DirectoryListingManagerListener::DirectoryDownloadAdded, const DirectoryDownloadPtr& aDownload) noexcept {
		if (!subscriptionActive("filelist_directory_download_added")) {
			return;
		}

		send("filelist_directory_download_added", Serializer::serializeDirectoryDownload(aDownload));
	}

	void FilelistApi::on(DirectoryListingManagerListener::DirectoryDownloadRemoved, const DirectoryDownloadPtr& aDownload) noexcept {
		if (!subscriptionActive("filelist_directory_download_removed")) {
			return;
		}

		send("filelist_directory_download_removed", Serializer::serializeDirectoryDownload(aDownload));
	}

	void FilelistApi::on(DirectoryListingManagerListener::DirectoryDownloadProcessed, const DirectoryDownloadPtr& aDirectoryInfo, const DirectoryBundleAddResult& aQueueInfo, const string& aError) noexcept {
		if (!subscriptionActive("filelist_directory_download_processed")) {
			return;
		}

		send("filelist_directory_download_processed", {
			{ "directory_download", Serializer::serializeDirectoryDownload(aDirectoryInfo) },
			{ "result", Serializer::serializeDirectoryBundleAddResult(aQueueInfo, aError) }
		});
	}

	void FilelistApi::on(DirectoryListingManagerListener::DirectoryDownloadFailed, const DirectoryDownloadPtr& aDirectoryInfo, const string& aError) noexcept {
		if (!subscriptionActive("filelist_directory_download_failed")) {
			return;
		}

		send("filelist_directory_download_failed", {
			{ "directory_download", Serializer::serializeDirectoryDownload(aDirectoryInfo) },
			{ "error", aError }
		});
	}

	json FilelistApi::serializeShareProfile(const DirectoryListingPtr& aList) noexcept {
		if (!aList->getShareProfile()) {
			return nullptr;
		}

		return Serializer::serializeShareProfileSimple(*aList->getShareProfile());
	}

	json FilelistApi::serializeList(const DirectoryListingPtr& aList) noexcept {
		int64_t totalSize = -1;
		size_t totalFiles = 0;
		aList->getPartialListInfo(totalSize, totalFiles);

		return {
			{ "id", aList->getUser()->getCID().toBase32() },
			{ "user", Serializer::serializeHintedUser(aList->getHintedUser()) },
			{ "state", FilelistInfo::serializeState(aList) },
			{ "location", FilelistInfo::serializeLocation(aList) },
			{ "partial_list", aList->getPartialList() },
			{ "total_files", totalFiles },
			{ "total_size", totalSize },
			{ "read", aList->isRead() },
			{ "share_profile", serializeShareProfile(aList) },
		};
	}

	api_return FilelistApi::handleGetDirectoryDownloads(ApiRequest& aRequest) {
		auto downloads = DirectoryListingManager::getInstance()->getDirectoryDownloads();
		aRequest.setResponseBody(Serializer::serializeList(downloads, Serializer::serializeDirectoryDownload));
		return websocketpp::http::status_code::ok;
	}


	api_return FilelistApi::handleGetDirectoryDownload(ApiRequest& aRequest) {
		auto downloadId = aRequest.getTokenParam();
		auto download = DirectoryListingManager::getInstance()->getDirectoryDownload(downloadId);
		if (!download) {
			aRequest.setResponseErrorStr("Directory download " + Util::toString(downloadId) + " was not found");
			return websocketpp::http::status_code::not_found;
		}

		aRequest.setResponseBody(Serializer::serializeDirectoryDownload(download));
		return websocketpp::http::status_code::ok;
	}

	api_return FilelistApi::handlePostDirectoryDownload(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		auto listPath = Validation::validateAdcDirectoryPath(JsonUtil::getField<string>("list_path", aRequest.getRequestBody(), false));

		string targetDirectory, targetBundleName = PathUtil::getAdcLastDir(listPath);
		Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), aRequest.getSession().get(), targetDirectory, targetBundleName, prio);

		addAsyncTask([
			hintedUser = Deserializer::deserializeHintedUser(reqJson),
			logBundleErrors = JsonUtil::getOptionalFieldDefault<bool>("log_bundle_errors", reqJson, true),
			complete = aRequest.defer(),
			caller = aRequest.getOwnerPtr(),
			targetDirectory,
			targetBundleName,
			prio,
			listPath
		] {
			try {
				auto listData = FilelistAddData(hintedUser, caller, listPath);
				auto errorMethod = logBundleErrors ? DirectoryDownload::ErrorMethod::LOG : DirectoryDownload::ErrorMethod::NONE;
				auto directoryDownload = DirectoryListingManager::getInstance()->addDirectoryDownloadHookedThrow(listData, targetBundleName, targetDirectory, prio, errorMethod);
				complete(websocketpp::http::status_code::ok, Serializer::serializeDirectoryDownload(directoryDownload), nullptr);
				return;
			} catch (const Exception& e) {
				complete(websocketpp::http::status_code::bad_request, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
				return;
			}
		});

		return CODE_DEFERRED;
	}

	api_return FilelistApi::handleDeleteDirectoryDownload(ApiRequest& aRequest) {
		auto downloadId = aRequest.getTokenParam();
		auto removed = DirectoryListingManager::getInstance()->cancelDirectoryDownload(downloadId);
		if (!removed) {
			aRequest.setResponseErrorStr("Directory download " + Util::toString(downloadId) + " was not found");
			return websocketpp::http::status_code::not_found;
		}

		return websocketpp::http::status_code::no_content;
	}
}
