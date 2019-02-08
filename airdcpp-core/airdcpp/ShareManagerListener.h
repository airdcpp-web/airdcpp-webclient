/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SHAREMANAGERLISTENER_H
#define DCPLUSPLUS_DCPP_SHAREMANAGERLISTENER_H

#include "typedefs.h"

namespace dcpp {

	class ShareManagerListener {
	public:
		virtual ~ShareManagerListener() {}
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> ShareLoaded;

		typedef X<1> RefreshQueued;
		typedef X<2> RefreshCompleted;

		typedef X<3> ProfileAdded;
		typedef X<4> ProfileUpdated;
		typedef X<5> ProfileRemoved;
		typedef X<6> DefaultProfileChanged;

		typedef X<7> RootCreated;
		typedef X<8> RootRemoved;
		typedef X<9> RootUpdated;
		typedef X<10> RootRefreshState;

		typedef X<11> ExcludeAdded;
		typedef X<12> ExcludeRemoved;


		virtual void on(ShareLoaded) noexcept{}
		virtual void on(RefreshCompleted, uint8_t /*tasktype*/, const RefreshPathList&) noexcept{}
		virtual void on(RefreshQueued, uint8_t /*tasktype*/, const RefreshPathList&) noexcept {}

		virtual void on(ProfileAdded, ProfileToken) noexcept {}
		virtual void on(ProfileUpdated, ProfileToken, bool /*aIsMajorChange*/) noexcept {}
		virtual void on(ProfileRemoved, ProfileToken) noexcept {}
		virtual void on(DefaultProfileChanged, ProfileToken /*aOldDefault*/, ProfileToken /*aNewDefault*/) noexcept {}

		virtual void on(RootCreated, const string&) noexcept {}
		virtual void on(RootRemoved, const string&) noexcept {}
		virtual void on(RootUpdated, const string&) noexcept {}
		virtual void on(RootRefreshState, const string&) noexcept {}

		virtual void on(ExcludeAdded, const string&) noexcept {}
		virtual void on(ExcludeRemoved, const string&) noexcept {}
	};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SHAREMANAGERLISTENER_H)