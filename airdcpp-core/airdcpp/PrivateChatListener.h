/*
* Copyright (C) 2012-2024 AirDC++ Project
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


#ifndef DCPP_PRIVATE_CHAT_LISTENER_H
#define DCPP_PRIVATE_CHAT_LISTENER_H

#include "forward.h"

namespace dcpp {

	class PrivateChatListener {
	public:
		virtual ~PrivateChatListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> PrivateMessage;
		typedef X<1> Close;
		typedef X<2> UserUpdated;
		typedef X<3> PMStatus;
		typedef X<4> StatusMessage;
		typedef X<5> CCPMStatusUpdated;
		typedef X<6> MessagesRead;
		typedef X<7> MessagesCleared;
		typedef X<8> ChatCommand;

		virtual void on(PrivateMessage, PrivateChat*, const ChatMessagePtr&) noexcept{}
		virtual void on(Close, PrivateChat*) noexcept{}
		virtual void on(UserUpdated, PrivateChat*) noexcept{}
		virtual void on(PMStatus, PrivateChat*, uint8_t) noexcept{}
		virtual void on(StatusMessage, PrivateChat*, const LogMessagePtr&, const string&) noexcept{}
		virtual void on(CCPMStatusUpdated, PrivateChat*) noexcept{}
		virtual void on(MessagesRead, PrivateChat*) noexcept {}
		virtual void on(MessagesCleared, PrivateChat*) noexcept {}
		virtual void on(ChatCommand, PrivateChat*, const OutgoingChatMessage&) noexcept {}
	};

} // namespace dcpp

#endif // !defined(DCPP_PRIVATE_CHAT_LISTENER_H)