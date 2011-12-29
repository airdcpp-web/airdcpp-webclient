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

/* adds a private member variable to a class and provides public member functions to access it:

	- for small types, one getter function that retrieves a copy of the member variable.
	- for small types, one setter function that assigns a copy of the value passed as parameter to
	the member variable.

	- for large types, one getter function that retrieves a const reference to the member variable.
	- for large types, two setter functions:
		* one setter function that binds to non-const rvalue references and moves the rvalue to the
		member variable (no copy operation).
		* one setter function that binds to the rest and assigns a copy of the value passed as
		parameter to the member variable. */

#include <boost/mpl/if.hpp>
#include <type_traits>

#ifndef DCPLUSPLUS_SIMPLE_GETSET

#define GETSET(t, name, name2) \
private: t name; \
public: boost::mpl::if_c<std::is_class<t>::value, const t&, t>::type get##name2() const { return name; } \
	\
	template<typename GetSetT> typename std::enable_if<!std::is_class<GetSetT>::value, void>::type \
	set##name2(GetSetT name) { this->name = name; } /* small type: simple setter that just copies */ \
	\
	template<typename GetSetT> typename std::enable_if<std::is_class<GetSetT>::value, void>::type \
	set##name2(GetSetT&& name) { this->name = std::forward<GetSetT>(name); } /* large type: move the rvalue ref */ \
	\
	template<typename GetSetT> typename std::enable_if<std::is_class<GetSetT>::value, void>::type \
	set##name2(const GetSetT& name) { this->name = name; } /* large type: copy the parameter */

#else

// This version is for my stupid editor =)
#define GETSET(t, name, name2) \
	private: t name; \
	public: t get##name2() const; void set##name2(t name);

#endif

#endif /* GETSET_H_ */
