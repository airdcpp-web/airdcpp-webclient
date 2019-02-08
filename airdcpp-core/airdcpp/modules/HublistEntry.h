/*
* Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_HUBLISTENTRY_H_
#define DCPLUSPLUS_DCPP_HUBLISTENTRY_H_

#include <airdcpp/typedefs.h>

#include <airdcpp/GetSet.h>
#include <airdcpp/Util.h>

namespace dcpp {

	using std::string;

	class HublistEntry {
	public:
		typedef vector<HublistEntry> List;

		HublistEntry(const string& aName, const string& aServer, const string& aDescription, const string& aUsers) noexcept :
			name(aName), server(aServer), description(aDescription), users(Util::toInt(aUsers)) { }

		HublistEntry(const string& aName, const string& aServer, const string& aDescription, const string& aUsers, const string& aCountry,
			const string& aShared, const string& aMinShare, const string& aMinSlots, const string& aMaxHubs, const string& aMaxUsers,
			const string& aReliability, const string& aRating) :

			name(aName), server(aServer), description(aDescription), country(aCountry),
			rating(aRating), reliability((float)(Util::toFloat(aReliability) / 100.0)), shared(Util::toInt64(aShared)), minShare(Util::toInt64(aMinShare)),
			users(Util::toInt(aUsers)), minSlots(Util::toInt(aMinSlots)), maxHubs(Util::toInt(aMaxHubs)), maxUsers(Util::toInt(aMaxUsers))
		{

		}

		HublistEntry() { }

		~HublistEntry() { }

		GETSET(string, name, Name);
		GETSET(string, server, Server);
		GETSET(string, description, Description);
		GETSET(string, country, Country);
		GETSET(string, rating, Rating);
		IGETSET(float, reliability, Reliability, 0.0);
		IGETSET(int64_t, shared, Shared, 0);
		IGETSET(int64_t, minShare, MinShare, 0);
		IGETSET(int, users, Users, 0);
		IGETSET(int, minSlots, MinSlots, 0);
		IGETSET(int, maxHubs, MaxHubs, 0);
		IGETSET(int, maxUsers, MaxUsers, 0);
	};
}

#endif /*DCPLUSPLUS_DCPP_HUBLISTENTRY_H_*/
