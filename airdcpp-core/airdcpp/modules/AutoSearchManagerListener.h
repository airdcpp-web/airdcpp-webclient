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


#ifndef DCPLUSPLUS_DCPP_AUTOSEARCH_MANAGER_LISTENER_H
#define DCPLUSPLUS_DCPP_AUTOSEARCH_MANAGER_LISTENER_H

#include "AutoSearch.h"

namespace dcpp {

class AutoSearchManagerListener {
public:
	virtual ~AutoSearchManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> ItemRemoved;
	typedef X<1> ItemAdded;
	typedef X<2> ItemUpdated;
	typedef X<3> SearchForeground;
	typedef X<4> ItemSearched;

	virtual void on(ItemRemoved, const AutoSearchPtr&) noexcept { }
	virtual void on(ItemAdded, const AutoSearchPtr&) noexcept { }
	virtual void on(ItemUpdated, const AutoSearchPtr&, bool) noexcept { }
	virtual void on(SearchForeground, const AutoSearchPtr&, const string& /*searchString*/) noexcept {}
	virtual void on(ItemSearched, const AutoSearchPtr&, const string& /*aMsg*/) noexcept {}
};

} // namespace dcpp

#endif // !defined(AUTOSEARCH_MANAGER_LISTENER_H)