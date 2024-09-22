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


#ifndef DCPLUSPLUS_WEBSERVER_WEBSERVERMANAGER_LISTENER_H
#define DCPLUSPLUS_WEBSERVER_WEBSERVERMANAGER_LISTENER_H

#include "forward.h"
#include "stdinc.h"

#include <airdcpp/forward.h>

namespace webserver {
	enum class TransportType {
		TYPE_SOCKET, TYPE_HTTP_API, TYPE_HTTP_FILE
	};

	enum class Direction {
		INCOMING, OUTGOING
	};

	class WebServerManagerListener {
	public:
		virtual ~WebServerManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> Started;
		typedef X<1> Stopping;
		typedef X<2> Stopped;

		typedef X<3> LoadSettings;
		typedef X<4> LoadLegacySettings;
		typedef X<5> SaveSettings;

		typedef X<6> Data;


		virtual void on(Started) noexcept { }
		virtual void on(Stopping) noexcept { }
		virtual void on(Stopped) noexcept { }

		virtual void on(LoadLegacySettings, SimpleXML&) noexcept { }
		virtual void on(LoadSettings, const MessageCallback&) noexcept { }
		virtual void on(SaveSettings, const MessageCallback&) noexcept { }

		virtual void on(Data, const string& /*aData*/, TransportType, Direction, const string& /*aIP*/) noexcept { }
	};

}

#endif // !defined(DCPLUSPLUS_DCPP_WEBSERVER_LISTENER_H)