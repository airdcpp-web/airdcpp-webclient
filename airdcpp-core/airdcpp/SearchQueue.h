/* 
 * Copyright (C) 2003-2017 RevConnect, http://www.revconnect.com
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

#ifndef DCPLUSPLUS_DCPP_SEARCHQUEUE_H
#define DCPLUSPLUS_DCPP_SEARCHQUEUE_H

#include "CriticalSection.h"
#include "GetSet.h"
#include "Search.h"

namespace dcpp {

class SearchQueue
{
public:
	SearchQueue();
	~SearchQueue();

	// Queues a new search, removes all possible existing items from the same owner
	// none is returned if the search queue is currently too long
	uint64_t add(const SearchPtr& s) noexcept;

	// Pops the next search item if one is available and it's allowed by the search intervals
	SearchPtr maybePop() noexcept;
	
	void clear() noexcept;
	bool cancelSearch(const void* aOwner) noexcept;

	// Interval defined by the client (settings or fav hub interval)
	IGETSET(int, minInterval, MinInterval, 5000);

	optional<uint64_t> getQueueTime(const Search::CompareF& aCompareF) const noexcept;
	uint64_t getTotalQueueTime() const noexcept;
	uint64_t getCurrentQueueTime() const noexcept;
	int getQueueSize() const noexcept;
	bool hasOverflow() const noexcept;
private:
	int getInterval(Priority aPriority) const noexcept;

	uint64_t lastSearchTick;
	deque<SearchPtr> searchQueue;
	mutable CriticalSection cs;
};

}

#endif // !defined(DCPLUSPLUS_DCPP_SEARCHQUEUE_H)