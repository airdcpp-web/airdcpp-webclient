/*
* Copyright (C) 2011-2023 AirDC++ Project
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
#include "FloodCounter.h"

#include "TimerManager.h"

namespace dcpp {
	FloodCounter::FloodCounter(int aPeriod) : floodPeriod(aPeriod) {

	}

	FloodCounter::FloodResult FloodCounter::handleRequest(const string& aIp, const FloodLimits& aLimits) noexcept {
		auto result = getFloodStatus(aIp, aLimits);

		addRequst(aIp);

		return result;
	}

	FloodCounter::FloodResult FloodCounter::getFloodStatus(const string& aIp, const FloodLimits& aLimits) noexcept {
		Lock l(cs);
		prune();

		auto count = static_cast<int>(floodIps.count(aIp));
		if (count >= aLimits.severeCount) {
			auto hitLimit = count == aLimits.severeCount;
			return {
				FloodType::FLOOD_SEVERE,
				hitLimit,
			};
		} else if (count >= aLimits.minorCount) {
			auto hitLimit = count == aLimits.minorCount;
			return {
				FloodType::FLOOD_MINOR,
				hitLimit,
			};
		}

		return {
			FloodType::OK,
			false,
		};
	}

	void FloodCounter::addRequst(const string& aIp) noexcept {
		Lock l(cs);
		floodIps.emplace(aIp, GET_TICK());
	}

	void FloodCounter::prune() noexcept {
		auto tick = GET_TICK();

		if (floodIps.empty()) {
			return;
		}

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
