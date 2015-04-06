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

#ifndef DCPLUSPLUS_DCPP_HTTP_CONNECTION_H
#define DCPLUSPLUS_DCPP_HTTP_CONNECTION_H

#include "BufferedSocketListener.h"
#include "HttpConnectionListener.h"
#include "Speaker.h"
#include "Util.h"

namespace dcpp {

using std::string;

class HttpConnection : BufferedSocketListener, public Speaker<HttpConnectionListener>, boost::noncopyable
{
public:
	HttpConnection(bool coralize = true, bool aIsUnique = false, bool v4only = false);
	virtual ~HttpConnection();

	void downloadFile(const string& aUrl);
	void postData(const string& aUrl, const StringMap& aData);

	const string& getMimeType() const { return mimeType; }

	int64_t getSize() const { return size; }
	int64_t getDone() const { return done; }

private:
	enum RequestType { TYPE_GET, TYPE_POST };
	enum ConnectionStates { CONN_UNKNOWN, CONN_OK, CONN_FAILED, CONN_MOVED, CONN_CHUNKED };
	enum CoralizeStates { CST_DEFAULT, CST_CONNECTED, CST_NOCORALIZE };

	string currentUrl;
	string method;
	string file;
	string server;
	string port;
	string query;

	string requestBody;

	string mimeType;
	int64_t size;
	int64_t done;

	ConnectionStates connState;
	CoralizeStates coralizeState;
	RequestType connType;

	BufferedSocket* socket;

	void prepareRequest(RequestType type);
	void abortRequest(bool disconnect);

	// BufferedSocketListener
	void on(Connected) noexcept;
	void on(Line, const string&) noexcept;
	void on(Data, uint8_t*, size_t) noexcept;
	void on(ModeChange) noexcept;
	void on(Failed, const string&) noexcept;
	bool isUnique;
	bool v4only;
};

} // namespace dcpp

#endif // !defined(HTTP_CONNECTION_H)
