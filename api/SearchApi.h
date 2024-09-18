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

#ifndef DCPLUSPLUS_DCPP_SEARCHAPI_H
#define DCPLUSPLUS_DCPP_SEARCHAPI_H

#include <api/SearchEntity.h>

#include <api/base/HookApiModule.h>
#include <api/base/HierarchicalApiModule.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/SearchManagerListener.h>

namespace webserver {
	class SearchApi: public ParentApiModule<SearchInstanceToken, SearchEntity, HookApiModule>, public SearchManagerListener {
	public:
		static StringList subscriptionList;

		explicit SearchApi(Session* aSession);
		~SearchApi() final;
	private:
		static json serializeSearchInstance(const SearchInstancePtr& aSearch) noexcept;

		api_return handleCreateInstance(ApiRequest& aRequest) const;
		api_return handleDeleteSubmodule(ApiRequest& aRequest) override;

		api_return handleGetTypes(ApiRequest& aRequest) const;
		api_return handlePostType(ApiRequest& aRequest) const;
		api_return handleGetType(ApiRequest& aRequest) const;
		api_return handleUpdateType(ApiRequest& aRequest) const;
		api_return handleRemoveType(ApiRequest& aRequest) const;

		void on(SearchManagerListener::SearchTypesChanged) noexcept override;
		void on(SearchManagerListener::SearchInstanceCreated, const SearchInstancePtr& aInstance) noexcept override;
		void on(SearchManagerListener::SearchInstanceRemoved, const SearchInstancePtr& aInstance) noexcept override;
		void on(SearchManagerListener::IncomingSearch, Client* aClient, const OnlineUserPtr& aAdcUser, const SearchQuery& aQuery, const SearchResultList& aResults, bool) noexcept override;

		static string serializeSearchQueryItemType(const SearchQuery& aQuery) noexcept;
		static json serializeSearchQuery(const SearchQuery& aQuery) noexcept;
		static json serializeSearchType(const SearchTypePtr& aType) noexcept;
		static string parseSearchTypeId(ApiRequest& aRequest) noexcept;
		string createCurrentSessionOwnerId(const string& aSuffix) const noexcept;

		ActionHookResult<> incomingUserResultHook(const SearchResultPtr& aResult, const ActionHookResultGetter<>& aResultGetter) noexcept;
	};
}

#endif