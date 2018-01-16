/*
* Copyright (C) 2012-2018 AirDC++ Project
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


#ifndef DCPP_IGNORE_MANAGER_LISTENER_H
#define DCPP_IGNORE_MANAGER_LISTENER_H

#include "forward.h"

namespace dcpp {

	class IgnoreManagerListener {
	public:
		virtual ~IgnoreManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> IgnoreAdded;
		typedef X<1> IgnoreRemoved;

		virtual void on(IgnoreAdded, const UserPtr&) noexcept {}
		virtual void on(IgnoreRemoved, const UserPtr&) noexcept {}
	};

} // namespace dcpp

#endif // !defined(DCPP_PRIVATECHAT_MANAGER_LISTENER_H)