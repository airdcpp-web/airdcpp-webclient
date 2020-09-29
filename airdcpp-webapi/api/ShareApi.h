/*
* Copyright (C) 2011-2019 AirDC++ Project
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
#include <airdcpp/ShareManager.h>
#include <airdcpp/ShareManagerListener.h>

namespace webserver {
	class ShareApi : public HookApiModule, private ShareManagerListener {
	public:
		ShareApi(Session* aSession);
		~ShareApi();
	private:
		ActionHookResult<> fileValidationHook(const string& aPath, int64_t aSize, const ActionHookResultGetter<>& aResultGetter) noexcept;
		ActionHookResult<> directoryValidationHook(const string& aPath, const ActionHookResultGetter<>& aResultGetter) noexcept;
		ActionHookResult<> newDirectoryValidationHook(const string& aPath, bool aNewParent, const ActionHookResultGetter<>& aResultGetter) noexcept;
		ActionHookResult<> newFileValidationHook(const string& aPath, int64_t aSize, bool aNewParent, const ActionHookResultGetter<>& aResultGetter) noexcept;

		api_return handleRefreshShare(ApiRequest& aRequest);
		api_return handleRefreshPaths(ApiRequest& aRequest);
		api_return handleRefreshVirtual(ApiRequest& aRequest);
		api_return handleAbortRefreshShare(ApiRequest& aRequest);
		api_return handleAbortRefreshTask(ApiRequest& aRequest);
		api_return handleGetRefreshTasks(ApiRequest& aRequest);

		api_return handleAddExclude(ApiRequest& aRequest);
		api_return handleRemoveExclude(ApiRequest& aRequest);
		api_return handleGetExcludes(ApiRequest& aRequest);

		static json serializeTempShare(const TempShareInfo& aInfo) noexcept;
		api_return handleAddTempShare(ApiRequest& aRequest);
		api_return handleRemoveTempShare(ApiRequest& aRequest);
		api_return handleGetTempShares(ApiRequest& aRequest);

		api_return handleGetStats(ApiRequest& aRequest);
		api_return handleSearch(ApiRequest& aRequest);

		api_return handleGetGroupedRootPaths(ApiRequest& aRequest);
		api_return handleFindDupePaths(ApiRequest& aRequest);
		api_return handleValidatePath(ApiRequest& aRequest);

		void on(ShareManagerListener::RefreshQueued, const ShareRefreshTask& aTask) noexcept override;
		void on(ShareManagerListener::RefreshCompleted, const ShareRefreshTask& aTask, bool aSucceed, int64_t aTotalHash) noexcept override;

		void on(ShareManagerListener::ExcludeAdded, const string& aPath) noexcept override;
		void on(ShareManagerListener::ExcludeRemoved, const string& aPath) noexcept override;

		void on(ShareManagerListener::TempFileAdded, const TempShareInfo& aFile) noexcept override;
		void on(ShareManagerListener::TempFileRemoved, const TempShareInfo& aFile) noexcept override;

		static string refreshTypeToString(ShareRefreshType aType) noexcept;

		static json serializeShareItem(const SearchResultPtr& aSR) noexcept;
		static json serializeRefreshQueueInfo(const ShareManager::RefreshTaskQueueInfo& aRefreshQueueInfo) noexcept;
		static json serializeRefreshTask(const ShareRefreshTask& aRefreshTask) noexcept;

		static string refreshResultToString(ShareManager::RefreshTaskQueueResult aRefreshQueueResult) noexcept;
		static string refreshPriorityToString(ShareRefreshPriority aPriority) noexcept;

		static ShareRefreshPriority parseRefreshPriority(const json& aJson);
	};
}

#endif