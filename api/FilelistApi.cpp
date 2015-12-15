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

	FilelistApi::FilelistApi(Session* aSession) : ParentApiModule("session", CID_PARAM, Access::FILELISTS_VIEW, aSession, FilelistApi::subscriptionList, FilelistInfo::subscriptionList, [](const string& aId) { return Deserializer::deserializeCID(aId); }) {

		DirectoryListingManager::getInstance()->addListener(this);

		METHOD_HANDLER("sessions", Access::FILELISTS_VIEW, ApiRequest::METHOD_GET, (), false, FilelistApi::handleGetLists);

		METHOD_HANDLER("session", Access::FILELISTS_EDIT, ApiRequest::METHOD_DELETE, (CID_PARAM), false, FilelistApi::handleDeleteList);
		METHOD_HANDLER("session", Access::FILELISTS_EDIT, ApiRequest::METHOD_POST, (), true, FilelistApi::handlePostList);

		METHOD_HANDLER("download_directory", Access::DOWNLOAD, ApiRequest::METHOD_POST, (), true, FilelistApi::handleDownload);

		auto rawLists = DirectoryListingManager::getInstance()->getLists();
		for (const auto& list : rawLists | map_values) {
			addList(list);
		}
	}

	FilelistApi::~FilelistApi() {
		DirectoryListingManager::getInstance()->removeListener(this);
	}

	void FilelistApi::addList(const DirectoryListingPtr& aList) noexcept {
		auto chatInfo = make_shared<FilelistInfo>(this, aList);

		{
			WLock l(cs);
			subModules.emplace(aList->getUser()->getCID(), move(chatInfo));
		}
	}

	api_return FilelistApi::handlePostList(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto user = Deserializer::deserializeHintedUser(reqJson);
		auto optionalDirectory = JsonUtil::getOptionalField<string>("directory", reqJson, false);

		auto directory = optionalDirectory ? Util::toNmdcFile(*optionalDirectory) : Util::emptyString;

		QueueItem::Flags flags;
		flags.setFlag(QueueItem::FLAG_PARTIAL_LIST);
		if (JsonUtil::getField<bool>("client_view", reqJson)) {
			flags.setFlag(QueueItem::FLAG_CLIENT_VIEW);
		}

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
		json retJson;

		{
			RLock l(cs);
			if (!subModules.empty()) {
				for (const auto& list : subModules | map_values) {
					retJson.push_back(serializeList(list->getList()));
				}
			} else {
				retJson = json::array();
			}
		}

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
		{
			WLock l(cs);
			subModules.erase(aList->getUser()->getCID());
		}

		if (!subscriptionActive("filelist_removed")) {
			return;
		}

		send("filelist_removed", {
			{ "id", aList->getUser()->getCID().toBase32() }
		});
	}

	json FilelistApi::serializeList(const DirectoryListingPtr& aList) noexcept {
		return{
			{ "id", aList->getUser()->getCID().toBase32() },
			{ "user", Serializer::serializeHintedUser(aList->getHintedUser()) },
			{ "state", FilelistInfo::serializeState(aList) },
			{ "location", FilelistInfo::serializeLocation(aList) }
			//{ "unread_count", aChat->getCache().countUnreadChatMessages() }
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
