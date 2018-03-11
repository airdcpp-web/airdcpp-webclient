/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SHAREAPI_H
#define DCPLUSPLUS_DCPP_SHAREAPI_H

#include <api/base/HookApiModule.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/ShareManagerListener.h>

namespace webserver {
	class ShareApi : public HookApiModule, private ShareManagerListener {
	public:
		ShareApi(Session* aSession);
		~ShareApi();
	private:
		ActionHookRejectionPtr fileValidationHook(const string& aPath, int64_t aSize, const HookRejectionGetter& aErrorGetter) noexcept;
		ActionHookRejectionPtr directoryValidationHook(const string& aPath, const HookRejectionGetter& aErrorGetter) noexcept;

		api_return handleRefreshShare(ApiRequest& aRequest);
		api_return handleRefreshPaths(ApiRequest& aRequest);
		api_return handleRefreshVirtual(ApiRequest& aRequest);

		api_return handleAddExclude(ApiRequest& aRequest);
		api_return handleRemoveExclude(ApiRequest& aRequest);
		api_return handleGetExcludes(ApiRequest& aRequest);

		api_return handleGetStats(ApiRequest& aRequest);
		api_return handleSearch(ApiRequest& aRequest);

		api_return handleGetGroupedRootPaths(ApiRequest& aRequest);
		api_return handleFindDupePaths(ApiRequest& aRequest);
		api_return handleValidatePath(ApiRequest& aRequest);

		void on(ShareManagerListener::RefreshQueued, uint8_t, const RefreshPathList& aPaths) noexcept override;
		void on(ShareManagerListener::RefreshCompleted, uint8_t, const RefreshPathList& aPaths) noexcept override;

		void on(ShareManagerListener::ExcludeAdded, const string& aPath) noexcept override;
		void on(ShareManagerListener::ExcludeRemoved, const string& aPath) noexcept override;

		void onShareRefreshed(const RefreshPathList& aRealPaths, uint8_t aTaskType, const string& aSubscription) noexcept;

		static string refreshTypeToString(uint8_t aTaskType) noexcept;

		static json serializeShareItem(const SearchResultPtr& aSR) noexcept;
	};
}

#endif