/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_HUBENTRY_H_
#define DCPLUSPLUS_DCPP_HUBENTRY_H_

#include <string>

#include "GetSet.h"
#include "ShareProfile.h"
#include "Util.h"

#include "HubSettings.h"

namespace dcpp {

using std::string;

class HubEntry {
public:
	typedef vector<HubEntry> List;
	
	HubEntry(const string& aName, const string& aServer, const string& aDescription, const string& aUsers) noexcept : 
	name(aName), server(aServer), description(aDescription), country(Util::emptyString), 
	rating(Util::emptyString), reliability(0.0), shared(0), minShare(0), users(Util::toInt(aUsers)), minSlots(0), maxHubs(0), maxUsers(0) { }

	HubEntry(const string& aName, const string& aServer, const string& aDescription, const string& aUsers, const string& aCountry,
		const string& aShared, const string& aMinShare, const string& aMinSlots, const string& aMaxHubs, const string& aMaxUsers,
		const string& aReliability, const string& aRating) :
		
		name(aName), server(aServer), description(aDescription), country(aCountry),
		rating(aRating), reliability((float)(Util::toFloat(aReliability) / 100.0)), shared(Util::toInt64(aShared)), minShare(Util::toInt64(aMinShare)),
		users(Util::toInt(aUsers)), minSlots(Util::toInt(aMinSlots)), maxHubs(Util::toInt(aMaxHubs)), maxUsers(Util::toInt(aMaxUsers))
	{

	}

	HubEntry() { }

	~HubEntry() { }

	GETSET(string, name, Name);
	GETSET(string, server, Server);
	GETSET(string, description, Description);
	GETSET(string, country, Country);
	GETSET(string, rating, Rating);
	GETSET(float, reliability, Reliability);
	GETSET(int64_t, shared, Shared);
	GETSET(int64_t, minShare, MinShare);
	GETSET(int, users, Users);
	GETSET(int, minSlots, MinSlots);
	GETSET(int, maxHubs, MaxHubs);
	GETSET(int, maxUsers, MaxUsers);
};

class ShareProfile;
class FavoriteHubEntry : public HubSettings, public intrusive_ptr_base<FavoriteHubEntry> {
public:
	typedef FavoriteHubEntry* Ptr;
	typedef vector<Ptr> List;
	typedef List::const_iterator Iter;

	FavoriteHubEntry() noexcept;
	FavoriteHubEntry(const HubEntry& rhs) noexcept;

	~FavoriteHubEntry() noexcept { }

	// url, is blocked
	typedef pair<string, bool> ServerBoolPair;
	typedef vector<ServerBoolPair> ServerList;

	GETSET(string, name, Name);
	GETSET(ServerList, servers, Servers);
	GETSET(string, description, Description);
	GETSET(string, password, Password);
	GETSET(string, headerOrder, HeaderOrder);
	GETSET(string, headerWidths, HeaderWidths);
	GETSET(string, headerVisible, HeaderVisible);
	GETSET(uint16_t, bottom, Bottom);
	GETSET(uint16_t, top, Top);
	GETSET(uint16_t, left, Left);
	GETSET(uint16_t, right, Right);
	GETSET(bool, connect, Connect);
	GETSET(int, chatusersplit, ChatUserSplit);
	GETSET(bool, stealth, Stealth);
	GETSET(bool, userliststate, UserListState);
	GETSET(bool, favnoPM, FavNoPM);
	GETSET(string, group, Group);	
	GETSET(ShareProfilePtr, shareProfile, ShareProfile);
	GETSET(ProfileToken, token, Token);

	void setServerStr(const string& aServers);
	bool isAdcHub() const;
	void addFailOvers(StringList&& addresses);
	void validateFailOvers();
	void blockFailOver(const string& aServer);
	string getServerStr() const;
};

class RecentHubEntry : public intrusive_ptr_base<RecentHubEntry> {
public:
	RecentHubEntry(const string& aUrl) : server(aUrl), name("*"), description("*"), shared("*"), users("*") {

	}

	~RecentHubEntry() noexcept { }	
	
	GETSET(string, server, Server);
	GETSET(string, name, Name);
	GETSET(string, description, Description);
	GETSET(string, shared, Shared);
	GETSET(string, users, Users);
};

}

#endif /*HUBENTRY_H_*/
