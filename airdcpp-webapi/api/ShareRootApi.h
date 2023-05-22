/*
* Copyright (C) 2011-2023 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SHAREROOT_API_H
#define DCPLUSPLUS_DCPP_SHAREROOT_API_H

#include <api/base/ApiModule.h>
#include <api/ShareUtils.h>
#include <api/common/ListViewController.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/HashManagerListener.h>
#include <airdcpp/ShareDirectoryInfo.h>
#include <airdcpp/ShareManagerListener.h>

namespace webserver {
	class ShareRootApi : public SubscribableApiModule, private ShareManagerListener, private HashManagerListener {
	public:
		ShareRootApi(Session* aSession);
		~ShareRootApi();
	private:
		api_return handleGetRoots(ApiRequest& aRequest);

		api_return handleAddRoot(ApiRequest& aRequest);
		api_return handleGetRoot(ApiRequest& aRequest);
		api_return handleUpdateRoot(ApiRequest& aRequest);
		api_return handleRemoveRoot(ApiRequest& aRequest);

		void parseRoot(ShareDirectoryInfoPtr& aInfo, const json& j);

		void on(ShareManagerListener::RootCreated, const string& aPath) noexcept override;
		void on(ShareManagerListener::RootRemoved, const string& aPath) noexcept override;
		void on(ShareManagerListener::RootUpdated, const string& aPath) noexcept override;
		void on(ShareManagerListener::RootRefreshState, const string& aPath) noexcept override;

		void on(HashManagerListener::FileHashed, const string& aFilePath, HashedFile& aFileInfo) noexcept override;

		typedef ListViewController<ShareDirectoryInfoPtr, ShareUtils::PROP_LAST> RootView;
		RootView rootView;

		void onRootUpdated(const string& aPath, PropertyIdSet&& aUpdatedProperties) noexcept;
		void onRootUpdated(const ShareDirectoryInfoPtr& aInfo, PropertyIdSet&& aUpdatedProperties) noexcept;

		ShareDirectoryInfoList getRoots() const noexcept;
		ShareDirectoryInfoPtr findRoot(const string& aPath) noexcept;
		ShareDirectoryInfoPtr getRoot(const ApiRequest& aRequest);

		// ListViewController compares items by memory address so we need to store the list here 
		ShareDirectoryInfoList roots;
		mutable SharedMutex cs;

		TimerPtr timer;
		void onTimer() noexcept;
		StringSet hashedPaths;
	};
}

#endif