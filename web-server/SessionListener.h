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


#ifndef DCPLUSPLUS_DCPP_SESSIONLISTENER_H
#define DCPLUSPLUS_DCPP_SESSIONLISTENER_H

#include <web-server/stdinc.h>

namespace webserver {

	class SessionListener {
	public:
		virtual ~SessionListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> SocketConnected;
		typedef X<1> SocketDisconnected;

		virtual void on(SocketConnected, const WebSocketPtr&) noexcept { }
		virtual void on(SocketDisconnected) noexcept { }
	};

}

#endif // !defined(DCPLUSPLUS_DCPP_SESSIONLISTENER_H)