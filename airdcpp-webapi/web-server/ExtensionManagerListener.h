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


#ifndef DCPLUSPLUS_WEBSERVER_EXTENSIONMANAGER_LISTENER_H
#define DCPLUSPLUS_WEBSERVER_EXTENSIONMANAGER_LISTENER_H

#include "forward.h"

namespace webserver {
	class ExtensionManagerListener {
	public:
		virtual ~ExtensionManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> ExtensionAdded;
		typedef X<1> ExtensionStateUpdated;
		typedef X<2> ExtensionRemoved;

		typedef X<3> InstallationStarted;
		typedef X<4> InstallationSucceeded;
		typedef X<5> InstallationFailed;

		typedef X<6> Started;
		typedef X<7> Stopped;

		virtual void on(ExtensionAdded, const ExtensionPtr&) noexcept { }
		virtual void on(ExtensionStateUpdated, const Extension*) noexcept { }
		virtual void on(ExtensionRemoved, const ExtensionPtr&) noexcept { }

		virtual void on(InstallationStarted, const string& /*installId*/) noexcept { }
		virtual void on(InstallationSucceeded, const string& /*installId*/, const ExtensionPtr&, bool /*updated*/) noexcept { }
		virtual void on(InstallationFailed, const string& /*installId*/, const string& /*error*/) noexcept { }

		virtual void on(Started) noexcept { }
		virtual void on(Stopped) noexcept { }
	};

}

#endif // !defined(DCPLUSPLUS_WEBSERVER_EXTENSIONMANAGER_LISTENER_H)