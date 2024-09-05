/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SHAREPROFILE_MANAGER_LISTENER_H
#define DCPLUSPLUS_DCPP_SHAREPROFILE_MANAGER_LISTENER_H

#include "typedefs.h"

namespace dcpp {

	class ShareProfileManagerListener {
	public:
		virtual ~ShareProfileManagerListener() {}
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<4> ProfileAdded;
		typedef X<5> ProfileUpdated;
		typedef X<6> ProfileRemoved;
		typedef X<7> DefaultProfileChanged;

		virtual void on(ProfileAdded, ProfileToken) noexcept {}
		virtual void on(ProfileUpdated, ProfileToken, bool /*aIsMajorChange*/) noexcept {}
		virtual void on(ProfileRemoved, ProfileToken) noexcept {}
		virtual void on(DefaultProfileChanged, ProfileToken /*aOldDefault*/, ProfileToken /*aNewDefault*/) noexcept {}
	};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SHAREMANAGERLISTENER_H)