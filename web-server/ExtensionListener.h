/*
* Copyright (C) 2012-2019 AirDC++ Project
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


#ifndef DCPLUSPLUS_DCPP_EXTENSION_LISTENER_H
#define DCPLUSPLUS_DCPP_EXTENSION_LISTENER_H

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


		virtual void on(ExtensionStarted) noexcept { }
		virtual void on(ExtensionStopped, bool /*aFailed*/) noexcept { }

		virtual void on(SettingValuesUpdated, const SettingValueMap&) noexcept { }
		virtual void on(SettingDefinitionsUpdated) noexcept { }
		virtual void on(PackageUpdated) noexcept { }
	};

}

#endif // !defined(DCPLUSPLUS_DCPP_EXTENSION_LISTENER_H)