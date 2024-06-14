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

#include <api/base/HierarchicalApiModule.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/SearchManagerListener.h>

namespace webserver {
	class SearchApi: public ParentApiModule<SearchInstanceToken, SearchEntity>, public SearchManagerListener {
	public:
		static StringList subscriptionList;

		SearchApi(Session* aSession);
		~SearchApi();
	private:
		static json serializeSearchInstance(const SearchInstancePtr& aSearch) noexcept;

		api_return handleCreateInstance(ApiRequest& aRequest);
		api_return handleDeleteSubmodule(ApiRequest& aRequest) override;

		api_return handleGetTypes(ApiRequest& aRequest);
		api_return handlePostType(ApiRequest& aRequest);
		api_return handleGetType(ApiRequest& aRequest);
		api_return handleUpdateType(ApiRequest& aRequest);
		api_return handleRemoveType(ApiRequest& aRequest);

		void on(SearchManagerListener::SearchTypesChanged) noexcept override;
		void on(SearchManagerListener::SearchInstanceCreated, const SearchInstancePtr& aInstance) noexcept override;
		void on(SearchManagerListener::SearchInstanceRemoved, const SearchInstancePtr& aInstance) noexcept override;

		static json serializeSearchType(const SearchTypePtr& aType) noexcept;
		static string parseSearchTypeId(ApiRequest& aRequest) noexcept;
		string createCurrentSessionOwnerId(const string& aSuffix) noexcept;
	};
}

#endif