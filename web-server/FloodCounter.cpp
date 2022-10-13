/*
* Copyright (C) 2011-2022 AirDC++ Project
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
#include <web-server/FloodCounter.h>

#include <airdcpp/TimerManager.h>

namespace webserver {
	FloodCounter::FloodCounter(int aCount, int aPeriod) : floodCount(aCount), floodPeriod(aPeriod) {

	}

	bool FloodCounter::checkFlood(const string& aIp) const noexcept {
		RLock l(cs);
		auto tmp = floodIps.count(aIp);
		return static_cast<int>(floodIps.count(aIp)) < floodCount;
	}

	void FloodCounter::addAttempt(const string& aIp) noexcept {
		WLock l(cs);
		floodIps.emplace(aIp, GET_TICK());
	}

	void FloodCounter::prune() noexcept {
		{
			RLock l(cs);
			if (floodIps.empty()) {
				return;
			}
		}

		auto tick = GET_TICK();

		{
			WLock l(cs);
			for (auto i = floodIps.begin(); i != floodIps.end(); ) {
				if (static_cast<uint64_t>(i->second + (floodPeriod * 1000)) < tick) {
					dcdebug("Removing an expired flood attempt from IP %s\n", i->first.c_str());
					i = floodIps.erase(i);
				} else {
					i++;
				}
			}
		}
	}
}
