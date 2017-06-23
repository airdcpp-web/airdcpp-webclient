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

#include <api/FilelistInfo.h>

#include <api/common/Deserializer.h>
#include <web-server/JsonUtil.h>

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
		METHOD_HANDLER(Access::FILELISTS_VIEW,	METHOD_POST,	(EXACT_PARAM("directory")),									FilelistInfo::handleChangeDirectory);
		METHOD_HANDLER(Access::VIEW_FILES_VIEW,	METHOD_POST,	(EXACT_PARAM("read")),										FilelistInfo::handleSetRead);

		METHOD_HANDLER(Access::FILELISTS_VIEW,	METHOD_GET,		(EXACT_PARAM("items"), RANGE_START_PARAM, RANGE_MAX_PARAM), FilelistInfo::handleGetItems);
	}

	void FilelistInfo::init() noexcept {
		dl->addListener(this);

		if (dl->isLoaded()) {
			addListTask([=] {
				updateItems(dl->getCurrentLocationInfo().directory->getAdcPath());
			});
		}
	}

	CID FilelistInfo::getId() const noexcept {
		return dl->getUser()->getCID();
	}

	FilelistInfo::~FilelistInfo() {
		dl->removeListener(this);
	}

	void FilelistInfo::addListTask(CallBack&& aTask) noexcept {
		dl->addAsyncTask(getAsyncWrapper(move(aTask)));
	}

	api_return FilelistInfo::handleGetItems(ApiRequest& aRequest) {
		int start = aRequest.getRangeParam(START_POS);
		int count = aRequest.getRangeParam(MAX_COUNT);

		{
			RLock l(cs);
			auto curDir = dl->getCurrentLocationInfo().directory;
			if (!curDir->isComplete() || !currentViewItemsInitialized) {
				aRequest.setResponseErrorStr("Content of this directory is not yet available");
				return websocketpp::http::status_code::service_unavailable;
			}

			aRequest.setResponseBody({
				{ "list_path", curDir->getAdcPath() },
				{ "items", Serializer::serializeItemList(start, count, FilelistUtils::propertyHandler, currentViewItems) },
			});
		}

		return websocketpp::http::status_code::ok;
	}

	api_return FilelistInfo::handleChangeDirectory(ApiRequest& aRequest) {
		const auto& j = aRequest.getRequestBody();

		auto listPath = JsonUtil::getField<string>("list_path", j, false);
		auto reload = JsonUtil::getOptionalFieldDefault<bool>("reload", j, false);

		dl->addDirectoryChangeTask(listPath, reload);
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
			bool loading = !aList->getCurrentLocationInfo().directory || aList->getCurrentLocationInfo().directory->getLoading();
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
			currentViewItems.clear();

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

	void FilelistInfo::on(DirectoryListingListener::LoadingFinished, int64_t /*aStart*/, const string& aPath, bool /*aBackgroundTask*/) noexcept {
		if (aPath == dl->getCurrentLocationInfo().directory->getAdcPath()) {
			updateItems(aPath);
		}
	}

	void FilelistInfo::on(DirectoryListingListener::ChangeDirectory, const string& aPath, bool /*aIsSearchChange*/) noexcept {
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