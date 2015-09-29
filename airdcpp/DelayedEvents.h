/*
 * Copyright (C) 2011-2015 AirDC++ Project
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

/* This allows scheduling events that will most likely happen sequently in a short time period and only executes the latest one. */

#ifndef DCPLUSPLUS_DCPP_DELAYEDEVENTS_H
#define DCPLUSPLUS_DCPP_DELAYEDEVENTS_H

#include "TimerManager.h"

namespace dcpp {

typedef std::function<void ()> DelayedF;
struct DelayTask {
	DelayTask(DelayedF aF, uint64_t aRunTick) : runTick(aRunTick), f(aF) { }
	uint64_t runTick;
	DelayedF f;
};

template<class T>
class DelayedEvents : private TimerManagerListener {
public:
	typedef unordered_map<T, unique_ptr<DelayTask>> List;

	DelayedEvents() { 
		TimerManager::getInstance()->addListener(this);
	}

	~DelayedEvents() {
		TimerManager::getInstance()->removeListener(this);
		clear();
	}

	bool runTask(const T& aKey) {
		Lock l(cs);
		auto i = eventList.find(aKey);
		if (i != eventList.end()) {
			i->second.get()->f();
			eventList.erase(i);
			return true;
		}
		return false;
	}

	void on(TimerManagerListener::Second, uint64_t aTick) noexcept {
		Lock l(cs);
		for (auto i = eventList.begin(); i != eventList.end();) {
			if (aTick > i->second.get()->runTick) {
				i->second.get()->f();
				eventList.erase(i);
				i = eventList.begin();
			} else {
				i++;
			}
		}
	}

	void addEvent(const T& aKey, DelayedF f, uint64_t aDelayTicks) {
		Lock l(cs);

		auto i = eventList.find(aKey);
		if (i != eventList.end()) {
			i->second.get()->runTick = GET_TICK() + aDelayTicks;
			return;
		}

		eventList.emplace(aKey, unique_ptr<DelayTask>(new DelayTask(f, GET_TICK() + aDelayTicks)));
	}

	void clear() {
		List tmp;
		tmp = move(eventList);
	}

	bool removeEvent(const T& aKey) {
		Lock l(cs);
		auto i = eventList.find(aKey);
		if (i != eventList.end()) {
			eventList.erase(i);
			return true;
		}

		return false;
	}
private:

	CriticalSection cs;
	List eventList;
};

} // namespace dcpp

#endif
