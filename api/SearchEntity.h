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

#ifndef DCPLUSPLUS_DCPP_SEARCHENTITY_H
#define DCPLUSPLUS_DCPP_SEARCHENTITY_H

#include <api/SearchUtils.h>

#include <api/base/HookApiModule.h>
#include <api/base/HierarchicalApiModule.h>
#include <api/common/ListViewController.h>

#include <airdcpp/core/header/typedefs.h>
#include <airdcpp/search/SearchInstanceListener.h>


namespace webserver {
	class SearchEntity : public SubApiModule<SearchInstanceToken, SearchEntity, SearchInstanceToken, HookApiModule>, private SearchInstanceListener {
	public:
		static const StringList subscriptionList;

		typedef ParentApiModule<SearchInstanceToken, SearchEntity, HookApiModule> ParentType;
		typedef shared_ptr<SearchEntity> Ptr;

		SearchEntity(ParentType* aParentModule, const SearchInstancePtr& aSearch);
		~SearchEntity();

		const SearchInstancePtr& getSearch() const noexcept {
			return search;
		}

		SearchInstanceToken getId() const noexcept override;

		void init() noexcept override;

		static json serializeSearchQuery(const SearchPtr& aQuery) noexcept;
		static json serializeSearchResult(const SearchResultPtr& aSR) noexcept;
	private:
		const SearchInstancePtr search;

		GroupedSearchResultList getResultList() noexcept;

		json serializeSearchQueueInfo(uint64_t aQueueItem, size_t aQueueCount) noexcept;

		api_return handlePostHubSearch(ApiRequest& aRequest);
		api_return handlePostUserSearch(ApiRequest& aRequest);
		api_return handleGetResults(ApiRequest& aRequest);
		api_return handleGetResult(ApiRequest& aRequest);

		api_return handleDownload(ApiRequest& aRequest);
		api_return handleGetChildren(ApiRequest& aRequest);

		void on(SearchInstanceListener::GroupedResultAdded, const GroupedSearchResultPtr& aResult) noexcept override;
		void on(SearchInstanceListener::ChildResultAdded, const GroupedSearchResultPtr& aResult, const SearchResultPtr&) noexcept override;
		void on(SearchInstanceListener::UserResult, const SearchResultPtr& aResult, const GroupedSearchResultPtr& aParent) noexcept override;
		void on(SearchInstanceListener::Reset) noexcept override;
		void on(SearchInstanceListener::HubSearchSent, const string& aSearchToken, int aSent) noexcept override;
		void on(SearchInstanceListener::HubSearchQueued, const string& aSearchToken, uint64_t aQueueTime, size_t aQueuedCount) noexcept override;

		typedef ListViewController<GroupedSearchResultPtr, SearchUtils::PROP_LAST> SearchView;
		SearchView searchView;

		GroupedSearchResultPtr parseResultParam(ApiRequest& aRequest);
	};
}

#endif