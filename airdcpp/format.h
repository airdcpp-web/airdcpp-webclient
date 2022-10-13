/*
 * Copyright (C) 2001-2022 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_FORMAT_H_
#define DCPLUSPLUS_DCPP_FORMAT_H_

#include <boost/format.hpp>

namespace dcpp {

template<typename T>
boost::basic_format<T> dcpp_fmt(const std::basic_string<T>& t) {
	boost::basic_format<T> fmt;
	fmt.exceptions(boost::io::no_error_bits);
	fmt.parse(t);
	return fmt;
}

template<typename T>
boost::basic_format<T> dcpp_fmt(const T* t) {
	return dcpp_fmt(std::basic_string<T>(t));
}

}

using boost::str;

#endif /* DCPLUSPLUS_DCPP_FORMAT_H_ */
