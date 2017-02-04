/*
* Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_RECENTMANAGERLISTENER_H_
#define DCPLUSPLUS_DCPP_RECENTMANAGERLISTENER_H_

#include "forward.h"

namespace dcpp {

	class RecentManagerListener {
	public:
		virtual ~RecentManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> RecentAdded;
		typedef X<1> RecentRemoved;
		typedef X<2> RecentUpdated;

		virtual void on(RecentAdded, const RecentEntryPtr&) noexcept {}
		virtual void on(RecentRemoved, const RecentEntryPtr&) noexcept {}
		virtual void on(RecentUpdated, const RecentEntryPtr&) noexcept {}
	};

} // namespace dcpp

#endif 
