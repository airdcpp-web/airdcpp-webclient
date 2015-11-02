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


#ifndef DCPLUSPLUS_DCPP_WEBSERVER_LISTENER_H
#define DCPLUSPLUS_DCPP_WEBSERVER_LISTENER_H

#include <web-server/stdinc.h>

namespace webserver {
	class WebServerManagerListener {
	public:
		virtual ~WebServerManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> Started;
		typedef X<1> Stopping;
		typedef X<2> Stopped;
		typedef X<3> LoadSettings;
		typedef X<4> SaveSettings;

		virtual void on(Started) noexcept { }
		virtual void on(Stopping) noexcept { }
		virtual void on(Stopped) noexcept { }
		virtual void on(LoadSettings, SimpleXML&) noexcept { }
		virtual void on(SaveSettings, SimpleXML&) noexcept { }
	};

}

#endif // !defined(DCPLUSPLUS_DCPP_WEBSERVER_LISTENER_H)