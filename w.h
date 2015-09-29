/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_W_H_
#define DCPLUSPLUS_DCPP_W_H_

#ifdef _WIN32
#define UNC_MAX_PATH 1024  // we can go much, much longer but limit it here.

#ifndef _WIN32_WINNT
# define _WIN32_WINNT 0x600
#endif

#ifndef _WIN32_IE
# define _WIN32_IE _WIN32_IE_IE70
#endif

#ifndef WINVER
# define WINVER 0x600
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <tchar.h>

#endif

#endif /* W_H_ */
