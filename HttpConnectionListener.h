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

#ifndef DCPLUSPLUS_DCPP_HTTP_CONNECTION_LISTENER_H
#define DCPLUSPLUS_DCPP_HTTP_CONNECTION_LISTENER_H

#include "forward.h"
#include "noexcept.h"
#include <string>

namespace dcpp {

using std::string;

class HttpConnectionListener {
public:
	virtual ~HttpConnectionListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> Data;
	typedef X<1> Failed;
	typedef X<2> Complete;
	typedef X<3> Redirected;
	typedef X<4> Retried;

	virtual void on(Data, HttpConnection*, const uint8_t*, size_t) noexcept = 0;
	virtual void on(Failed, HttpConnection*, const string&) noexcept = 0;
	virtual void on(Complete, HttpConnection*, const string&, bool) noexcept = 0;
	virtual void on(Redirected, HttpConnection*, const string&) noexcept { }
	virtual void on(Retried, HttpConnection*, bool) noexcept { }
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_HTTP_CONNECTION_LISTENER_H)
