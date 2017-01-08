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

#include "stdinc.h"
#include "SearchInstance.h"

#include <airdcpp/ClientManager.h>
#include <airdcpp/SearchManager.h>

#include <boost/range/algorithm/copy.hpp>


namespace dcpp {
	SearchInstance::SearchInstance() {
		SearchManager::getInstance()->addListener(this);
	}

	SearchInstance::~SearchInstance() {
		SearchManager::getInstance()->removeListener(this);
	}

	GroupedSearchResult::Ptr SearchInstance::getResult(GroupedResultToken aToken) const noexcept {
		RLock l(cs);
		auto i = find_if(results | map_values, [&](const GroupedSearchResultPtr& aSI) { return aSI->getTTH() == aToken; });
		if (i.base() == results.end()) {
			return nullptr;
		}

		return *i;
	}

	GroupedSearchResultList SearchInstance::getResultList() const noexcept {
		GroupedSearchResultList ret;

		RLock l(cs);
		boost::range::copy(results | map_values, back_inserter(ret));
		return ret;
	}

	GroupedSearchResult::Set SearchInstance::getResultSet() const noexcept {
		GroupedSearchResult::Set resultSet;

		{
			RLock l(cs);
			boost::range::copy(results | map_values, inserter(resultSet, resultSet.begin()));
		}

		return resultSet;
	}

	void SearchInstance::reset() noexcept {
		{
			WLock l(cs);
			results.clear();
		}

		fire(SearchInstanceListener::Reset());
	}

	SearchQueueInfo SearchInstance::hubSearch(StringList& aHubUrls, const SearchPtr& aSearch) noexcept {
		currentSearchToken = aSearch->token;
		curSearch = shared_ptr<SearchQuery>(SearchQuery::getSearch(aSearch));

		reset();

		return SearchManager::getInstance()->search(aHubUrls, aSearch, this);
	}

	void SearchInstance::userSearch(const HintedUser& aUser, const SearchPtr& aSearch) noexcept {
		currentSearchToken = aSearch->token;
		curSearch = shared_ptr<SearchQuery>(SearchQuery::getSearch(aSearch));

		reset();

		ClientManager::getInstance()->directSearch(aUser, aSearch);
	}

	void SearchInstance::on(SearchManagerListener::SR, const SearchResultPtr& aResult) noexcept {
		auto search = curSearch; // Increase the refs
		if (!search) {
			return;
		}

		SearchResult::RelevanceInfo relevanceInfo;
		{
			WLock l(cs);
			if (!aResult->getRelevance(*search.get(), relevanceInfo, currentSearchToken)) {
				return;
			}
		}

		GroupedSearchResultPtr parent = nullptr;
		bool created = false;

		{
			WLock l(cs);
			auto i = results.find(aResult->getTTH());
			if (i == results.end()) {
				parent = std::make_shared<GroupedSearchResult>(aResult, move(relevanceInfo));
				results.emplace(aResult->getTTH(), parent);
				created = true;
			} else {
				parent = i->second;
			}
		}

		if (created) {
			// New parent
			fire(SearchInstanceListener::GroupedResultAdded(), parent);
		} else {
			// Existing parent from now on
			if (!parent->addChildResult(aResult)) {
				return;
			}

			fire(SearchInstanceListener::GroupedResultUpdated(), parent);
		}

		fire(SearchInstanceListener::UserResult(), aResult, parent);
	}
}