/*
* Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"

#include "DirectSearch.h"

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/search/SearchManager.h>


namespace dcpp {
	DirectSearch::DirectSearch(const HintedUser& aUser, const SearchPtr& aSearch, uint64_t aNoResultTimeout) : noResultTimeout(aNoResultTimeout) {
		ClientManager::getInstance()->addListener(this);
		SearchManager::getInstance()->addListener(this);

		searchToken = aSearch->token;

		maxResultCount = aSearch->maxResults;

		string error;
		ClientManager::getInstance()->directSearchHooked(aUser, aSearch, error);
	}

	DirectSearch::~DirectSearch() {
		removeListeners();
	}

	void DirectSearch::on(SearchManagerListener::SR, const SearchResultPtr& aSR) noexcept {
		if (compare(aSR->getSearchToken(), searchToken) != 0) {
			return;
		}

		lastResult = GET_TICK();

		results.push_back(aSR);
		curResultCount++;
	}

	void DirectSearch::on(ClientManagerListener::DirectSearchEnd, const string& aToken, int aResultCount) noexcept {
		if (compare(aToken, searchToken) != 0) {
			return;
		}

		// Are there still results to be received?
		maxResultCount = aResultCount;

		if (aResultCount == curResultCount)
			removeListeners();
	}

	bool DirectSearch::finished() noexcept {
		auto tick = GET_TICK();

		// No results and timeout reached?
		if (curResultCount == 0 && started + noResultTimeout < tick) {
			timedOut = true;
			removeListeners();
			return true;
		} 
			
		// Use a shorter timeout after we have received some results
		// in case the client doesn't support sending a reply message
		// This will also finish if all results are received
		if ((lastResult > 0 && lastResult + 1000 < tick) || maxResultCount == curResultCount) {
			removeListeners();
			return true;
		}

		return false;
	}

	void DirectSearch::getAdcPaths(OrderedStringSet& paths_, bool aParents) const noexcept {
		for (const auto& sr : results) {
			auto path = sr->getAdcPath();
			if (aParents && !sr->getUser().user->isSet(User::ASCH)) {
				//convert the regular search results
				path = sr->getType() == SearchResult::Type::DIRECTORY ? PathUtil::getAdcParentDir(sr->getAdcPath()) : sr->getAdcFilePath();
			}

			paths_.insert(path);
		}
	}

	void DirectSearch::removeListeners() noexcept {
		ClientManager::getInstance()->removeListener(this);
		SearchManager::getInstance()->removeListener(this);
	}
} // namespace dcpp
