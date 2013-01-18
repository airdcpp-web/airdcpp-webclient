/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_ATOMIC_HPP_
#define DCPLUSPLUS_DCPP_ATOMIC_HPP_

// GCC 4.6 and below has issues with atomic - see https://bugs.launchpad.net/dcplusplus/+bug/735512
// MSVC 10 doesn't have atomic at all
#if defined(__GNUC__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)) || defined(_MSC_VER) && _MSC_VER < 1700

#include <boost/atomic.hpp>

namespace dcpp
{

using boost::atomic;
using boost::atomic_flag;

}

#else

#include <atomic>

namespace dcpp
{

using std::atomic;
using std::atomic_flag;

}

#endif



#endif /* DCPLUSPLUS_DCPP_ATOMIC_HPP_ */
