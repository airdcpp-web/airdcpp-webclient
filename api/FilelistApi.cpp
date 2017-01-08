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

#include <api/FilelistApi.h>

#include <api/common/Deserializer.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/QueueManager.h>

namespace webserver {
	StringList FilelistApi::subscriptionList = {
		"filelist_created",
		"filelist_removed",
		"filelist_directory_download_added",
		"filelist_directory_download_removed",
		"filelist_directory_download_processed",
		"filelist_directory_download_failed",
	};

	FilelistApi::FilelistApi(Session* aSession) : 
		ParentApiModule("session", CID_PARAM, Access::FILELISTS_VIEW, aSession, FilelistApi::subscriptionList, 
			FilelistInfo::subscriptionList, 
			[](const string& aId) { return Deserializer::parseCID(aId); },
			[](const FilelistInfo& aInfo) { return serializeList(aInfo.getList()); }
		) 
	{

		DirectoryListingManager::getInstance()->addListener(this);;

		METHOD_HANDLER("session", Access::FILELISTS_EDIT, ApiRequest::METHOD_DELETE, (CID_PARAM), false, FilelistApi::handleDeleteList);
		METHOD_HANDLER("session", Access::FILELISTS_EDIT, ApiRequest::METHOD_POST, (), true, FilelistApi::handlePostList);
		METHOD_HANDLER("session", Access::FILELISTS_VIEW, ApiRequest::METHOD_POST, (EXACT_PARAM("me")), true, FilelistApi::handleOwnList);

		METHOD_HANDLER("download_directory", Access::DOWNLOAD, ApiRequest::METHOD_POST, (), true, FilelistApi::handlePostDirectoryDownload); // DEPRECATED

		METHOD_HANDLER("directory_downloads", Access::DOWNLOAD, ApiRequest::METHOD_GET, (), false, FilelistApi::handleGetDirectoryDownloads);
		METHOD_HANDLER("directory_download", Access::DOWNLOAD, ApiRequest::METHOD_POST, (), true, FilelistApi::handlePostDirectoryDownload);
		METHOD_HANDLER("directory_download", Access::DOWNLOAD, ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, FilelistApi::handleDeleteDirectoryDownload);

		METHOD_HANDLER("find_nfo", Access::VIEW_FILES_EDIT, ApiRequest::METHOD_POST, (), true, FilelistApi::handleFindNfo);
		METHOD_HANDLER("match_queue", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (), true, FilelistApi::handleMatchQueue);

		auto rawLists = DirectoryListingManager::getInstance()->getLists();
		for (const auto& list : rawLists | map_values) {
			addList(list);
		}
	}

	FilelistApi::~FilelistApi() {
		DirectoryListingManager::getInstance()->removeListener(this);
	}

	void FilelistApi::addList(const DirectoryListingPtr& aList) noexcept {
		addSubModule(aList->getUser()->getCID(), std::make_shared<FilelistInfo>(this, aList));
	}

	api_return FilelistApi::handleQueueList(ApiRequest& aRequest, QueueItem::Flags aFlags) {
		const auto& reqJson = aRequest.getRequestBody();

		auto user = Deserializer::deserializeHintedUser(reqJson);
		auto directory = Util::toNmdcFile(JsonUtil::getOptionalFieldDefault<string>("directory", reqJson, "/", false));

		auto flags = aFlags;
		flags.setFlag(QueueItem::FLAG_PARTIAL_LIST);

		DirectoryListingPtr dl = nullptr;
		try {
			dl = DirectoryListingManager::getInstance()->createList(user, flags.getFlags(), directory);
		} catch (const DupeException& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::conflict;
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::bad_request;
		}

		aRequest.setResponseBody(serializeList(dl));
		return websocketpp::http::status_code::ok;
	}

	api_return FilelistApi::handleFindNfo(ApiRequest& aRequest) {
		return handleQueueList(aRequest, QueueItem::FLAG_VIEW_NFO | QueueItem::FLAG_RECURSIVE_LIST);
	}

	api_return FilelistApi::handleMatchQueue(ApiRequest& aRequest) {
		return handleQueueList(aRequest, QueueItem::FLAG_MATCH_QUEUE | QueueItem::FLAG_RECURSIVE_LIST);
	}

	api_return FilelistApi::handlePostList(ApiRequest& aRequest) {
		return handleQueueList(aRequest, QueueItem::FLAG_CLIENT_VIEW);
	}

	api_return FilelistApi::handleOwnList(ApiRequest& aRequest) {
		auto profile = Deserializer::deserializeShareProfile(aRequest.getRequestBody());
		DirectoryListingManager::getInstance()->openOwnList(profile);

		return websocketpp::http::status_code::ok;
	}

	api_return FilelistApi::handleDeleteList(ApiRequest& aRequest) {
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

	void FilelistApi::on(DirectoryListingManagerListener::DirectoryDownloadProcessed, const DirectoryDownloadPtr& aDirectoryInfo, const DirectoryBundleAddInfo& aQueueInfo, const string& aError) noexcept {
		if (!subscriptionActive("filelist_directory_download_processed")) {
			return;
		}

		send("filelist_directory_download_processed", {
			{ "directory_download", Serializer::serializeDirectoryDownload(aDirectoryInfo) },
			{ "result", Serializer::serializeDirectoryBundleAddInfo(aQueueInfo, aError) }
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
		if (!aList->getIsOwnList()) {
			return nullptr;
		}

		return Serializer::serializeShareProfileSimple(aList->getShareProfile());
	}

	json FilelistApi::serializeList(const DirectoryListingPtr& aList) noexcept {
		int64_t totalSize = -1;
		size_t totalFiles = -1;
		aList->getPartialListInfo(totalSize, totalFiles);

		return {
			{ "id", aList->getUser()->getCID().toBase32() },
			{ "user", Serializer::serializeHintedUser(aList->getHintedUser()) },
			{ "state", FilelistInfo::serializeState(aList) },
			{ "location", FilelistInfo::serializeLocation(aList) },
			{ "partial", aList->getPartialList() },
			{ "total_files", totalFiles },
			{ "total_size", totalSize },
			{ "read", aList->isRead() },
			{ "share_profile", serializeShareProfile(aList) },
		};
	}

	api_return FilelistApi::handleGetDirectoryDownloads(ApiRequest& aRequest) {
		auto downloads = DirectoryListingManager::getInstance()->getDirectoryDownloads();

		auto ret = json::array();
		for (const auto& d : downloads) {
			ret.push_back(Serializer::serializeDirectoryDownload(d));
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	api_return FilelistApi::handlePostDirectoryDownload(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		auto listPath = JsonUtil::getField<string>("list_path", aRequest.getRequestBody(), false);

		string targetDirectory, targetBundleName = Util::getAdcLastDir(listPath);
		Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), aRequest.getSession(), targetDirectory, targetBundleName, prio);

		auto user = Deserializer::deserializeHintedUser(reqJson);

		try {
			auto directoryDownload = DirectoryListingManager::getInstance()->addDirectoryDownload(user, targetBundleName, Util::toNmdcFile(listPath), targetDirectory, prio);
			aRequest.setResponseBody(Serializer::serializeDirectoryDownload(directoryDownload));
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
	}

	api_return FilelistApi::handleDeleteDirectoryDownload(ApiRequest& aRequest) {
		auto removed = DirectoryListingManager::getInstance()->removeDirectoryDownload(aRequest.getTokenParam(0));
		if (!removed) {
			aRequest.setResponseErrorStr("Directory download not found");
			return websocketpp::http::status_code::not_found;
		}

		return websocketpp::http::status_code::no_content;
	}
}
