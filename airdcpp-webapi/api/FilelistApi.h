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

#ifndef DCPLUSPLUS_DCPP_FILELISTAPI_H
#define DCPLUSPLUS_DCPP_FILELISTAPI_H

#include <api/base/HierarchicalApiModule.h>
#include <api/base/HookApiModule.h>
#include <api/FilelistInfo.h>

#include <airdcpp/core/header/typedefs.h>
#include <airdcpp/filelist/DirectoryListingManagerListener.h>
#include <airdcpp/queue/QueueItem.h>

namespace webserver {
	class FilelistApi : public ParentApiModule<CID, FilelistInfo, HookApiModule>, private DirectoryListingManagerListener {
	public:
		static StringList subscriptionList;

		explicit FilelistApi(Session* aSession);
		~FilelistApi() override;
	private:
		void addList(const DirectoryListingPtr& aList) noexcept;

		api_return handlePostList(ApiRequest& aRequest);
		api_return handleDeleteSubmodule(ApiRequest& aRequest) override;
		api_return handleOwnList(ApiRequest& aRequest);

		api_return handlePostDirectoryDownload(ApiRequest& aRequest);
		api_return handleDeleteDirectoryDownload(ApiRequest& aRequest);
		api_return handleGetDirectoryDownloads(ApiRequest& aRequest);
		api_return handleGetDirectoryDownload(ApiRequest& aRequest);

		api_return handleMatchQueue(ApiRequest& aRequest);

		void on(DirectoryListingManagerListener::ListingCreated, const DirectoryListingPtr& aList) noexcept override;
		void on(DirectoryListingManagerListener::ListingClosed, const DirectoryListingPtr&) noexcept override;

		void on(DirectoryListingManagerListener::DirectoryDownloadAdded, const DirectoryDownloadPtr&) noexcept override;
		void on(DirectoryListingManagerListener::DirectoryDownloadRemoved, const DirectoryDownloadPtr&) noexcept override;
		void on(DirectoryListingManagerListener::DirectoryDownloadProcessed, const DirectoryDownloadPtr& aDirectoryInfo, const DirectoryBundleAddResult& aQueueInfo, const string& aError) noexcept override;
		void on(DirectoryListingManagerListener::DirectoryDownloadFailed, const DirectoryDownloadPtr& aDirectoryInfo, const string& aError) noexcept override;

		static json serializeList(const DirectoryListingPtr& aList) noexcept;
		static json serializeShareProfile(const DirectoryListingPtr& aList) noexcept;

		ActionHookResult<> directoryLoadHook(const DirectoryListing::Directory::Ptr& aDirectory, const DirectoryListing& aList, const ActionHookResultGetter<>& aResultGetter) noexcept;
		ActionHookResult<> fileLoadHook(const DirectoryListing::File::Ptr& aFile, const DirectoryListing& aList, const ActionHookResultGetter<>& aResultGetter) noexcept;
	};
}

#endif