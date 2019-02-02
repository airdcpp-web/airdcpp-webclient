/*
* Copyright (C) 2011-2019 AirDC++ Project
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
#include <random>

#include "SettingsManager.h"
#include "TimerManager.h"


namespace dcpp {

template<class ItemT>
class PrioritySearchQueue {
	typedef vector<double> ProbabilityList;
public:
	PrioritySearchQueue(SettingsManager::IntSetting aMinInterval) : minIntervalSetting(aMinInterval) {

	}

	void addSearchPrio(const ItemT& aItem) noexcept{
		if (aItem->getPriority() < Priority::LOW) {
			return;
		}

		{
			auto& queue = getQueue(aItem);
			dcassert(find(queue.begin(), queue.end(), aItem) == queue.end());
			queue.push_back(aItem);
		}

		recalculateSearchTimes(aItem->isRecent(), false);
	}

	void removeSearchPrio(const ItemT& aItem) noexcept{
		if (aItem->getPriority() < Priority::LOW) {
			return;
		}

		auto& queue = getQueue(aItem);
		queue.erase(remove(queue.begin(), queue.end(), aItem), queue.end());
	}

	// Get the next normal/recent item to search for and rotate the search queue
	// Recent state might change, recalculate next search for correct search queue
	ItemT maybePopSearchItem(uint64_t aTick, bool aIgnoreNextTick = false) noexcept{
		ItemT ret = nullptr;
		if (aTick >= nextSearchNormal || aIgnoreNextTick) {
			ret = maybePopNormal();
			if (ret)
				recalculateSearchTimes(false, true, aTick);
		}

		if (!ret && (aTick >= nextSearchRecent || aIgnoreNextTick)) {
			ret = maybePopRecent();
			if (ret)
				recalculateSearchTimes(true, true, aTick);
		}

		return ret;
	}

	uint64_t getNextSearchNormal() const noexcept {
		return nextSearchNormal;
	}

	uint64_t getNextSearchRecent() const noexcept {
		return nextSearchRecent;
	}

	// Recalculates the next normal/recent search tick
	// Use aForce if the previously set next search tick can be postponed
	// Returns the calculated search tick
	// NOTE: remember read lock
	uint64_t recalculateSearchTimes(bool aRecent, bool aForce, uint64_t aTick = GET_TICK()) noexcept {
		auto& nextSearch = aRecent ? nextSearchRecent : nextSearchNormal;
		const auto minIntervalMinutes = SettingsManager::getInstance()->get(minIntervalSetting);

		int calculatedIntervalMinutes = 0;
		if (aRecent) {
			auto itemCount = getValidItemCountRecent();
			if (itemCount == 0) {
				nextSearch = 0;
				return nextSearch;
			}

			calculatedIntervalMinutes = max(15 / itemCount, minIntervalMinutes);
		} else {
			auto itemCount = getValidItemCountNormal();
			if (itemCount == 0) {
				nextSearch = 0;
				return nextSearch;
			}

			calculatedIntervalMinutes = max(60 / itemCount, minIntervalMinutes);
		}

		if (!aForce && nextSearch > 0) {
			// Never postpone the next search when adding new items/changing priorities
			nextSearch = min(nextSearch, aTick + (calculatedIntervalMinutes * 60 * 1000));
		} else {
			nextSearch = aTick + (calculatedIntervalMinutes * 60 * 1000);
		}

		return nextSearch;
	}
private:
	struct AllowSearch {
		bool operator()(const ItemT& aItem) const noexcept { return aItem->allowAutoSearch(); }
	};

	ItemT maybePopRecent() noexcept{
		for (auto i = 0; i < static_cast<int>(recentSearchQueue.size()); i++) {
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
			}
		}

		return nullptr;
	}

	ItemT maybePopNormal() noexcept{
		ProbabilityList probabilities;
		auto itemCount = getValidItemCountNormal(&probabilities);

		//do we have anything where to search from?
		if (itemCount == 0) {
			return nullptr;
		}

		auto dist = discrete_distribution<>(probabilities.begin(), probabilities.end());

		// Choose the search queue, can't be paused or lowest
		auto& sbq = prioSearchQueue[dist(gen) + static_cast<int>(Priority::LOW)];
		dcassert(!sbq.empty());

		// Find the first item from the search queue that can be searched for
		auto s = find_if(sbq.begin(), sbq.end(), AllowSearch());
		if (s != sbq.end()) {
			auto item = *s;
			//move to the back
			sbq.erase(s);
			sbq.push_back(item);
			return item;
		}

		return nullptr;
	}

	int getValidItemCountRecent() const noexcept {
		return static_cast<int>(count_if(recentSearchQueue.begin(), recentSearchQueue.end(), AllowSearch()));
	}

	mt19937 gen;

	int getValidItemCountNormal(ProbabilityList* probabilities_ = nullptr) const noexcept{
		int itemCount = 0;
		int p = static_cast<int>(Priority::LOW);
		do {
			int dequeItems = static_cast<int>(count_if(prioSearchQueue[p].begin(), prioSearchQueue[p].end(), AllowSearch()));

			if (probabilities_) {
				(*probabilities_).push_back((p - 1) * dequeItems); //multiply with a priority factor to get bigger probability for items with higher priority
			}

			itemCount += dequeItems;
			p++;
		} while (p < static_cast<int>(Priority::LAST));

		return itemCount;
	}

	typedef deque<ItemT> QueueType;

	QueueType& getQueue(const ItemT& aItem) {
		return aItem->isRecent() ? recentSearchQueue : prioSearchQueue[static_cast<int>(aItem->getPriority())];
	}

	// Search items by priority (low-highest)
	QueueType prioSearchQueue[static_cast<int>(Priority::LAST)];
	QueueType recentSearchQueue;

	// Next normal search tick
	uint64_t nextSearchNormal = GET_TICK() + (90 * 1000);

	// Next recent search tick
	uint64_t nextSearchRecent = GET_TICK() + (30 * 1000);

	const SettingsManager::IntSetting minIntervalSetting;
};

}

#endif