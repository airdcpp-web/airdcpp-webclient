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

#ifndef DCPLUSPLUS_DCPP_FILELIST_H
#define DCPLUSPLUS_DCPP_FILELIST_H

#include <api/FilelistUtils.h>
#include <api/FilelistItemInfo.h>

#include <api/base/HookApiModule.h>
#include <api/base/HierarchicalApiModule.h>
#include <api/common/ListViewController.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/DirectoryListingListener.h>


namespace webserver {

	class FilelistInfo : public SubApiModule<CID, FilelistInfo, std::string, HookApiModule>, private DirectoryListingListener {
	public:
		using Ptr = shared_ptr<FilelistInfo>;

		static const StringList subscriptionList;

		FilelistInfo(ParentType* aParentModule, const DirectoryListingPtr& aFilelist);
		~FilelistInfo() override;

		DirectoryListingPtr getList() const noexcept { return dl; }

		static string formatState(const DirectoryListingPtr& aList) noexcept;
		static json serializeState(const DirectoryListingPtr& aList) noexcept;
		static json serializeLocation(const DirectoryListingPtr& aListing) noexcept;

		void init() noexcept override;
		CID getId() const noexcept override;
	private:
		api_return handleUpdateList(ApiRequest& aRequest);
		api_return handleChangeDirectory(ApiRequest& aRequest);
		api_return handleSetRead(ApiRequest& aRequest);
		api_return handleGetItems(ApiRequest& aRequest);
		api_return handleGetItem(ApiRequest& aRequest);

		DirectoryListing::DirectoryPtr ensureCurrentDirectoryLoaded() const;

		void on(DirectoryListingListener::LoadingFinished, int64_t aStart, const string& aDir, uint8_t aType) noexcept override;
		void on(DirectoryListingListener::LoadingFailed, const string& aReason) noexcept override;
		void on(DirectoryListingListener::LoadingStarted, bool changeDir) noexcept override;
		void on(DirectoryListingListener::ChangeDirectory, const string& aDir, uint8_t aChangeType) noexcept override;
		void on(DirectoryListingListener::UpdateStatusMessage, const string& aMessage) noexcept override;
		void on(DirectoryListingListener::UserUpdated) noexcept override;
		void on(DirectoryListingListener::StateChanged) noexcept override;
		void on(DirectoryListingListener::Read) noexcept override;
		void on(DirectoryListingListener::ShareProfileChanged) noexcept override;

		void addListTask(Callback&& aTask) noexcept;

		FilelistItemInfo::List getCurrentViewItems();

		DirectoryListingPtr dl;

		using DirectoryView = ListViewController<FilelistItemInfoPtr, FilelistUtils::PROP_LAST>;
		DirectoryView directoryView;

		void onSessionUpdated(const json& aData) noexcept;

		FilelistItemInfo::List currentViewItems;
		bool currentViewItemsInitialized = false;

		void updateItems(const string& aPath) noexcept;

		SharedMutex cs;
	};

	using FilelistInfoPtr = FilelistInfo::Ptr;
}

#endif