/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_CLIENTLISTENER_H_
#define DCPLUSPLUS_DCPP_CLIENTLISTENER_H_

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
	typedef X<7> Disconnected;
	typedef X<8> GetPassword;
	typedef X<9> HubUpdated;
	typedef X<10> ChatMessage;
	typedef X<11> StatusMessage;
	typedef X<12> HubUserCommand;
	typedef X<13> HubFull;
	typedef X<19> SetActive;
	typedef X<20> Close;
	typedef X<21> MessagesRead;
	typedef X<22> MessagesCleared;
	typedef X<23> Redirected;
	typedef X<24> ConnectStateChanged;
	typedef X<25> KeyprintMismatch;
	typedef X<26> OutgoingSearch;
	typedef X<27> PrivateMessage;
	typedef X<28> ChatCommand;
	typedef X<29> SettingsUpdated;
	
	virtual void on(Connecting, const Client*) noexcept { }
	virtual void on(Connected, const Client*) noexcept { }
	virtual void on(UserConnected, const Client*, const OnlineUserPtr&) noexcept {}
	virtual void on(UserUpdated, const Client*, const OnlineUserPtr&) noexcept { }
	virtual void on(UsersUpdated, const Client*, const OnlineUserList&) noexcept { }
	virtual void on(UserRemoved, const Client*, const OnlineUserPtr&) noexcept { }
	virtual void on(Redirect, const Client*, const string&) noexcept { }
	virtual void on(Disconnected, const string&/*hubUrl*/, const string& /*aLine*/) noexcept { }
	virtual void on(GetPassword, const Client*) noexcept { }
	virtual void on(HubUpdated, const Client*) noexcept { }
	virtual void on(ChatMessage, const Client*, const ChatMessagePtr&) noexcept { }
	virtual void on(StatusMessage, const Client*, const LogMessagePtr&, const string&) noexcept { }
	virtual void on(HubUserCommand, const Client*, int, int, const string&, const string&) noexcept { }
	virtual void on(HubFull, const Client*) noexcept { }
	virtual void on(SetActive, const Client*) noexcept {}
	virtual void on(Close, const Client*) noexcept {}
	virtual void on(MessagesRead, const Client*) noexcept {}
	virtual void on(MessagesCleared, const Client*) noexcept {}
	virtual void on(Redirected, const string&/*old*/, const ClientPtr& /*new*/) noexcept {}
	virtual void on(ConnectStateChanged, const Client*, uint8_t) noexcept {}
	virtual void on(KeyprintMismatch, const Client*) noexcept {}
	virtual void on(OutgoingSearch, const Client*, const SearchPtr&) noexcept {}
	virtual void on(PrivateMessage, const Client*, const ChatMessagePtr&) noexcept { }
	virtual void on(ChatCommand, const Client*, const OutgoingChatMessage&) noexcept { }
	virtual void on(SettingsUpdated, const Client*) noexcept { }
};

} // namespace dcpp

#endif /*CLIENTLISTENER_H_*/
