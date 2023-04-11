/*
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_CLIENT_MANAGER_LISTENER_H
#define DCPLUSPLUS_DCPP_CLIENT_MANAGER_LISTENER_H

#include "forward.h"
#include "OnlineUser.h"

namespace dcpp {

class ClientManagerListener {
public:
	virtual ~ClientManagerListener() { }
	template<int I>	struct X { enum { TYPE = I };  };
	
	typedef X<0> UserConnected;
	typedef X<1> UserUpdated;
	typedef X<2> UserDisconnected;

	typedef X<3> ClientCreated;
	typedef X<4> ClientConnected;
	typedef X<5> ClientUpdated;
	typedef X<6> ClientRedirected;
	typedef X<7> ClientDisconnected;
	typedef X<8> ClientRemoved;

	typedef X<9> IncomingSearch;
	typedef X<10> IncomingADCSearch;
	typedef X<11> DirectSearchEnd;
	typedef X<11> OutgoingSearch;
	typedef X<12> PrivateMessage;


	virtual void on(UserConnected, const OnlineUser&, bool /*was offline*/) noexcept { }
	virtual void on(UserDisconnected, const UserPtr&, bool /*went offline*/) noexcept { }
	virtual void on(UserUpdated, const OnlineUser&) noexcept { }

	virtual void on(ClientCreated, const ClientPtr&) noexcept {}
	virtual void on(ClientConnected, const ClientPtr&) noexcept { }
	virtual void on(ClientUpdated, const ClientPtr&) noexcept { }
	virtual void on(ClientRedirected, const ClientPtr& /*old*/, const ClientPtr& /*new*/) noexcept { }
	virtual void on(ClientDisconnected, const string&) noexcept { }
	virtual void on(ClientRemoved, const ClientPtr&) noexcept { }

	virtual void on(IncomingSearch, const string&) noexcept { }
	virtual void on(IncomingADCSearch, const AdcCommand&) noexcept { }
	virtual void on(DirectSearchEnd, const string& /*token*/, int /*resultcount*/) noexcept { }
	virtual void on(OutgoingSearch, const string&, const SearchPtr&) noexcept {}
	virtual void on(PrivateMessage, const ChatMessagePtr&) noexcept {}
};

} // namespace dcpp

#endif // !defined(CLIENT_MANAGER_LISTENER_H)