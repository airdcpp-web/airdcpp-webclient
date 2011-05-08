/*
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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

#include "memcpy_amd.h"

#define _SECURE_SCL  0
#define _ITERATOR_DEBUG_LEVEL 0
#define _HAS_ITERATOR_DEBUGGING 0

#ifndef _DEBUG
# define BOOST_DISABLE_ASSERTS 1
#endif

// This enables stlport's debug mode (and slows it down to a crawl...)
//#define _STLP_DEBUG 1
//#define _STLP_USE_MALLOC 1
//#define _STLP_USE_NEWALLOC 1
//#define _STLP_LEAKS_PEDANTIC 1

// --- Shouldn't have to change anything under here...

#ifndef _REENTRANT
# define _REENTRANT 1
#endif

#ifndef BZ_NO_STDIO
#define BZ_NO_STDIO 1
#endif

#ifdef _MSC_VER

//disable the deprecated warnings for the CRT functions.
#define _CRT_SECURE_NO_DEPRECATE 1
#define _ATL_SECURE_NO_DEPRECATE 1
#define _CRT_NON_CONFORMING_SWPRINTFS 1

# pragma warning(disable: 4711) // function 'xxx' selected for automatic inline expansion
# pragma warning(disable: 4786) // identifier was truncated to '255' characters in the debug information
# pragma warning(disable: 4290) // C++ Exception Specification ignored
# pragma warning(disable: 4127) // constant expression
# pragma warning(disable: 4710) // function not inlined
# pragma warning(disable: 4503) // decorated name length exceeded, name was truncated
# pragma warning(disable: 4428) // universal-character-name encountered in source
# pragma warning(disable: 4201) // nonstadard extension used : nameless struct/union
# pragma warning(disable: 4244) // converting string to unsigned char to int

#ifdef _WIN64
# pragma warning(disable: 4244) // conversion from 'xxx' to 'yyy', possible loss of data
# pragma warning(disable: 4267) // conversion from 'xxx' to 'yyy', possible loss of data
#endif

# ifndef CDECL
#  define CDECL _cdecl
# endif

#else // _MSC_VER

# ifndef CDECL
#  define CDECL
# endif

#endif // _MSC_VER

#ifdef _WIN32
# define _WIN32_WINNT 0x0502
# define _WIN32_IE	0x0501
# define WINVER 0x501

#define STRICT
#define WIN32_LEAN_AND_MEAN

#if _MSC_VER == 1400 || _MSC_VER == 1500
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES 1
//disable the deprecated warnings for the crt functions.
#pragma warning(disable: 4996)
#endif

#include <winsock2.h>

#include <windows.h>
#include <mmsystem.h>

#include <tchar.h>
#include <shlobj.h>

#else
#include <unistd.h>
#endif

#ifdef _MSC_VER
#include <crtdbg.h>

#else
#include <assert.h>
#endif

#include <cctype>
#include <clocale>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include <memory.h>
#include <sys/types.h>

#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <deque>
#include <list>
#include <utility>
#include <functional>
#include <memory>
#include <numeric>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <boost/scoped_array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>

//#define BOOST_REGEX_NO_LIB 1
//#define BOOST_THREAD_NO_LIB 1
#define BOOST_ALL_NO_LIB 1
#include <boost/regex.hpp>
//#include <regex>

namespace dcpp {
	using namespace std;

}


#endif // !defined(STDINC_H)

/**
 * @file
 * $Id: stdinc.h 548 2010-09-06 08:54:37Z bigmuscle $
 */
