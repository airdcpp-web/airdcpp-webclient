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

#include <airdcpp/format.h>
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

		sendPlain(j);
	}

	void WebSocket::logError(const string& aMessage, websocketpp::log::level aErrorLevel) const noexcept {
		auto message = (dcpp_fmt("Websocket: " + aMessage + " (%s)") % (session ? session->getAuthToken().c_str() : "no session")).str();
		if (secure) {
			tlsServer->get_elog().write(aErrorLevel, message);
		} else {
			plainServer->get_elog().write(aErrorLevel, message);
		}
	}

	void WebSocket::debugMessage(const string& aMessage) const noexcept {
		dcdebug(string(aMessage + " (%s)\n").c_str(), session ? session->getAuthToken().c_str() : "no session");
	}

	void WebSocket::sendPlain(const json& aJson) noexcept {
		auto str = aJson.dump();

		//debugMessage("WebSocket::sendPlain:" + (str.length() <= 500 ? str : str.substr(0, 500) + " (truncated)"));

		try {
			if (secure) {
				tlsServer->send(hdl, str, websocketpp::frame::opcode::text);
			} else {
				plainServer->send(hdl, str, websocketpp::frame::opcode::text);
			}
		} catch (const std::exception& e) {
			logError("Failed to send data: " + string(e.what()), websocketpp::log::elevel::fatal);
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
			debugMessage("Ping failed: " + string(e.what()));
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