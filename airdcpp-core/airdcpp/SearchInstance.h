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

#ifndef DCPLUSPLUS_DCPP_SEARCHINSTANCE_H
#define DCPLUSPLUS_DCPP_SEARCHINSTANCE_H

#include "stdinc.h"

#include "SearchInstanceListener.h"
#include "SearchManagerListener.h"

#include "GroupedSearchResult.h"
#include "SearchQuery.h"
#include "Speaker.h"


namespace dcpp {
	struct SearchQueueInfo;
	class SearchInstance : public Speaker<SearchInstanceListener>, private SearchManagerListener {
	public:
		SearchInstance();
		~SearchInstance();

		SearchQueueInfo hubSearch(StringList& aHubUrls, const SearchPtr& aSearch) noexcept;
		void userSearch(const HintedUser& aUser, const SearchPtr& aSearch) noexcept;

		void reset() noexcept;

		const string& getCurrentSearchToken() const noexcept {
			return currentSearchToken;
		}

		GroupedSearchResultList getResultList() const noexcept;

		// The most relevant result is sorted first
		GroupedSearchResult::Set getResultSet() const noexcept;
		GroupedSearchResult::Ptr getResult(GroupedResultToken aToken) const noexcept;
	private:
		void on(SearchManagerListener::SR, const SearchResultPtr& aResult) noexcept override;

		GroupedSearchResult::Map results;
		shared_ptr<SearchQuery> curSearch;

		std::string currentSearchToken;
		mutable SharedMutex cs;
	};
}

#endif