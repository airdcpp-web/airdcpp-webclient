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

#ifndef DCPLUSPLUS_FLOODCOUNTER_H
#define DCPLUSPLUS_FLOODCOUNTER_H

#include <airdcpp/typedefs.h>

#include <airdcpp/CriticalSection.h>

namespace dcpp {

	// Class to control IP flood 
	class FloodCounter {
	public:
		enum class FloodType {
			OK,
			FLOOD_MINOR,
			FLOOD_SEVERE,
		};

		struct FloodResult {
			FloodType type;
			bool hitLimit;
		};

		struct FloodLimits {
			int minorCount;
			int severeCount;
		};

		FloodCounter(int aPeriod);

		// Check and count a request
		FloodResult handleRequest(const string& aRequester, const FloodLimits& aLimits) noexcept;

		// Check the current flood status for a requester (call addAttempt separately if flood control needs to be applied)
		FloodResult getFloodStatus(const string& aRequester, const FloodLimits& aLimits) noexcept;

		void addRequest(const string& aRequester) noexcept;
	protected:
		typedef multimap<string, time_t> IpMap;

		IpMap floodIps;

		mutable CriticalSection cs;

		const int floodPeriod;

		// Remove expired flood entries
		void prune() noexcept;
	};
}

#endif // !defined(DCPLUSPLUS_FLOODCOUNTER_H)