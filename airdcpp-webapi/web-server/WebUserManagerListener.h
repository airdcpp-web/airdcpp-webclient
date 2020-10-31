/*
* Copyright (C) 2012-2021 AirDC++ Project
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


#ifndef DCPLUSPLUS_DCPP_WEBUSER_LISTENER_H
#define DCPLUSPLUS_DCPP_WEBUSER_LISTENER_H

#include "stdinc.h"
#include <web-server/WebUser.h>

namespace webserver {
	class WebUserManagerListener {
	public:
		virtual ~WebUserManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> UserAdded;
		typedef X<1> UserUpdated;
		typedef X<2> UserRemoved;

		typedef X<3> SessionCreated;
		typedef X<4> SessionRemoved;

		virtual void on(UserAdded, const WebUserPtr&) noexcept { }
		virtual void on(UserUpdated, const WebUserPtr&) noexcept { }
		virtual void on(UserRemoved, const WebUserPtr&) noexcept { }

		virtual void on(SessionCreated, const SessionPtr&) noexcept { }
		virtual void on(SessionRemoved, const SessionPtr&, bool /*aTimedOut*/) noexcept { }
	};

}

#endif // !defined(DCPLUSPLUS_DCPP_WEBUSER_LISTENER_H)