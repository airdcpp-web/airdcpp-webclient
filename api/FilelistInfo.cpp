/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#include <api/FilelistInfo.h>

#include <api/common/Deserializer.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/Client.h>
#include <airdcpp/DirectoryListingManager.h>


namespace webserver {
	const StringList FilelistInfo::subscriptionList = {
		"filelist_updated"
	};

	FilelistInfo::FilelistInfo(ParentType* aParentModule, const DirectoryListingPtr& aFilelist) : 
		SubApiModule(aParentModule, aFilelist->getUser()->getCID().toBase32(), subscriptionList), 
		dl(aFilelist),
		directoryView("filelist_view", this, FilelistUtils::propertyHandler, std::bind(&FilelistInfo::getCurrentViewItems, this))
	{
		METHOD_HANDLER(Access::FILELISTS_VIEW,	METHOD_PATCH,	(),															FilelistInfo::handleUpdateList);

		METHOD_HANDLER(Access::FILELISTS_VIEW,	METHOD_POST,	(EXACT_PARAM("directory")),									FilelistInfo::handleChangeDirectory);
		METHOD_HANDLER(Access::FILELISTS_VIEW,	METHOD_POST,	(EXACT_PARAM("read")),										FilelistInfo::handleSetRead);

		METHOD_HANDLER(Access::FILELISTS_VIEW,	METHOD_GET,		(EXACT_PARAM("items"), RANGE_START_PARAM, RANGE_MAX_PARAM), FilelistInfo::handleGetItems);
		METHOD_HANDLER(Access::FILELISTS_VIEW,	METHOD_GET,		(EXACT_PARAM("items"), TOKEN_PARAM),						FilelistInfo::handleGetItem);
	}

	void FilelistInfo::init() noexcept {
		dl->addListener(this);

		if (dl->isLoaded()) {
			auto start = GET_TICK();
			addListTask([=] {
				updateItems(dl->getCurrentLocationInfo().directory->getAdcPath());
				dcdebug("Filelist %s was loaded in " I64_FMT " milliseconds\n", dl->getNick(false).c_str(), GET_TICK() - start);
			});
		}
	}

	CID FilelistInfo::getId() const noexcept {
		return dl->getUser()->getCID();
	}

	FilelistInfo::~FilelistInfo() {
		dl->removeListener(this);
	}

	void FilelistInfo::addListTask(Callback&& aTask) noexcept {
		dl->addAsyncTask(getAsyncWrapper(move(aTask)));
	}

	api_return FilelistInfo::handleUpdateList(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		if (dl->getIsOwnList()) {
			auto profile = Deserializer::deserializeOptionalShareProfile(reqJson);
			if (profile) {
				dl->addShareProfileChangeTask(*profile);
			}
		} else {
			auto client = Deserializer::deserializeClient(reqJson, true);
			if (client) {
				dl->addHubUrlChangeTask(client->getHubUrl());
			}
		}

		return websocketpp::http::status_code::no_content;
	}

	api_return FilelistInfo::handleGetItems(ApiRequest& aRequest) {
		int start = aRequest.getRangeParam(START_POS);
		int count = aRequest.getRangeParam(MAX_COUNT);

		{
			auto curDir = ensureCurrentDirectoryLoaded();
			aRequest.setResponseBody({
				{ "list_path", curDir->getAdcPath() },
				{ "items", Serializer::serializeItemList(start, count, FilelistUtils::propertyHandler, currentViewItems) },
			});
		}

		return websocketpp::http::status_code::ok;
	}

	DirectoryListing::Directory::Ptr FilelistInfo::ensureCurrentDirectoryLoaded() {
		auto curDir = dl->getCurrentLocationInfo().directory;
		if (!curDir) {
			throw RequestException(websocketpp::http::status_code::service_unavailable, "Filelist has not finished loading yet");
		}

		if (!curDir->isComplete()) {
			throw RequestException(websocketpp::http::status_code::service_unavailable, "Content of directory " + curDir->getAdcPath() + " is not yet available");
		}

		if (!currentViewItemsInitialized) {
			// The list content is know but the module hasn't initialized view items yet
			// It can especially with extensions having filelist context menu items that try get fetch 
			// items by ID for the first time (that will trigger initialization of the filelist module)

			// Wait as that shouldn't take too long
			for (auto i = 0; i < (2000 / 20); ++i) {
				if (!currentViewItemsInitialized) {
					Thread::sleep(20);
				}
			}

			if (!currentViewItemsInitialized) {
				throw RequestException(websocketpp::http::status_code::service_unavailable, "Content of directory " + curDir->getAdcPath() + " has not finished loading yet");
			}
		}

		return curDir;
	}

