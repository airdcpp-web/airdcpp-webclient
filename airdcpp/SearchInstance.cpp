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

#include "stdinc.h"
#include "SearchInstance.h"

#include <airdcpp/ClientManager.h>
#include <airdcpp/SearchManager.h>

#include <boost/range/algorithm/copy.hpp>


namespace dcpp {
	SearchInstance::SearchInstance() {
		SearchManager::getInstance()->addListener(this);
		ClientManager::getInstance()->addListener(this);
	}

	SearchInstance::~SearchInstance() {
		ClientManager::getInstance()->cancelSearch(this);

		ClientManager::getInstance()->removeListener(this);
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

	void SearchInstance::reset(const SearchPtr& aSearch) noexcept {
		ClientManager::getInstance()->cancelSearch(this);

		{
			WLock l(cs);
			currentSearchToken = aSearch->token;
			curSearch = shared_ptr<SearchQuery>(SearchQuery::getSearch(aSearch));

			results.clear();
			queuedHubUrls.clear();
			searchesSent = 0;
		}

		fire(SearchInstanceListener::Reset());
	}

	SearchQueueInfo SearchInstance::hubSearch(StringList& aHubUrls, const SearchPtr& aSearch) noexcept {
		reset(aSearch);

		auto queueInfo = SearchManager::getInstance()->search(aHubUrls, aSearch, this);
		if (!queueInfo.queuedHubUrls.empty()) {
			lastSearchTime = GET_TIME();
			if (queueInfo.queueTime < 1000) {
				fire(SearchInstanceListener::HubSearchSent(), currentSearchToken, queueInfo.queuedHubUrls.size());
			} else {
				WLock l(cs);
				queuedHubUrls = queueInfo.queuedHubUrls;
			}
		}

		return queueInfo;
	}

	bool SearchInstance::userSearch(const HintedUser& aUser, const SearchPtr& aSearch, string& error_) noexcept {
		reset(aSearch);
		if (!ClientManager::getInstance()->directSearch(aUser, aSearch, error_)) {
			return false;
		}

		lastSearchTime = GET_TIME();
		return true;
	}

	uint64_t SearchInstance::getTimeFromLastSearch() const noexcept {
		if (lastSearchTime == 0) {
			return 0;
		}

		return GET_TIME() - lastSearchTime;
	}

	uint64_t SearchInstance::getQueueTime() const noexcept {
		auto time = ClientManager::getInstance()->getMaxSearchQueueTime(this);
		if (time) {
			return *time;
		}

		return 0;
	}

	int SearchInstance::getQueueCount() const noexcept {
		RLock l(cs);
		return static_cast<int>(queuedHubUrls.size());
	}

	int SearchInstance::getResultCount() const noexcept {
		RLock l(cs);
		return static_cast<int>(results.size());
	}

	void SearchInstance::on(ClientManagerListener::OutgoingSearch, const string& aHubUrl, const SearchPtr& aSearch) noexcept {
		if (aSearch->owner != this) {
			return;
		}

		searchesSent++;
		removeQueuedUrl(aHubUrl);
	}

	void SearchInstance::on(ClientManagerListener::ClientDisconnected, const string& aHubUrl) noexcept {
		removeQueuedUrl(aHubUrl);
	}

	void SearchInstance::removeQueuedUrl(const string& aHubUrl) noexcept {
		auto queueEmpty = false;

		{
			WLock l(cs);
			auto removed = queuedHubUrls.erase(aHubUrl);
			if (removed == 0) {
				return;
			}

			queueEmpty = queuedHubUrls.empty();
		}

		if (queueEmpty) {
			fire(SearchInstanceListener::HubSearchSent(), currentSearchToken, searchesSent);
		}
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