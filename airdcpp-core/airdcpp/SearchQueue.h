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

#pragma once

#include "CriticalSection.h"
#include "GetSet.h"
#include "Search.h"

namespace dcpp {

class SearchQueue
{
public:
	
	SearchQueue();
	~SearchQueue();

	uint64_t add(const SearchPtr& s) noexcept;
	SearchPtr pop() noexcept;
	
	void clear() noexcept;
	bool cancelSearch(void* aOwner) noexcept;

	uint64_t getNextSearchTick() const noexcept;
	bool hasWaitingTime(uint64_t aTick) const noexcept;

	uint64_t lastSearchTime = 0;

	// Interval defined by the client (settings or fav hub interval)
	IGETSET(int, minInterval, MinInterval, 5000);
private:
	int getInterval(Search::Type aSearchType) const noexcept;

	deque<SearchPtr> searchQueue;
	int	nextInterval;
	CriticalSection cs;
};

}
