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

#include <api/FilelistApi.h>

#include <api/common/Deserializer.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/QueueManager.h>

namespace webserver {
	StringList FilelistApi::subscriptionList = {
		"filelist_created",
		"filelist_removed"
	};

	FilelistApi::FilelistApi(Session* aSession) : ParentApiModule("session", CID_PARAM, Access::FILELISTS_VIEW, aSession, FilelistApi::subscriptionList, FilelistInfo::subscriptionList, [](const string& aId) { return Deserializer::parseCID(aId); }) {

		DirectoryListingManager::getInstance()->addListener(this);

		METHOD_HANDLER("sessions", Access::FILELISTS_VIEW, ApiRequest::METHOD_GET, (), false, FilelistApi::handleGetLists);

		METHOD_HANDLER("session", Access::FILELISTS_EDIT, ApiRequest::METHOD_DELETE, (CID_PARAM), false, FilelistApi::handleDeleteList);
		METHOD_HANDLER("session", Access::FILELISTS_EDIT, ApiRequest::METHOD_POST, (), true, FilelistApi::handlePostList);

		METHOD_HANDLER("download_directory", Access::DOWNLOAD, ApiRequest::METHOD_POST, (), true, FilelistApi::handleDownload);
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
		addSubModule(aList->getUser()->getCID(), make_shared<FilelistInfo>(this, aList));
	}

	api_return FilelistApi::handleQueueList(ApiRequest& aRequest, QueueItem::Flags aFlags) {
		const auto& reqJson = aRequest.getRequestBody();

		auto user = Deserializer::deserializeHintedUser(reqJson);
		auto directory = Util::toNmdcFile(JsonUtil::getOptionalFieldDefault<string>("directory", reqJson, "/", false));

		auto flags = aFlags;
		flags.setFlag(QueueItem::FLAG_PARTIAL_LIST);

		QueueItemPtr q = nullptr;
		try {
			q = QueueManager::getInstance()->addList(user, flags.getFlags(), directory);
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::bad_request;
		}

		aRequest.setResponseBody({
			{ "id", user.user->getCID().toBase32() }
		});

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

	api_return FilelistApi::handleDeleteList(ApiRequest& aRequest) {
		auto list = getSubModule(aRequest.getStringParam(0));
		if (!list) {
			aRequest.setResponseErrorStr("List not found");
			return websocketpp::http::status_code::not_found;
		}

		DirectoryListingManager::getInstance()->removeList(list->getList()->getUser());
		return websocketpp::http::status_code::ok;
	}

	api_return FilelistApi::handleGetLists(ApiRequest& aRequest) {
		auto retJson = json::array();
		forEachSubModule([&](const FilelistInfo& aInfo) { 
			retJson.push_back(serializeList(aInfo.getList()));
		});

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
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

	json FilelistApi::serializeList(const DirectoryListingPtr& aList) noexcept {
		int64_t shareSize = -1, totalFiles = -1;
		auto user = ClientManager::getInstance()->findOnlineUser(aList->getHintedUser());
		if (user) {
			shareSize = Util::toInt64(user->getIdentity().getShareSize());
			totalFiles = Util::toInt64(user->getIdentity().getSharedFiles());
		}

		return{
			{ "id", aList->getUser()->getCID().toBase32() },
			{ "user", Serializer::serializeHintedUser(aList->getHintedUser()) },
			{ "state", FilelistInfo::serializeState(aList) },
			{ "location", FilelistInfo::serializeLocation(aList) },
			{ "partial", aList->getPartialList() },
			{ "total_files", totalFiles },
			{ "total_size", shareSize },
		};
	}

	api_return FilelistApi::handleDownload(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		auto listPath = JsonUtil::getField<string>("list_path", aRequest.getRequestBody(), false);

		string targetDirectory, targetBundleName;
		TargetUtil::TargetType targetType;
		QueueItemBase::Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), targetDirectory, targetBundleName, targetType, prio);

		auto user = Deserializer::deserializeHintedUser(reqJson);

		DirectoryListingManager::getInstance()->addDirectoryDownload(Util::toNmdcFile(listPath), targetBundleName, user,
			targetDirectory, targetType, true, prio);

		return websocketpp::http::status_code::ok;
	}
}
