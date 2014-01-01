/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef CLIENTLISTENER_H_
#define CLIENTLISTENER_H_

#include "typedefs.h"

namespace dcpp {

class ClientListener
{
public:
	virtual ~ClientListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> Connecting;
	typedef X<1> Connected;
	typedef X<2> UserConnected;
	typedef X<3> UserUpdated;
	typedef X<4> UsersUpdated;
	typedef X<5> UserRemoved;
	typedef X<6> Redirect;
	typedef X<7> Failed;
	typedef X<8> GetPassword;
	typedef X<9> HubUpdated;
	typedef X<10> Message;
	typedef X<11> StatusMessage;
	typedef X<12> HubUserCommand;
	typedef X<13> HubFull;
	typedef X<14> NickTaken;
	typedef X<15> SearchFlood;
	typedef X<16> NmdcSearch;
	typedef X<17> HubTopic;
	typedef X<18> AddLine;
	typedef X<19> SetActive;

	enum StatusFlags {
		FLAG_NORMAL = 0x00,
		FLAG_IS_SPAM = 0x01
	};
	
	virtual void on(Connecting, const Client*) noexcept { }
	virtual void on(Connected, const Client*) noexcept { }
	virtual void on(UserConnected, const Client*, const OnlineUserPtr&) noexcept {}
	virtual void on(UserUpdated, const Client*, const OnlineUserPtr&) noexcept { }
	virtual void on(UsersUpdated, const Client*, const OnlineUserList&) noexcept { }
	virtual void on(UserRemoved, const Client*, const OnlineUserPtr&) noexcept { }
	virtual void on(Redirect, const Client*, const string&) noexcept { }
	virtual void on(Failed, const string&/*hubUrl*/, const string& /*aLine*/) noexcept { }
	virtual void on(GetPassword, const Client*) noexcept { }
	virtual void on(HubUpdated, const Client*) noexcept { }
	virtual void on(Message, const Client*, const ChatMessage&) noexcept { }
	virtual void on(StatusMessage, const Client*, const string&, int = FLAG_NORMAL) noexcept { }
	virtual void on(HubUserCommand, const Client*, int, int, const string&, const string&) noexcept { }
	virtual void on(HubFull, const Client*) noexcept { }
	virtual void on(NickTaken, const Client*) noexcept { }
	virtual void on(SearchFlood, const Client*, const string&) noexcept { }
	virtual void on(NmdcSearch, Client*, const string&, int, int64_t, int, const string&, bool) noexcept { }
	virtual void on(HubTopic, const Client*, const string&) noexcept { }
	virtual void on(AddLine, const Client*, const string&) noexcept { }
	virtual void on(SetActive, const Client*) noexcept {}
};

} // namespace dcpp

#endif /*CLIENTLISTENER_H_*/
