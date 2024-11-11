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


#ifndef DCPLUSPLUS_WEBSERVER_EXTENSION_LISTENER_H
#define DCPLUSPLUS_WEBSERVER_EXTENSION_LISTENER_H

#include "forward.h"

#include "stdinc.h"

namespace webserver {
	class ExtensionListener {
	public:
		virtual ~ExtensionListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> ExtensionStarted;
		typedef X<1> ExtensionStopped;

		typedef X<2> SettingValuesUpdated;
		typedef X<3> SettingDefinitionsUpdated;

		typedef X<4> PackageUpdated;
		typedef X<5> StateUpdated;


		virtual void on(ExtensionStarted, const Extension*) noexcept { }
		virtual void on(ExtensionStopped, const Extension*, bool /*aFailed*/) noexcept { }

		virtual void on(SettingValuesUpdated, const Extension*, const SettingValueMap&) noexcept { }
		virtual void on(SettingDefinitionsUpdated, const Extension*) noexcept { }
		virtual void on(PackageUpdated, const Extension*) noexcept { }
		virtual void on(StateUpdated, const Extension*) noexcept { }
	};

}

#endif // !defined(DCPLUSPLUS_WEBSERVER_EXTENSION_LISTENER_H)