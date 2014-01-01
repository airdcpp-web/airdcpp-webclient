/*
* Copyright (C) 2001-2014 AirDC++
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

#ifndef OFFLINE_USER_H
#define OFFLINE_USER_H

#include "User.h"
#include "CID.h"

namespace dcpp {

	class OfflineUser {
	public:
		OfflineUser(const string& nick_, const string& hubUrl_, time_t lastSeen_ = 0) : nick(nick_), url(hubUrl_), lastSeen(lastSeen_) {}

		GETSET(string, nick, Nick);
		GETSET(string, url, Url);
		GETSET(time_t, lastSeen, LastSeen);
	};

} // namespace dcpp

#endif // !defined(OFFLINE_USER_H)