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

#ifndef DCPLUSPLUS_DCPP_COMPILER_H
#define DCPLUSPLUS_DCPP_COMPILER_H

#if defined (__clang__)

#if __clang_major__ < 15
#error Clang 15 is required
#endif

#elif defined(__GNUC__)

#if __GNUC__ < 8
#error GCC 8.0 is required
#endif

#ifdef _WIN32

#ifdef HAVE_OLD_MINGW
#error Regular MinGW has stability problems; use a MinGW package from mingw-w64
// see <https://bugs.launchpad.net/dcplusplus/+bug/1029629> for details
#endif

#endif // _WIN32

#elif defined(_MSC_VER)
#if _MSC_FULL_VER < 193732822
#error Visual Studio 2022 17.7.0 or newer is required
#endif

//disable the deprecated warnings for the CRT functions.
#define _CRT_SECURE_NO_DEPRECATE 1
#define _ATL_SECURE_NO_DEPRECATE 1
#define _CRT_NON_CONFORMING_SWPRINTFS 1

#define strtoll _strtoi64
#define snwprintf _snwprintf

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

#else

#if defined(SIZEOF_LONG) && SIZEOF_LONG == 8
	#define _LL(x) x##ll
	#define _ULL(x) x##ull
#else
	#define _LL(x) x##ll
	#define _ULL(x) x##ull
#endif

#endif

#define I64_FMT "%" PRId64
#define U64_FMT "%" PRIu64
#define SIZET_FMT "%zu"

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

// https://github.com/airdcpp-web/airdcpp-webclient/issues/60
#define BOOST_MOVE_USE_STANDARD_LIBRARY_MOVE

#ifndef _REENTRANT
# define _REENTRANT 1
#endif

#ifdef _MSC_VER
//# pragma warning(disable: 4290) // C++ Exception Specification ignored
# pragma warning(disable: 4127) // websocketpp\frame.hpp(598,24): warning C4127: conditional expression is constant
//# pragma warning(disable: 4996) // Function call with parameters that may be unsafe - this call relies on the caller to check that the passed values are correct.

# pragma warning(disable: 4267) // conversion from 'xxx' to 'yyy', possible loss of data
//# pragma warning(disable: 4706) // assignment within conditional expression

#define BOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE 1

#endif

#endif // DCPLUSPLUS_DCPP_COMPILER_H
