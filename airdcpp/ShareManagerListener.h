/*
* Copyright (C) 2011-2022 AirDC++ Project
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

		typedef X<1> RefreshQueued;
		typedef X<2> RefreshStarted;
		typedef X<3> RefreshCompleted;

		typedef X<4> ProfileAdded;
		typedef X<5> ProfileUpdated;
		typedef X<6> ProfileRemoved;
		typedef X<7> DefaultProfileChanged;

		typedef X<8> RootCreated;
		typedef X<9> RootRemoved;
		typedef X<10> RootUpdated;
		typedef X<11> RootRefreshState;

		typedef X<12> ExcludeAdded;
		typedef X<13> ExcludeRemoved;

		typedef X<14> TempFileAdded;
		typedef X<15> TempFileRemoved;

		virtual void on(RefreshQueued, const ShareRefreshTask&) noexcept {}
		virtual void on(RefreshStarted, const ShareRefreshTask&) noexcept {}
		virtual void on(RefreshCompleted, const ShareRefreshTask&, bool /*aSucceed*/, const ShareRefreshStats& /*stats*/) noexcept{}

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

		virtual void on(TempFileAdded, const TempShareInfo&) noexcept {}
		virtual void on(TempFileRemoved, const TempShareInfo&) noexcept {}
	};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SHAREMANAGERLISTENER_H)