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

#ifndef DCPLUSPLUS_DCPP_STDINC_H
#define DCPLUSPLUS_DCPP_STDINC_H

#include "compiler.h"

#ifndef _DEBUG
# define BOOST_DISABLE_ASSERTS 1
#endif

#ifndef BZ_NO_STDIO
#define BZ_NO_STDIO 1
#endif

#ifdef _WIN32
#include "w.h"
#else
#include <unistd.h>
#define BOOST_PTHREAD_HAS_MUTEXATTR_SETTYPE
#endif

/*#ifndef _WIN64
# undef memcpy
# undef memset
# undef memzero
# define memcpy memcpy2
# define memset memset2
#endif*/

#include <wchar.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <memory.h>
#include <sys/types.h>
#include <time.h>
#include <locale.h>
#ifndef _MSC_VER
#include <stdint.h>
#endif

#include <algorithm>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <numeric>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/algorithm/cxx11/copy_if.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/scoped_array.hpp>
#include <boost/noncopyable.hpp>
#include <boost/regex.hpp>
#include <boost/optional.hpp>
#include <boost/range/adaptor/reversed.hpp>

namespace dcpp {
	using namespace std;
	using boost::optional;
	using boost::adaptors::map_values;
	using boost::adaptors::map_keys;
	using boost::adaptors::reversed;
	
#ifdef _WIN32
	inline int stricmp(const string& a, const string& b) { return _stricmp(a.c_str(), b.c_str()); }
	inline int strnicmp(const string& a, const string& b, size_t n) { return _strnicmp(a.c_str(), b.c_str(), n); }
	inline int stricmp(const wstring& a, const wstring& b) { return _wcsicmp(a.c_str(), b.c_str()); }
	inline int strnicmp(const wstring& a, const wstring& b, size_t n) { return _wcsnicmp(a.c_str(), b.c_str(), n); }
#endif
}

// always include
#include <utility>
using std::move;

#endif // !defined(STDINC_H)
