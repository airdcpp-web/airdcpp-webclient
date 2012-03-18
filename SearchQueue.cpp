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

#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>

namespace dcpp {

using boost::range::for_each;
	
SearchQueue::SearchQueue(uint32_t aInterval) 
	: lastSearchTime(0), minInterval(aInterval)
{
	nextInterval = 10*1000;
}

uint64_t SearchQueue::add(Search& s)
{
	dcassert(s.owners.size() == 1);
	uint32_t x = 0;
	bool add = true;

	Lock l(cs);

	auto i = searchQueue.begin();
	for (;;) {
		if (i == searchQueue.end())
			break;
		if(s.type < i->type) {
			//we found our place :}
			if((*i) == s) {
				//replace the lower prio item with this one, move the owners from the old search
				//boost::for_each(i->owners, [&s](Search& tmp) { s.owners.insert(tmp); });
				searchQueue.erase(i);
				prev(i);
			}
			break;
		} else if(s == *i) {
			//don't queue the same item twice
			void* aOwner = *s.owners.begin();
			i->owners.insert(aOwner);
			add = false;
			break;
		}

		x += i->getInterval();
		advance(i, 1);
	}

	if (add)
		searchQueue.insert(i, s);

	auto now = GET_TICK();
	if (x > 0) {
		//subtract ellapsed time for the first item from all items before this
		//LogManager::getInstance()->message("Time remaining in this queue: " + Util::toString(x - (getNextSearchTick() - now)) + " (next search " + Util::toString(getNextSearchTick())
		//	+ "ms, now " + Util::toString(now) + "ms, queueTime: " + Util::toString(x) + "ms)");

		if (getNextSearchTick() <= now) {
			//we have queue but the a search can be performed
			if (now - getNextSearchTick() > x) {
				//the last search was loong time ago, just return the queue time
				return x;
			} else {
				//subtract the time ellapsed since last search from the queue time
				return x - (now - getNextSearchTick());
			}
		} else {
			//we have queue and even waiting time for the next search
			return x - (getNextSearchTick() - now);
		}
	} else {
		//empty queue
		if (getNextSearchTick() <= now)
			return 0;

		//we still need to wait after the previous search, subract the waiting time from the interval of this item
		return s.getInterval() - (getNextSearchTick() - now);
	}
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
			lastSearchTime = GET_TICK();
			nextInterval = minInterval;
			if(!searchQueue.empty()) {
				nextInterval = max(searchQueue.front().getInterval(), minInterval);
			}
			return true;
		}
	}

	return false;
}

bool SearchQueue::hasWaitingTime(uint64_t aTick) {
	return lastSearchTime + nextInterval > aTick;
}
/*
uint64_t SearchQueue::getSearchTime(const Search& s){
	Lock l(cs);

	//if(aOwner == 0) return 0xFFFFFFFF;

	uint32_t x = 0; //max(lastSearchTime, GET_TICK() - interval);

	for(auto i = searchQueue.begin(); i != searchQueue.end(); i++){
		if((*i) == s) {
			auto now = GET_TICK();
			if (x > 0) {
				//all items before this - ellapsed time for the first item
				return x - (getNextSearchTick() - now);
			} else {
				//empty queue but we still need to wait after the previous search
				return i->getInterval() - (getNextSearchTick() - now);
			}
		}
		x += i->getInterval();
	}

	return 0;
}
*/
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
