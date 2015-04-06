/*
* Copyright (C) 2012-2015 AirDC++ Project
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


#ifndef PRIVATE_CHAT_LISTENER_H
#define PRIVATE_CHAT_LISTENER_H

#include "forward.h"
#include "noexcept.h"

namespace dcpp {

	class PrivateChatListener {
	public:
		virtual ~PrivateChatListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> PrivateMessage;
		typedef X<1> StatusMessage;
		typedef X<2> Activate;
		typedef X<3> Close;
		typedef X<4> UserUpdated;
		typedef X<5> CCPMStatusChanged;
		typedef X<6> PMStatus;


		virtual void on(PrivateMessage, const ChatMessage&) noexcept{}
		virtual void on(StatusMessage, const string&, uint8_t) noexcept{}
		virtual void on(Activate, const string&, Client*) noexcept{}
		virtual void on(Close) noexcept{}
		virtual void on(UserUpdated) noexcept{}
		virtual void on(CCPMStatusChanged, const string&) noexcept{}
		virtual void on(PMStatus, uint8_t) noexcept{}
	};

} // namespace dcpp

#endif // !defined(MESSAGE_MANAGER_LISTENER_H)