	api_return FilelistInfo::handleGetItem(ApiRequest& aRequest) {
		FilelistItemInfoPtr item = nullptr;
		auto itemId = aRequest.getTokenParam();

		auto curDir = dl->getCurrentLocationInfo().directory;
		if (!curDir) {
			throw RequestException(websocketpp::http::status_code::service_unavailable, "Filelist has not finished loading yet");
		}

		// TODO: refactor filelists and do something better than this
		{
			RLock l(cs);

			// Check view items
			auto i = boost::find_if(currentViewItems, [itemId](const FilelistItemInfoPtr& aInfo) {
				return aInfo->getToken() == itemId;
			});

			if (i == currentViewItems.end()) {
				// Check current location
				auto dirInfo = std::make_shared<FilelistItemInfo>(curDir);
				if (dirInfo->getToken() == itemId) {
					item = dirInfo;
				}
			} else {
				item = *i;
			}
		}

		if (!item) {
			aRequest.setResponseErrorStr("Item " + Util::toString(itemId) + " was not found");
			return websocketpp::http::status_code::not_found;
		}

		auto j = Serializer::serializeItem(item, FilelistUtils::propertyHandler);
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return FilelistInfo::handleChangeDirectory(ApiRequest& aRequest) {
		const auto& j = aRequest.getRequestBody();

		auto listPath = JsonUtil::getField<string>("list_path", j, false);
		auto reload = JsonUtil::getOptionalFieldDefault<bool>("reload", j, false);

		dl->addDirectoryChangeTask(listPath, reload ? DirectoryListing::DirectoryLoadType::CHANGE_RELOAD : DirectoryListing::DirectoryLoadType::CHANGE_NORMAL);
		return websocketpp::http::status_code::no_content;
	}

	api_return FilelistInfo::handleSetRead(ApiRequest&) {
		dl->setRead();
		return websocketpp::http::status_code::no_content;
	}

	FilelistItemInfo::List FilelistInfo::getCurrentViewItems() {
		RLock l(cs);
		return currentViewItems;
	}

	string FilelistInfo::formatState(const DirectoryListingPtr& aList) noexcept {
		if (aList->getDownloadState() == DirectoryListing::STATE_DOWNLOADED) {
			return aList->isLoaded() ? "loaded" : "loading";
		}

		return Serializer::serializeDownloadState(*aList.get());
	}

	json FilelistInfo::serializeState(const DirectoryListingPtr& aList) noexcept {
		if (aList->getDownloadState() == DirectoryListing::STATE_DOWNLOADED) {
			bool loading = !aList->getCurrentLocationInfo().directory || aList->getCurrentLocationInfo().directory->getLoading() != DirectoryListing::DirectoryLoadType::NONE;
			return {
				{ "id", loading ? "loading" : "loaded" },
				{ "str", loading ? "Parsing data" : "Loaded" },
			};
		}

		return Serializer::serializeDownloadState(*aList.get());
	}

	json FilelistInfo::serializeLocation(const DirectoryListingPtr& aListing) noexcept {
		const auto& location = aListing->getCurrentLocationInfo();
		if (!location.directory) {
			// The list hasn't been loaded yet
			return nullptr;
		}

		auto ret = Serializer::serializeItem(std::make_shared<FilelistItemInfo>(location.directory), FilelistUtils::propertyHandler);

		ret["size"] = location.totalSize;
		return ret;
	}

	// This should be called only from the filelist thread
	void FilelistInfo::updateItems(const string& aPath) noexcept {
		{
			WLock l(cs);
			currentViewItemsInitialized = false;
			currentViewItems.clear();
		}

		auto curDir = dl->findDirectory(aPath);
		if (!curDir) {
			return;
		}

		{
			WLock l(cs);
			for (auto& d : curDir->directories | map_values) {
				currentViewItems.emplace_back(std::make_shared<FilelistItemInfo>(d));
			}

			for (auto& f : curDir->files) {
				currentViewItems.emplace_back(std::make_shared<FilelistItemInfo>(f));
			}

			currentViewItemsInitialized = true;
		}

		directoryView.resetItems();

		onSessionUpdated({
			{ "location", serializeLocation(dl) },
			{ "read", dl->isRead() },
		});
	}

	void FilelistInfo::on(DirectoryListingListener::LoadingFailed, const string&) noexcept {

	}

	void FilelistInfo::on(DirectoryListingListener::LoadingStarted, bool /*aChangeDir*/) noexcept {

	}

	void FilelistInfo::on(DirectoryListingListener::LoadingFinished, int64_t /*aStart*/, const string& aLoadedPath, uint8_t aType) noexcept {
		if (static_cast<DirectoryListing::DirectoryLoadType>(aType) != DirectoryListing::DirectoryLoadType::LOAD_CONTENT) {
			// Insert new items
			updateItems(aLoadedPath);
		} else if (AirUtil::isParentOrExactAdc(aLoadedPath, dl->getCurrentLocationInfo().directory->getAdcPath())) {
			// Reload directory content
			updateItems(dl->getCurrentLocationInfo().directory->getAdcPath());
		}
	}

	void FilelistInfo::on(DirectoryListingListener::ChangeDirectory, const string& aPath, uint8_t /*aChangeType*/) noexcept {
		updateItems(aPath);
	}

	void FilelistInfo::on(DirectoryListingListener::UpdateStatusMessage, const string& /*aMessage*/) noexcept {

	}

	void FilelistInfo::on(DirectoryListingListener::StateChanged) noexcept {
		onSessionUpdated({
			{ "state", serializeState(dl) }
		});
	}

	void FilelistInfo::on(DirectoryListingListener::Read) noexcept {
		onSessionUpdated({
			{ "read", dl->isRead() }
		});
	}

	void FilelistInfo::on(DirectoryListingListener::UserUpdated) noexcept {
		onSessionUpdated({
			{ "user", Serializer::serializeHintedUser(dl->getHintedUser()) }
		});
	}

	void FilelistInfo::on(DirectoryListingListener::ShareProfileChanged) noexcept {
		onSessionUpdated({
			{ "share_profile", Serializer::serializeShareProfileSimple(dl->getShareProfile()) }
		});
	}

	void FilelistInfo::onSessionUpdated(const json& aData) noexcept {
		if (!subscriptionActive("filelist_updated")) {
			return;
		}

		send("filelist_updated", aData);
	}
}