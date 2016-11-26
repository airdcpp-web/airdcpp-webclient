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

#include <api/SearchResultInfo.h>
#include <api/SearchUtils.h>

#include <api/ApiModule.h>
#include <api/common/ListViewController.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/SearchManagerListener.h>
#include <airdcpp/SearchQuery.h>


namespace webserver {
	class SearchApi : public SubscribableApiModule, private SearchManagerListener {
	public:
		SearchApi(Session* aSession);
		~SearchApi();

		int getVersion() const noexcept override {
			return 0;
		}
	private:
		static json serializeSearchResult(const SearchResultPtr& aSR) noexcept;
		SearchResultInfo::List getResultList();

		static json serializeDirectSearchResults(const SearchResultList& aResults, SearchQuery& aQuery) noexcept;

		api_return handlePostHubSearch(ApiRequest& aRequest);
		api_return handlePostUserSearch(ApiRequest& aRequest);
		api_return handlePostShareSearch(ApiRequest& aRequest);

		api_return handleGetResults(ApiRequest& aRequest);
		api_return handleGetTypes(ApiRequest& aRequest);

		api_return handleDownload(ApiRequest& aRequest);
		api_return handleGetChildren(ApiRequest& aRequest);

		SearchResultInfo::Ptr getResult(ResultToken aToken);

		void on(SearchManagerListener::SR, const SearchResultPtr& aResult) noexcept override;

		typedef ListViewController<SearchResultInfoPtr, SearchUtils::PROP_LAST> SearchView;
		SearchView searchView;

		SearchResultInfo::Map results;
		shared_ptr<SearchQuery> curSearch;

		std::string currentSearchToken;
		mutable SharedMutex cs;
	};
}

#endif