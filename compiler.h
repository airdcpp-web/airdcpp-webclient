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

#ifndef DCPLUSPLUS_DCPP_COMPILER_H
#define DCPLUSPLUS_DCPP_COMPILER_H

#if defined (__clang__)

#if __clang_major__ < 3 || (__clang_major__ == 3 && __clang_minor__ < 2)
#error Clang 3.2 is required
#endif

#elif defined(__GNUC__)

#ifdef _WIN32

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8)
#error GCC 4.8 is required
#endif

#ifdef HAVE_OLD_MINGW
#error Regular MinGW has stability problems; use a MinGW package from mingw-w64
// see <https://bugs.launchpad.net/dcplusplus/+bug/1029629> for details
#endif

#else // _WIN32

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)
#error GCC 4.7 is required
#endif

#endif // _WIN32

#elif defined(_MSC_VER)
#if _MSC_VER < 1800
#error MSVC 12 (2013) is required
#endif

//disable the deprecated warnings for the CRT functions.
#define _CRT_SECURE_NO_DEPRECATE 1
#define _ATL_SECURE_NO_DEPRECATE 1
#define _CRT_NON_CONFORMING_SWPRINTFS 1

#if defined(_MSC_VER)
#define strtoll _strtoi64
#define snprintf _snprintf
#define snwprintf _snwprintf
#else
#define snwprintf snprintf
#endif

#else
#error No supported compiler found

#endif

//#define _SECURE_SCL  0
//#define _ITERATOR_DEBUG_LEVEL 0
//#define _HAS_ITERATOR_DEBUGGING 0
//#define _SECURE_SCL_THROWS 0
#define memzero(dest, n) memset(dest, 0, n)

#if !defined(_MSC_VER) && !defined(__BCPLUSPLUS__)
#if !defined(SIZEOF_LONG_LONG) && !defined(SIZEOF_LONG)
#if (defined(__alpha__) || defined(__ia64__) || defined(_ARCH_PPC64) \
	|| defined(__mips64) || defined(__x86_64__))
/* long should be 64bit */
#define SIZEOF_LONG 8
#elif defined(__i386__) || defined(__CORTEX_M3__)
/* long long should be 64bit */
#define SIZEOF_LONG_LONG 8
#endif
#endif
#endif

#if defined(_MSC_VER) || defined(__MINGW32__)
#define _LL(x) x##ll
#define _ULL(x) x##ull
#define I64_FMT "%I64d"
#define U64_FMT "%I64d"
#define SIZET_FMT "%u"

#elif defined(SIZEOF_LONG) && SIZEOF_LONG == 8
#define _LL(x) x##l
#define _ULL(x) x##ul
#define I64_FMT "%ld"
#define U64_FMT "%lu"
#define SIZET_FMT "%zu"
#else
#define _LL(x) x##ll
#define _ULL(x) x##ull
#define I64_FMT "%lld"
#define U64_FMT "%llu"
#define SIZET_FMT "%zu"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef STRICT
#define STRICT
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef BOOST_ALL_NO_LIB
#define BOOST_ALL_NO_LIB
#endif

#ifndef BOOST_USE_WINDOWS_H
#define BOOST_USE_WINDOWS_H
#endif

#define BOOST_MOVE_USE_STANDARD_LIBRARY_MOVE
#define _VARIADIC_MAX 10

#ifndef _REENTRANT
# define _REENTRANT 1
#endif

#ifdef _MSC_VER
# pragma warning(disable: 4307) //temp disable this annoying warning, boost::lockfree integral constant overflow
# pragma warning(disable: 4913) //boost thread warnings (boost 1.52, remove later)

//# pragma warning(disable: 4996)
# pragma warning(disable: 4711) // function 'xxx' selected for automatic inline expansion
# pragma warning(disable: 4786) // identifier was truncated to '255' characters in the debug information
# pragma warning(disable: 4290) // C++ Exception Specification ignored
# pragma warning(disable: 4127) // constant expression
# pragma warning(disable: 4710) // function not inlined
# pragma warning(disable: 4503) // decorated name length exceeded, name was truncated
# pragma warning(disable: 4428) // universal-character-name encountered in source
# pragma warning(disable: 4201) // nonstadard extension used : nameless struct/union
# pragma warning(disable: 4324) // structure was padded due to __declspec(align()), lockfree queue


# pragma warning(disable: 4244) // conversion from 'xxx' to 'yyy', possible loss of data
# pragma warning(disable: 4267) // conversion from 'xxx' to 'yyy', possible loss of data

#endif

#endif // DCPLUSPLUS_DCPP_COMPILER_H
