/*
* Copyright (C) 2012-2017 AirDC++ Project
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


#ifndef DCPLUSPLUS_DCPP_EXTENSIONMANAGER_LISTENER_H
#define DCPLUSPLUS_DCPP_EXTENSIONMANAGER_LISTENER_H

#include <web-server/stdinc.h>

namespace webserver {
	class ExtensionManagerListener {
	public:
		virtual ~ExtensionManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> ExtensionAdded;
		typedef X<1> ExtensionRemoved;

		typedef X<2> InstallationStarted;
		typedef X<3> InstallationSucceeded;
		typedef X<4> InstallationFailed;

		virtual void on(ExtensionAdded, const ExtensionPtr&) noexcept { }
		virtual void on(ExtensionRemoved, const ExtensionPtr&) noexcept { }

		virtual void on(InstallationStarted, const string&) noexcept { }
		virtual void on(InstallationSucceeded, const string&) noexcept { }
		virtual void on(InstallationFailed, const string&, const string&) noexcept { }
	};

}

#endif // !defined(DCPLUSPLUS_DCPP_EXTENSIONMANAGER_LISTENER_H)