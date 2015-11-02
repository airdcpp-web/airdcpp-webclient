/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_WEBSOCKET_H
#define DCPLUSPLUS_DCPP_WEBSOCKET_H

#include <web-server/stdinc.h>

#include <web-server/Session.h>
#include <web-server/ApiRequest.h>

#include <airdcpp/GetSet.h>

namespace webserver {
	// WebSockets are owned by WebServerManager and API modules

	class WebSocket {
	public:
		WebSocket(bool aIsSecure, websocketpp::connection_hdl aHdl, server_plain* aServer) : WebSocket(aIsSecure, aHdl) {
			plainServer = aServer;
		}
		WebSocket(bool aIsSecure, websocketpp::connection_hdl aHdl, server_tls* aServer) : WebSocket(aIsSecure, aHdl) {
			tlsServer = aServer;
		}
		~WebSocket();

		void close(websocketpp::close::status::value aCode, const std::string& aMsg);

		IGETSET(SessionPtr, session, Session, nullptr);

		void sendPlain(const std::string& aMsg);
		void sendApiResponse(const json& aJsonResponse, const json& aErrorJson, websocketpp::http::status_code::value aCode, int aCallbackId);

		WebSocket(WebSocket&) = delete;
		WebSocket& operator=(WebSocket&) = delete;

		string getIp() const noexcept;
	protected:
		WebSocket(bool aIsSecure, websocketpp::connection_hdl aHdl);
	private:
		union {
			server_plain* plainServer;
			server_tls* tlsServer;
		};

		websocketpp::connection_hdl hdl;

		bool secure;
	};
}

#endif