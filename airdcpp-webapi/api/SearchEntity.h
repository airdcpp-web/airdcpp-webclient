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

#ifndef DCPLUSPLUS_DCPP_SEARCHENTITY_H
#define DCPLUSPLUS_DCPP_SEARCHENTITY_H

#include <api/SearchUtils.h>

#include <api/base/HierarchicalApiModule.h>
#include <api/common/ListViewController.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/SearchInstanceListener.h>


namespace webserver {
	class SearchEntity;
	typedef uint32_t SearchInstanceToken;
	class SearchEntity : public SubApiModule<SearchInstanceToken, SearchEntity, SearchInstanceToken>, private SearchInstanceListener {
	public:
		static const StringList subscriptionList;

		typedef ParentApiModule<SearchInstanceToken, SearchEntity> ParentType;
		typedef shared_ptr<SearchEntity> Ptr;

		SearchEntity(ParentType* aParentModule, const SearchInstancePtr& aSearch, SearchInstanceToken aId, uint64_t aExpirationTick);
		~SearchEntity();

		const SearchInstancePtr& getSearch() const noexcept {
			return search;
		}

		SearchInstanceToken getId() const noexcept override {
			return id;
		}

		optional<int64_t> getTimeToExpiration() const noexcept;

		void init() noexcept override;
	private:
		const SearchInstanceToken id;

		GroupedSearchResultList getResultList() noexcept;

		static json serializeSearchResult(const SearchResultPtr& aSR) noexcept;

		api_return handlePostHubSearch(ApiRequest& aRequest);
		api_return handlePostUserSearch(ApiRequest& aRequest);
		api_return handleGetResults(ApiRequest& aRequest);

		api_return handleDownload(ApiRequest& aRequest);
		api_return handleGetChildren(ApiRequest& aRequest);

		void on(SearchInstanceListener::GroupedResultAdded, const GroupedSearchResultPtr& aResult) noexcept override;
		void on(SearchInstanceListener::GroupedResultUpdated, const GroupedSearchResultPtr& aResult) noexcept override;
		void on(SearchInstanceListener::UserResult, const SearchResultPtr& aResult, const GroupedSearchResultPtr& aParent) noexcept override;
		void on(SearchInstanceListener::Reset) noexcept override;
		void on(SearchInstanceListener::HubSearchSent, const string& aSearchToken, int aSent) noexcept override;

		typedef ListViewController<GroupedSearchResultPtr, SearchUtils::PROP_LAST> SearchView;
		SearchView searchView;

		const SearchInstancePtr search;
		const uint64_t expirationTick;
	};
}

#endif