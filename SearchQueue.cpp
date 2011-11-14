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

#include "stdinc.h"
#include "SearchQueue.h"

#include "TimerManager.h"
#include "QueueManager.h"
#include "SearchManager.h"

namespace dcpp {

bool SearchQueue::add(const Search& s)
{
	dcassert(s.owners.size() == 1);
	dcassert(interval >= 4000); // min interval is 5 seconds

	Lock l(cs);
	
	if (s.type == Search::MANUAL) {
		nextInterval = min(interval, nextInterval);
	}

	// check dupe
	auto i = find(searchQueue.begin(), searchQueue.end(), s);
	if (i != searchQueue.end()) {
		void* aOwner = *s.owners.begin();
		i->owners.insert(aOwner);
			
		// erase the old search if it has lower prio
		if(s.type > i->type) {
			searchQueue.erase(i);
		} else {
			return false;
		}
	}

	//insert based on the type
	searchQueue.insert(upper_bound(searchQueue.begin(), searchQueue.end(), s), s);
	return true;
}

bool SearchQueue::pop(Search& s)
{
	uint64_t now = GET_TICK();
	if(now <= lastSearchTime + nextInterval) 
		return false;
	
	{
		Lock l(cs);
		if(!searchQueue.empty()){
			s = searchQueue.front();
			searchQueue.pop_front();
			lastSearchTime = now;
			nextInterval = interval;
			if(!searchQueue.empty()) { 
				Search::searchType type = searchQueue.front().type;
				if (type == Search::ALT_AUTO) {
					nextInterval = max(interval, (uint32_t)15000);
				} else if (type == Search::AUTO_SEARCH) {
					nextInterval = max(interval, (uint32_t)15000);
				}
			}
			return true;
		}
	}

	return false;
}

uint64_t SearchQueue::getSearchTime(void* aOwner){
	Lock l(cs);

	if(aOwner == 0) return 0xFFFFFFFF;

	uint64_t x = max(lastSearchTime, GET_TICK() - interval);

	for(auto i = searchQueue.begin(); i != searchQueue.end(); i++){
		x += interval; //it's fine if the time is only being counted correctly for manual searches

		if(i->owners.count(aOwner))
			return x;
		else if(i->owners.empty())
			break;
	}

	return 0;
}
	
bool SearchQueue::cancelSearch(void* aOwner){
	dcassert(aOwner);

	Lock l(cs);
	for(auto i = searchQueue.begin(); i != searchQueue.end(); i++){
		if(i->owners.count(aOwner)){
			i->owners.erase(aOwner);
			if(i->owners.empty())
				searchQueue.erase(i);
			return true;
		}
	}
	return false;
}

}
