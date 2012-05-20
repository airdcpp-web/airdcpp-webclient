/* 
 * Copyright (C) 2003-2006 RevConnect, http://www.revconnect.com
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

#include "MerkleTree.h"

namespace dcpp {

struct Search
{
	enum searchType {
		MANUAL,
		ALT,
		ALT_AUTO,
		AUTO_SEARCH,
	};

	int32_t		sizeType;
	int64_t		size;
	int32_t		fileType;
	string		query;
	string		token;
	StringList	exts;
	set<void*>	owners;
	searchType	type;
	
	bool operator==(const Search& rhs) const {
		 return this->sizeType == rhs.sizeType && 
		 		this->size == rhs.size && 
		 		this->fileType == rhs.fileType && 
		 		this->query == rhs.query;
	}

	bool operator<(const Search& rhs) const {
		 return this->type < rhs.type;
	}

	uint32_t getInterval() { 
		switch(type) {
			case MANUAL: return 5000;
			case ALT: return 10000;
			case ALT_AUTO: return 20000;
			case AUTO_SEARCH: return 20000;
		}
		return 5000;
	}
};

class SearchQueue
{
public:
	
	SearchQueue(uint32_t aInterval = 0);

	uint64_t add(Search& s);
	bool pop(Search& s);
	
	void clear()
	{
		Lock l(cs);
		searchQueue.clear();
	}

	bool cancelSearch(void* aOwner);

	/* Interval defined by the client (settings or fav hub interval) */
	uint32_t minInterval;
	uint64_t getNextSearchTick() { return lastSearchTime+nextInterval; }
	bool hasWaitingTime(uint64_t aTick);
	uint64_t lastSearchTime;
private:
	deque<Search>	searchQueue;
	uint32_t		nextInterval;
	CriticalSection cs;
};

}
