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

#ifndef DCPLUSPLUS_DCPP_FAVORITEUSERMANAGERLISTENER_H_
#define DCPLUSPLUS_DCPP_FAVORITEUSERMANAGERLISTENER_H_

#include "forward.h"

namespace dcpp {

class FavoriteUserManagerListener {
public:
	virtual ~FavoriteUserManagerListener() { }
	template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> FavoriteUserAdded;
	typedef X<1> FavoriteUserRemoved;
	typedef X<2> FavoriteUserUpdated;

	typedef X<3> SlotsUpdated;

	virtual void on(FavoriteUserAdded, const FavoriteUser&) noexcept { }
	virtual void on(FavoriteUserRemoved, const FavoriteUser&) noexcept { }
	virtual void on(FavoriteUserUpdated, const UserPtr&) noexcept { }

	virtual void on(SlotsUpdated, const UserPtr&) noexcept { }
};

} // namespace dcpp

#endif 
