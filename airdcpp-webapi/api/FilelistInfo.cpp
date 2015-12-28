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

#include <api/FilelistInfo.h>
#include <api/FilelistUtils.h>

#include <api/common/Deserializer.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/DirectoryListingManager.h>
#include <airdcpp/Download.h>
#include <airdcpp/DownloadManager.h>

namespace webserver {
	const PropertyList FilelistInfo::properties = {
		{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
		{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_DATE, "time", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_PATH, "path", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_TTH, "tth", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_DUPE, "dupe", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
		//{ PROP_COMPLETE, "complete", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
	};

	const StringList FilelistInfo::subscriptionList = {
		"filelist_updated"
	};

	const FilelistInfo::Handler FilelistInfo::itemHandler(properties,
		FilelistUtils::getStringInfo, 
		FilelistUtils::getNumericInfo, 
		FilelistUtils::compareItems, 
		FilelistUtils::serializeItem
	);

	FilelistInfo::FilelistInfo(ParentType* aParentModule, const DirectoryListingPtr& aFilelist) : 
		SubApiModule(aParentModule, aFilelist->getUser()->getCID().toBase32(), subscriptionList), 
		dl(aFilelist),
		directoryView("filelist_view", this, itemHandler, std::bind(&FilelistInfo::getCurrentViewItems, this))
	{
		METHOD_HANDLER("directory", Access::FILELISTS_VIEW, ApiRequest::METHOD_POST, (), true, FilelistInfo::handleChangeDirectory);

		dl->addListener(this);

		if (dl->isOpen()) {
			updateItems(dl->getCurrentLocationInfo().directory->getPath());
		}
	}

	FilelistInfo::~FilelistInfo() {
		dl->removeListener(this);
	}

	api_return FilelistInfo::handleChangeDirectory(ApiRequest& aRequest) {
		auto listPath = JsonUtil::getField<string>("list_path", aRequest.getRequestBody(), false);
		dl->addAsyncTask([=] {
			dl->changeDirectory(Util::toNmdcFile(listPath), DirectoryListing::RELOAD_NONE);
		});
		return websocketpp::http::status_code::ok;
	}

	FilelistItemInfo::List FilelistInfo::getCurrentViewItems() {
		RLock l(cs);
		return currentViewItems;
	}

	json FilelistInfo::serializeState(const DirectoryListingPtr& aList) noexcept {
		string id;
		switch (aList->getState()) {
			case DirectoryListing::STATE_DOWNLOAD_PENDING: id = "download_pending"; break;
			case DirectoryListing::STATE_DOWNLOADING: id = "downloading"; break;
			case DirectoryListing::STATE_LOADING: id = "loading"; break;
			case DirectoryListing::STATE_LOADED: id = "loaded"; break;
		}

		return{
			{ "id", id }
		};
	}

	json FilelistInfo::serializeLocation(const DirectoryListingPtr& aListing) noexcept {
		const auto& location = aListing->getCurrentLocationInfo();
		auto ret = Serializer::serializeItem(make_shared<FilelistItemInfo>(location.directory), itemHandler);

		ret["size"] = location.totalSize;
		ret["files"] = location.files;
		ret["directories"] = location.directories;
		ret["complete"] = location.directory->isComplete();
		return ret;
	}

	void FilelistInfo::updateItems(const string& aPath) noexcept {
		dl->addAsyncTask([=] {
			auto curDir = dl->findDirectory(aPath);
			if (!curDir) {
				return;
			}

			{
				WLock l(cs);
				currentViewItems.clear();

				for (auto& d : curDir->directories) {
					currentViewItems.emplace_back(make_shared<FilelistItemInfo>(d));
				}

				for (auto& f : curDir->files) {
					currentViewItems.emplace_back(make_shared<FilelistItemInfo>(f));
				}
			}

			directoryView.resetItems();

			json j;
			onSessionUpdated({
				{ "location", serializeLocation(dl) }
			});
		});
	}

	void FilelistInfo::on(DirectoryListingListener::LoadingFailed, const string& aReason) noexcept {

	}

	void FilelistInfo::on(DirectoryListingListener::LoadingStarted, bool changeDir) noexcept {

	}

	void FilelistInfo::on(DirectoryListingListener::LoadingFinished, int64_t aStart, const string& aPath, bool reloadList, bool changeDir) noexcept {
		if (changeDir) {
			updateItems(aPath);
		}
	}

	void FilelistInfo::on(DirectoryListingListener::ChangeDirectory, const string& aPath, bool isSearchChange) noexcept {
		updateItems(aPath);
	}

	void FilelistInfo::on(DirectoryListingListener::UpdateStatusMessage, const string& aMessage) noexcept {

	}

	void FilelistInfo::on(DirectoryListingListener::StateChanged, uint8_t aState) noexcept {
		onSessionUpdated({
			{ "state", serializeState(dl) }
		});
	}

	void FilelistInfo::on(DirectoryListingListener::UserUpdated) noexcept {
		onSessionUpdated({
			{ "user", Serializer::serializeHintedUser(dl->getHintedUser()) }
		});
	}

	void FilelistInfo::onSessionUpdated(const json& aData) noexcept {
		if (!subscriptionActive("filelist_updated")) {
			return;
		}

		send("filelist_updated", aData);
	}
}