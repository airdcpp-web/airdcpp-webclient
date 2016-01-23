/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <web-server/stdinc.h>
#include <web-server/WebSocket.h>

#include <airdcpp/TimerManager.h>
#include <airdcpp/Util.h>

namespace webserver {
	WebSocket::WebSocket(bool aIsSecure, websocketpp::connection_hdl aHdl) :
		secure(aIsSecure), hdl(aHdl), timeCreated(GET_TICK()) {

		debugMessage("Websocket created");
	}

	WebSocket::~WebSocket() {
		dcdebug("Websocket was deleted\n");
	}

	string WebSocket::getIp() const noexcept {
		if (secure) {
			auto conn = tlsServer->get_con_from_hdl(hdl);
			return conn->get_remote_endpoint();
		} else {
			auto conn = plainServer->get_con_from_hdl(hdl);
			return conn->get_remote_endpoint();
		}
	}

	void WebSocket::sendApiResponse(const json& aResponseJson, const json& aErrorJson, websocketpp::http::status_code::value aCode, int aCallbackId) noexcept {
		json j;

		if (aCallbackId > 0) {
			j["callback_id"] = aCallbackId;
		} else {
			// Failed to parse the request
			dcassert(!aErrorJson.is_null());
		}
		
		j["code"] = aCode;

		if (aCode < 200 || aCode > 299) {
			dcdebug("Socket request %d failed: %s\n", aCallbackId, aErrorJson.dump().c_str());
			j["error"] = aErrorJson;
		} else if (!aResponseJson.is_null()) {
			j["data"] = aResponseJson;
		}

		sendPlain(j.dump(4));
	}

	void WebSocket::debugMessage(const string& aMessage) const noexcept {
		dcdebug(string(aMessage + " (%s)\n").c_str(), session ? session->getAuthToken().c_str() : "no session");
	}

	void WebSocket::sendPlain(const string& aMsg) noexcept {
		//dcdebug("WebSocket::send: %s\n", aMsg.c_str());
		try {
			if (secure) {
				tlsServer->send(hdl, aMsg, websocketpp::frame::opcode::text);
			} else {
				plainServer->send(hdl, aMsg, websocketpp::frame::opcode::text);
			}

		} catch (const std::exception& e) {
			debugMessage("WebSocket::send failed: " + string(e.what()));
		}
	}

	void WebSocket::ping() noexcept {
		try {
			if (secure) {
				tlsServer->ping(hdl, Util::emptyString);
			} else {
				plainServer->ping(hdl, Util::emptyString);
			}

		} catch (const std::exception& e) {
			debugMessage("WebSocket::ping failed: " + string(e.what()));
		}
	}

	void WebSocket::close(websocketpp::close::status::value aCode, const string& aMsg) {
		try {
			if (secure) {
				tlsServer->close(hdl, aCode, aMsg);
			} else {
				plainServer->close(hdl, aCode, aMsg);
			}
		} catch (const std::exception& e) {
			debugMessage("WebSocket::close failed: " + string(e.what()));
		}
	}
}