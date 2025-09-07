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
#include <airdcpp/core/classes/FloodCounter.h>

#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/core/timer/TimerManager.h>
#include <airdcpp/util/Util.h>

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

	FloodCounter::FloodRate FloodCounter::getRate(const string& aRequester) const noexcept {
		Lock l(cs);
		auto range = floodIps.equal_range(aRequester);
		if (range.first == range.second) {
			return {
				0,
				0
			};
		}

		auto min = ranges::min_element(range | pair_to_range | views::values);
		auto max = ranges::max_element(range | pair_to_range | views::values);

		auto period = *max - *min;
		return {
			static_cast<int>(distance(range.first, range.second)),
			static_cast<int>(period),
		};
	}

	string FloodCounter::appendFloodRate(const string& aRequester, const string& aMessage, bool aSevere) const noexcept {
		auto rate = getRate(aRequester);

		auto toAppend = STRING_F(X_REQUESTS_SECONDS, rate.attempts % Util::toString(static_cast<double>(rate.periodMs) / 1000));
		if (aSevere) {
			toAppend += ", " + Text::toLower(STRING(SEVERE));
		}

		return aMessage + " (" + toAppend + ")";
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
				return true;
			}

			return false;
		});
	}
}
