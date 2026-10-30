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
#include <airdcpp/core/timer/TimerManager.h>

#ifndef _WIN32
#include <sys/time.h>
#endif


namespace dcpp {

using namespace boost::posix_time;

TimerManager::TimerManager() {

}

TimerManager::~TimerManager() {
	dcassert(listeners.size() == 0);
}

void TimerManager::shutdown() {
	{
		std::lock_guard<std::mutex> l(mtx);
		stopping = true;
	}

	cv.notify_all();
	join();
}

int TimerManager::run() {
	auto nextMinTick = getTick() + 60 * 1000;

	for (;;) {
		{
			// Scoped so the listeners below run with the lock released.
			std::unique_lock<std::mutex> l(mtx);
			if (cv.wait_for(l, std::chrono::seconds(1), [this] { return stopping; })) {
				break;
			}
		}

		const auto t = getTick();
		fire(TimerManagerListener::Second(), t);

		if (nextMinTick <= t) {
			nextMinTick = t + 60 * 1000;
			fire(TimerManagerListener::Minute(), t);
		}
	}

	dcdebug("TimerManager done\n");
	return 0;
}

uint64_t TimerManager::getTick() {
	using clock = std::chrono::steady_clock;
	static const auto start = clock::now();
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count());
}

time_t TimerManager::getTime() {
	return (time_t)time(NULL); 
}

time_t TimerManager::getStartTime() noexcept {
	return getTime() - getUptime();
}

time_t TimerManager::getUptime() noexcept {
	return getTick() / 1000;
}

} // namespace dcpp