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

#ifndef DCPP_DCPLUSPLUS_DEBUG_H_
#define DCPP_DCPLUSPLUS_DEBUG_H_

#include <cstdio>

#ifdef _DEBUG
#define NO_FAST_ALLOC
#include <cassert>

#ifdef _MSC_VER

#define dcdebug debugTrace
#include <crtdbg.h>

inline void __cdecl debugTrace(const char* format, ...) {

	//show the debug info in output window. 	 
	va_list args;
	va_start(args, format);
	char str[4096];
	vsprintf(str, format, args);
	wchar_t str2[4096];
	::MultiByteToWideChar(CP_UTF8, NULL, str, -1, str2, (int)sizeof(str2) -1);
	OutputDebugString(str2);
	va_end(args);
}

#define dcassert(exp) \
do { if (!(exp)) { \
	dcdebug("Assertion hit in %s(%d): " #exp "\n", __FILE__, __LINE__); \
	if(1 == _CrtDbgReport(_CRT_ASSERT, __FILE__, __LINE__, NULL, #exp)) \
_CrtDbgBreak(); } } while(false)
#else
#define dcdebug printf
#define dcassert(exp) assert(exp)
#endif
#define dcdrun(exp) exp
#else //_DEBUG
#define dcdebug if (false) printf
#define dcassert(exp)
#define dcdrun(exp)
#endif //_DEBUG

#endif /* DCPP_DCPLUSPLUS_DEBUG_H_ */
