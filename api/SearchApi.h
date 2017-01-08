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

#ifndef DCPLUSPLUS_DCPP_SEARCHAPI_H
#define DCPLUSPLUS_DCPP_SEARCHAPI_H

#include <web-server/stdinc.h>

#include <api/SearchUtils.h>
#include <api/SearchEntity.h>

#include <api/HierarchicalApiModule.h>
#include <api/common/ListViewController.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/SearchManagerListener.h>
#include <airdcpp/SearchQuery.h>


namespace webserver {
	class SearchApi : public ParentApiModule<SearchInstanceToken, SearchEntity>, private SearchManagerListener {
	public:
		static StringList subscriptionList;

		SearchApi(Session* aSession);
		~SearchApi();

		int getVersion() const noexcept override {
			return 0;
		}
	private:
		static json serializeSearchInstance(const SearchEntity& aSearch) noexcept;

		static json serializeSearchResult(const SearchResultPtr& aSR) noexcept;
		GroupedSearchResultList getResultList();

		static json serializeDirectSearchResults(const SearchResultList& aResults, SearchQuery& aQuery) noexcept;
		SearchEntity::Ptr createInstance(uint64_t aExpirationTick);

		api_return handleCreateInstance(ApiRequest& aRequest);
		api_return handleDeleteInstance(ApiRequest& aRequest);

		api_return handlePostHubSearch(ApiRequest& aRequest);
		api_return handlePostUserSearch(ApiRequest& aRequest);
		api_return handlePostShareSearch(ApiRequest& aRequest);

		api_return handleGetResults(ApiRequest& aRequest);
		api_return handleGetTypes(ApiRequest& aRequest);

		api_return handleDownload(ApiRequest& aRequest);
		api_return handleGetChildren(ApiRequest& aRequest);

		GroupedSearchResultPtr getResult(GroupedResultToken aToken);
		void onTimer() noexcept;

		void on(SearchManagerListener::SR, const SearchResultPtr& aResult) noexcept override;

		typedef ListViewController<GroupedSearchResultPtr, SearchUtils::PROP_LAST> SearchView;
		SearchView searchView;

		SearchInstanceToken instanceIdCounter = 0;
		TimerPtr timer;

		GroupedSearchResult::Map results;
		shared_ptr<SearchQuery> curSearch;

		std::string currentSearchToken;
		mutable SharedMutex cs;
	};
}

#endif