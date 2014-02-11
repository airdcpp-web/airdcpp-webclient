/*
* Copyright (C) 2011-2014 AirDC++ Project
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

#ifndef DCPLUSPLUS_PRIORITY_SEARCH_QUEUE_H
#define DCPLUSPLUS_PRIORITY_SEARCH_QUEUE_H

#include "stdinc.h"

// TODO: replace with STD when MSVC can initialize the distribution correctly
#include <boost/random/discrete_distribution.hpp>
#include <boost/random/mersenne_twister.hpp>

namespace dcpp {

template<class ItemT>
class PrioritySearchQueue {
	typedef vector<double> ProbabilityList;
public:
	void addSearchPrio(ItemT& aItem) noexcept{
		if (aItem->getPriority() < QueueItemBase::LOW) {
			return;
		}

		if (aItem->isRecent()) {
			dcassert(find(recentSearchQueue, aItem) == recentSearchQueue.end());
			recentSearchQueue.push_back(aItem);
			return;
		} else {
			dcassert(find(prioSearchQueue[aItem->getPriority()], aItem) == prioSearchQueue[aItem->getPriority()].end());
			prioSearchQueue[aItem->getPriority()].push_back(aItem);
		}
	}

	void removeSearchPrio(ItemT& aItem) noexcept{
		if (aItem->getPriority() < QueueItemBase::LOW) {
			return;
		}

		if (aItem->isRecent()) {
			auto i = find(recentSearchQueue, aItem);
			if (i != recentSearchQueue.end()) {
				recentSearchQueue.erase(i);
			}
		} else {
			auto i = find(prioSearchQueue[aItem->getPriority()], aItem);
			if (i != prioSearchQueue[aItem->getPriority()].end()) {
				prioSearchQueue[aItem->getPriority()].erase(i);
			}
		}
	}

	ItemT findSearchItem(uint64_t aTick, bool force = false) noexcept{
		ItemT ret = nullptr;
		if (aTick >= nextSearch || force) {
			ret = findNormal();
		}

		if (!ret && (aTick >= nextRecentSearch || force)) {
			ret = findRecent();
		}
		return ret;
	}

	int64_t recalculateSearchTimes(bool aRecent, bool isPrioChange) noexcept{
		if (!aRecent) {
			int prioItems = getPrioSum();
			int minInterval = SETTING(SEARCH_TIME);

			if (prioItems > 0) {
				minInterval = max(60 / prioItems, minInterval);
			}

			if (nextSearch > 0 && isPrioChange) {
				nextSearch = min(nextSearch, GET_TICK() + (minInterval * 60 * 1000));
			} else {
				nextSearch = GET_TICK() + (minInterval * 60 * 1000);
			}
			return nextSearch;
		} else {
			if (nextRecentSearch > 0 && isPrioChange) {
				nextRecentSearch = min(nextRecentSearch, GET_TICK() + getRecentIntervalMs());
			} else {
				nextRecentSearch = GET_TICK() + getRecentIntervalMs();
			}
			return nextRecentSearch;
		}
	}

	int getRecentIntervalMs() const noexcept{
		int recentItems = count_if(recentSearchQueue.begin(), recentSearchQueue.end(), [](const ItemT& aItem) { 
			return aItem->allowAutoSearch(); 
		});

		if (recentItems == 1) {
			return 15 * 60 * 1000;
		} else if (recentItems == 2) {
			return 8 * 60 * 1000;
		} else {
			return 5 * 60 * 1000;
		}
	}

private:

	ItemT findRecent() noexcept{
		if (recentSearchQueue.size() == 0) {
			return nullptr;
		}

		uint32_t count = 0;
		for (;;) {
			auto item = recentSearchQueue.front();
			recentSearchQueue.pop_front();

			//check if the item still belongs to here
			if (item->checkRecent()) {
				recentSearchQueue.push_back(item);
			} else {
				addSearchPrio(item);
			}

			if (item->allowAutoSearch()) {
				return item;
			} else if (count >= recentSearchQueue.size()) {
				break;
			}

			count++;
		}

		return nullptr;
	}

	ItemT findNormal() noexcept{
		ProbabilityList probabilities;
		int itemCount = getPrioSum(&probabilities);

		//do we have anything where to search from?
		if (itemCount == 0) {
			return nullptr;
		}

		auto dist = boost::random::discrete_distribution<>(probabilities);

		//choose the search queue, can't be paused or lowest
		auto& sbq = prioSearchQueue[dist(gen) + QueueItemBase::LOW];
		dcassert(!sbq.empty());

		//find the first item from the search queue that can be searched for
		auto s = find_if(sbq.begin(), sbq.end(), [](const ItemT& item) { return item->allowAutoSearch(); });
		if (s != sbq.end()) {
			auto item = *s;
			//move to the back
			sbq.erase(s);
			sbq.push_back(item);
			return item;
		}

		return nullptr;
	}

	boost::mt19937 gen;

	int getPrioSum(ProbabilityList* probabilities_ = nullptr) const noexcept{
		int itemCount = 0;
		int p = QueueItemBase::LOW;
		do {
			int dequeItems = count_if(prioSearchQueue[p].begin(), prioSearchQueue[p].end(), [](const ItemT& aItem) { 
				return aItem->allowAutoSearch(); 
			});

			if (probabilities_)
				(*probabilities_).push_back((p - 1)*dequeItems); //multiply with a priority factor to get bigger probability for items with higher priority
			itemCount += dequeItems;
			p++;
		} while (p < QueueItemBase::LAST);

		return itemCount;
	}

	/** Search items by priority (low-highest) */
	vector<ItemT> prioSearchQueue[QueueItemBase::LAST];
	deque<ItemT> recentSearchQueue;

	/** Next normal search */
	uint64_t nextSearch = 0;
	/** Next recent search */
	uint64_t nextRecentSearch = 0;

	//SettingsManager::IntSetting minSearchInterval;
};

}

#endif