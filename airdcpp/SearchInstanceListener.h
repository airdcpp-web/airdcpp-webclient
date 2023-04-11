/*
* Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SEARCH_INSTANCE_LISTENER_H
#define DCPLUSPLUS_DCPP_SEARCH_INSTANCE_LISTENER_H

#include "forward.h"

namespace dcpp {
	class SearchInstanceListener {
	public:
		virtual ~SearchInstanceListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> UserResult;
		typedef X<1> GroupedResultAdded;
		typedef X<2> ChildResultAdded;
		typedef X<3> Reset;
		typedef X<4> HubSearchQueued;
		typedef X<5> HubSearchSent;
		typedef X<6> ResultFiltered;

		virtual void on(GroupedResultAdded, const GroupedSearchResultPtr&) noexcept { }
		virtual void on(ChildResultAdded, const GroupedSearchResultPtr&, const SearchResultPtr&) noexcept { }

		virtual void on(UserResult, const SearchResultPtr&, const GroupedSearchResultPtr&) noexcept { }

		virtual void on(Reset) noexcept { }
		virtual void on(HubSearchQueued, const string&, uint64_t, size_t) noexcept { }
		virtual void on(HubSearchSent, const string&, int) noexcept { }
		virtual void on(ResultFiltered) noexcept { }
	};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SEARCH_INSTANCE_LISTENER_H)
