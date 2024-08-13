/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SEARCH_MANAGER_LISTENER_H
#define DCPLUSPLUS_DCPP_SEARCH_MANAGER_LISTENER_H

#include "forward.h"

namespace dcpp {

class SearchQueueItem;

class SearchManagerListener {
public:
	virtual ~SearchManagerListener() { }
	template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> SR;
	typedef X<1> IncomingSearch;

	typedef X<5> SearchTypesChanged;
    typedef X<6> SearchInstanceCreated;
    typedef X<7> SearchInstanceRemoved;

	virtual void on(SR, const SearchResultPtr&) noexcept { }
	virtual void on(IncomingSearch, Client*, const OnlineUserPtr& /*aAdcUser*/, const SearchQuery&, const SearchResultList&, bool /*isActive*/) noexcept {}

	virtual void on(SearchTypesChanged) noexcept { }
    virtual void on(SearchInstanceCreated, const SearchInstancePtr&) noexcept { }
    virtual void on(SearchInstanceRemoved, const SearchInstancePtr&) noexcept { }
};

} // namespace dcpp

#endif // !defined(SEARCH_MANAGER_LISTENER_H)
