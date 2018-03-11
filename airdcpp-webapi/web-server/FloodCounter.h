/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_WEBSERVER_FLOODCOUNTER_H
#define DCPLUSPLUS_WEBSERVER_FLOODCOUNTER_H

#include "stdinc.h"

#include <airdcpp/CriticalSection.h>

namespace webserver {

	// Class to control IP flood 
	class FloodCounter {
	public:
		FloodCounter(int aCount, int aPeriod);

		bool checkFlood(const string& aIp) const noexcept;
		void addAttempt(const string& aIp) noexcept;

		// Remove expired flood entries
		// Must be called externally, optimally with an interval equal to floodPeriod
		void prune() noexcept;
	protected:
		//typedef deque<string> IpList;
		typedef multimap<string, time_t> IpMap;

		IpMap floodIps;

		mutable SharedMutex cs;

		const int floodPeriod;
		const int floodCount;
	};
}

#endif // !defined(DCPLUSPLUS_WEBSERVER_FLOODCOUNTER_H)