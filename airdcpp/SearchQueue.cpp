/* 
 * Copyright (C) 2003-2016 RevConnect, http://www.revconnect.com
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
#include "SearchQueue.h"

#include "TimerManager.h"

namespace dcpp {

using boost::range::for_each;
	
SearchQueue::SearchQueue() : lastSearchTick(GET_TICK()) {

}

SearchQueue::~SearchQueue() { }

int SearchQueue::getInterval(Priority aPriority) const noexcept {
	int ret = 0;
	switch(aPriority) {
		case Priority::HIGHEST:
		case Priority::HIGH: ret = 5000; break;
		case Priority::NORMAL: ret = 10000; break;
		case Priority::LOW: ret = 15000; break;
		default: ret = 20000; break;
	}
	return max(ret, minInterval);
}

void SearchQueue::clear() noexcept {
	Lock l(cs);
	searchQueue.clear();
}

uint64_t SearchQueue::getCurrentQueueTime() const noexcept {
	Lock l(cs);
	if (searchQueue.empty()) {
		return 0;
	}

	auto now = GET_TICK();
	auto nextAllowedSearch = getInterval(searchQueue.front()->priority) + lastSearchTick;
	if (nextAllowedSearch > now) {
		return nextAllowedSearch - now;
	}

	return 0;
}

int SearchQueue::getQueueSize() const noexcept {
	Lock l(cs);
	return static_cast<int>(searchQueue.size());
}

uint64_t SearchQueue::getTotalQueueTime() const noexcept {
	uint64_t queueTime = 0;
	bool first = true;

	{
		Lock l(cs);
		for (const auto& i : searchQueue) {
			queueTime += first ? getCurrentQueueTime() : getInterval(i->priority);
			first = false;
		}
	}

	return queueTime;
}

optional<uint64_t> SearchQueue::getQueueTime(const Search::CompareF& aCompareF) const noexcept {
	uint64_t queueTime = 0;
	bool first = true;

	{
		Lock l(cs);
		for (const auto& i: searchQueue) {
			queueTime += first ? getCurrentQueueTime() : getInterval(i->priority);
			if (aCompareF(i)) {
				return queueTime;
			}

			first = false;
		}
	}

	// Not found
	return boost::none;
}

#define MAX_QUEUE_MINUTES 20
optional<uint64_t> SearchQueue::maybeAdd(const SearchPtr& aSearch) noexcept {
	if (aSearch->owner) {
		cancelSearch(aSearch->owner);
	}

	// Prevent the queue from growing indefinitely because of background searches
	if (aSearch->priority < Priority::HIGH && getTotalQueueTime() > (MAX_QUEUE_MINUTES * 60 * 1000)) {
		return boost::none;
	}

	{
		Lock l(cs);
		auto pos = std::upper_bound(searchQueue.begin(), searchQueue.end(), aSearch);
		searchQueue.insert(pos, aSearch);
	}

	return getQueueTime(Search::ComparePtr(aSearch));
}

SearchPtr SearchQueue::maybePop() noexcept {
	if (getCurrentQueueTime() > 0) {
		return nullptr;
	}
	
	{
		Lock l(cs);
		if(!searchQueue.empty()){
			auto s = move(searchQueue.front());
			searchQueue.pop_front();
			lastSearchTick = GET_TICK();
			return s;
		}
	}

	return nullptr;
}

bool SearchQueue::cancelSearch(const void* aOwner) noexcept {
	dcassert(aOwner);

	Lock l(cs);
	auto i = find_if(searchQueue.begin(), searchQueue.end(), Search::CompareOwner(aOwner));
	if (i != searchQueue.end()) {
		searchQueue.erase(i);
		return true;
	}

	return false;
}

}
