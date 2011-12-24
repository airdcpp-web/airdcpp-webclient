/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_GET_SET_H
#define DCPLUSPLUS_DCPP_GET_SET_H

#include <boost/mpl/if.hpp>
#include <type_traits>

#define REF_OR_COPY(t) boost::mpl::if_c<std::is_class<t>::value, const t&, t>::type

#define GETSET(type, name, name2) \
private: type name; \
public: REF_OR_COPY(type) get##name2() const { return name; } \
	void set##name2(REF_OR_COPY(type) name) { this->name = name; }

#endif /* GETSET_H_ */
