/*
* Copyright (C) 2011-2024 AirDC++ Project
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
#include <airdcpp/FloodCounter.h>

#include <airdcpp/TimerManager.h>

namespace dcpp {
	FloodCounter::FloodCounter(int aPeriod) : floodPeriod(aPeriod) {

	}

	FloodCounter::FloodResult FloodCounter::handleRequest(const string& aIp, const FloodLimits& aLimits) noexcept {
		auto result = getFloodStatus(aIp, aLimits);

		addRequest(aIp);

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

	void FloodCounter::addRequest(const string& aIp) noexcept {
		Lock l(cs);
		floodIps.emplace(aIp, GET_TICK());
	}

	void FloodCounter::prune() noexcept {
		auto tick = GET_TICK();

		if (floodIps.empty()) {
			return;
		}

		std::erase_if(floodIps, [this, tick](const auto& ipTimePair) {
			if (static_cast<uint64_t>(ipTimePair.second + (floodPeriod * 1000)) < tick) {
				dcdebug("Removing an expired flood attempt from IP %s\n", ipTimePair.first.c_str());
				return true;
			}

			return false;
		});
	}
}
