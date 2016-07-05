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
		{ PROP_DUPE, "dupe", TYPE_NUMERIC_OTHER, SERIALIZE_CUSTOM, SORT_NUMERIC },
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

	DirectoryListingToken FilelistItemInfo::getToken() const noexcept {
		return hash<string>()(type == DIRECTORY ? dir->getName() : file->getName());
	}

	FilelistItemInfo::FilelistItemInfo(const DirectoryListing::File::Ptr& f) : type(FILE), file(f) { 
		//dcdebug("FilelistItemInfo (file) %s was created\n", f->getName().c_str());
	}

	FilelistItemInfo::FilelistItemInfo(const DirectoryListing::Directory::Ptr& d) : type(DIRECTORY), dir(d) {
		//dcdebug("FilelistItemInfo (directory) %s was created\n", d->getName().c_str());
	}

	FilelistItemInfo::~FilelistItemInfo() { 
		//dcdebug("FilelistItemInfo %s was deleted\n", getName().c_str());

		// The member destructor is not called automatically in an union
		if (type == FILE) {
			file.~shared_ptr();
		} else {
			dir.~shared_ptr();
		}
	}

	FilelistInfo::FilelistInfo(ParentType* aParentModule, const DirectoryListingPtr& aFilelist) : 
		SubApiModule(aParentModule, aFilelist->getUser()->getCID().toBase32(), subscriptionList), 
		dl(aFilelist),
		directoryView("filelist_view", this, itemHandler, std::bind(&FilelistInfo::getCurrentViewItems, this))
	{
		METHOD_HANDLER("directory", Access::FILELISTS_VIEW, ApiRequest::METHOD_POST, (), true, FilelistInfo::handleChangeDirectory);
		METHOD_HANDLER("read", Access::VIEW_FILES_VIEW, ApiRequest::METHOD_POST, (), false, FilelistInfo::handleSetRead);
	}

	void FilelistInfo::init() noexcept {
		dl->addListener(this);

		if (dl->isLoaded()) {
			addListTask([=] {
				updateItems(dl->getCurrentLocationInfo().directory->getPath());
			});
		}
	}

	FilelistInfo::~FilelistInfo() {
		dl->removeListener(this);
	}

	void FilelistInfo::addListTask(CallBack&& aTask) noexcept {
		dl->addAsyncTask(getAsyncWrapper(move(aTask)));
	}

	api_return FilelistInfo::handleChangeDirectory(ApiRequest& aRequest) {
		const auto& j = aRequest.getRequestBody();

		auto listPath = JsonUtil::getField<string>("list_path", j, false);
		auto reload = JsonUtil::getOptionalFieldDefault<bool>("reload", j, false);

		dl->addDirectoryChangeTask(Util::toNmdcFile(listPath), reload ? DirectoryListing::RELOAD_DIR : DirectoryListing::RELOAD_NONE);
		return websocketpp::http::status_code::ok;
	}

	api_return FilelistInfo::handleSetRead(ApiRequest& aRequest) {
		dl->setRead();
		return websocketpp::http::status_code::ok;
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

		auto ret = Serializer::serializeItem(std::make_shared<FilelistItemInfo>(location.directory), itemHandler);

		ret["size"] = location.totalSize;
		ret["complete"] = location.directory->isComplete();
		return ret;
	}

	// This should be called only from the filelist thread
	void FilelistInfo::updateItems(const string& aPath) noexcept {
		auto curDir = dl->findDirectory(aPath);
		if (!curDir) {
			return;
		}

		{
			WLock l(cs);
			currentViewItems.clear();

			for (auto& d : curDir->directories) {
				currentViewItems.emplace_back(std::make_shared<FilelistItemInfo>(d));
			}

			for (auto& f : curDir->files) {
				currentViewItems.emplace_back(std::make_shared<FilelistItemInfo>(f));
			}
		}

		directoryView.resetItems();

		onSessionUpdated({
			{ "location", serializeLocation(dl) }
		});
	}

	void FilelistInfo::on(DirectoryListingListener::LoadingFailed, const string& aReason) noexcept {

	}

	void FilelistInfo::on(DirectoryListingListener::LoadingStarted, bool aChangeDir) noexcept {

	}

	void FilelistInfo::on(DirectoryListingListener::LoadingFinished, int64_t aStart, const string& aPath, bool aReloadList, bool aChangeDir) noexcept {
		if (aChangeDir || (aPath == dl->getCurrentLocationInfo().directory->getPath())) {
			updateItems(aPath);
		}
	}

	void FilelistInfo::on(DirectoryListingListener::ChangeDirectory, const string& aPath, bool aIsSearchChange) noexcept {
		updateItems(aPath);
	}

	void FilelistInfo::on(DirectoryListingListener::UpdateStatusMessage, const string& aMessage) noexcept {

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