/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_HTTP_CONNECTION_H
#define DCPLUSPLUS_DCPP_HTTP_CONNECTION_H

#include "BufferedSocket.h"

namespace dcpp {

class HttpConnection;

class HttpConnectionListener {
public:
	virtual ~HttpConnectionListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> Data;
	typedef X<1> Failed;
	typedef X<2> Complete;
	typedef X<3> Redirected;
	typedef X<4> TypeNormal;
	typedef X<5> TypeBZ2;
	typedef X<6> Retried;

	virtual void on(Data, HttpConnection*, const uint8_t*, size_t) noexcept = 0;
	virtual void on(Failed, HttpConnection*, const string&) noexcept = 0;
	virtual void on(Complete, HttpConnection*, const string&, bool) noexcept = 0;
	virtual void on(Redirected, HttpConnection*, const string&) noexcept { }
	virtual void on(TypeNormal, HttpConnection*) noexcept { }
	virtual void on(TypeBZ2, HttpConnection*) noexcept { }
	virtual void on(Retried, HttpConnection*, bool) noexcept { }
};

class HttpConnection : BufferedSocketListener, public Speaker<HttpConnectionListener>
{
public:
	HttpConnection(bool coralize = true);
	virtual ~HttpConnection();

	void downloadFile(const string& aUrl);
	int64_t getSize() const { return size; }
private:
	enum CoralizeState { CST_DEFAULT, CST_CONNECTED, CST_NOCORALIZE };

	string currentUrl;
	string file;
	string server;
	bool ok;
	string port;
	int64_t size;
	bool moved302;

	CoralizeState coralizeState;

	BufferedSocket* socket;

	// BufferedSocketListener
	void on(Connected) noexcept;
	void on(Line, const string&) noexcept;
	void on(Data, uint8_t*, size_t) noexcept;
	void on(ModeChange) noexcept;
	void on(Failed, const string&) noexcept;

	void onConnected(); 
	void onLine(const string& aLine);
	
};

} // namespace dcpp

#endif // !defined(HTTP_CONNECTION_H)

/**
 * @file
 * $Id: HttpConnection.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
