/*
* Copyright (C) 2011-2018 AirDC++ Project
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
#include <web-server/WebServerManager.h>
#include <web-server/WebSocket.h>

#include <airdcpp/format.h>
#include <airdcpp/TimerManager.h>
#include <airdcpp/Util.h>


namespace webserver {
	WebSocket::WebSocket(bool aIsSecure, websocketpp::connection_hdl aHdl, const websocketpp::http::parser::request& aRequest, server_plain* aServer, WebServerManager* aWsm) : WebSocket(aIsSecure, aHdl, aRequest, aWsm) {
		plainServer = aServer;
	}

	WebSocket::WebSocket(bool aIsSecure, websocketpp::connection_hdl aHdl, const websocketpp::http::parser::request& aRequest, server_tls* aServer, WebServerManager* aWsm) : WebSocket(aIsSecure, aHdl, aRequest, aWsm) {
		tlsServer = aServer;
	}

	WebSocket::WebSocket(bool aIsSecure, websocketpp::connection_hdl aHdl, const websocketpp::http::parser::request& aRequest, WebServerManager* aWsm) :
		secure(aIsSecure), hdl(aHdl), timeCreated(GET_TICK()), wsm(aWsm) {

		debugMessage("Websocket created");

		url = aRequest.get_uri();
		if (!url.empty() && url.back() != '/') {
			url += '/';
		}
	}

	WebSocket::~WebSocket() {
		dcdebug("Websocket was deleted\n");
	}

	string WebSocket::getIp() const noexcept {
		try {
			if (secure) {
				auto conn = tlsServer->get_con_from_hdl(hdl);
				return conn->get_raw_socket().remote_endpoint().address().to_string();
			} else {
				auto conn = plainServer->get_con_from_hdl(hdl);
				return conn->get_raw_socket().remote_endpoint().address().to_string();
			}
		} catch (const std::exception& e) {
			dcdebug("WebSocket::getIp failed: %s\n", e.what());
		}

		return Util::emptyString;
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
		} else {
			dcassert(aCode == websocketpp::http::status_code::no_content);
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

		wsm->onData(str, TransportType::TYPE_SOCKET, Direction::OUTGOING, getIp());

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
			debugMessage("WebSocket::ping failed: " + string(e.what()));
		}
	}

	void WebSocket::close(websocketpp::close::status::value aCode, const string& aMsg) {
		debugMessage("WebSocket::close");
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