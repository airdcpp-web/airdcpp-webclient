/*
* Copyright (C) 2011-2024 AirDC++ Project
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
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

#ifndef DCPLUSPLUS_WEBSERVER_SOCKET_MANAGER_H
#define DCPLUSPLUS_WEBSERVER_SOCKET_MANAGER_H

#include "stdinc.h"

#include "Timer.h"
#include "SocketManagerListener.h"
#include "WebSocket.h"
#include "WebUserManagerListener.h"

#include <airdcpp/Speaker.h>


namespace webserver {
	class WebServerManager;
	class SocketManager : public Speaker<SocketManagerListener>, private WebUserManagerListener {
	public:
		explicit SocketManager(WebServerManager* aWsm) noexcept : wsm(aWsm) {}

		void start();
		void stop() noexcept;

		// Disconnect all sockets
		void disconnectSockets(const std::string& aMessage) noexcept;

		// Reset sessions for associated sockets
		WebSocketPtr getSocket(LocalSessionId aSessionToken) noexcept;

		SocketManager(SocketManager&) = delete;
		SocketManager& operator=(SocketManager&) = delete;

		template<class EndpointType>
		void setEndpointHandlers(EndpointType& aEndpoint, bool aIsSecure) {
			aEndpoint.set_message_handler(
				std::bind_front(&SocketManager::handleSocketMessage<EndpointType>, this));

			aEndpoint.set_close_handler(std::bind_front(&SocketManager::handleSocketDisconnected, this));
			aEndpoint.set_open_handler(std::bind_front(&SocketManager::handleSocketConnected<EndpointType>, this, &aEndpoint, aIsSecure));

			aEndpoint.set_pong_timeout_handler(std::bind_front(&SocketManager::handlePongTimeout, this));
		}
	private:
		void onAuthenticated(const SessionPtr& aSession, const WebSocketPtr& aSocket) noexcept;

		// Websocketpp event handlers
		template <typename EndpointType>
		void handleSocketConnected(EndpointType* aServer, bool aIsSecure, websocketpp::connection_hdl hdl) {
			auto con = aServer->get_con_from_hdl(hdl);
			auto socket = make_shared<WebSocket>(aIsSecure, hdl, con->get_request(), aServer, wsm);

			addSocket(hdl, socket);
		}

		void handleSocketDisconnected(websocketpp::connection_hdl hdl);

		void handlePongReceived(websocketpp::connection_hdl hdl, const string& aPayload);
		void handlePongTimeout(websocketpp::connection_hdl hdl, const string& aPayload);

		// The shared on_message handler takes a template parameter so the function can
		// resolve any endpoint dependent types like message_ptr or connection_ptr
		template <typename EndpointType>
		void handleSocketMessage(websocketpp::connection_hdl hdl,
			typename EndpointType::message_ptr msg) {

			auto socket = getSocket(hdl);
			if (!socket) {
				dcassert(0);
				return;
			}

			socket->onData(msg->get_payload(), [&socket, this](const SessionPtr& aSession) {
				onAuthenticated(aSession, socket);
			});
		}

		void addSocket(websocketpp::connection_hdl hdl, const WebSocketPtr& aSocket) noexcept;
		WebSocketPtr getSocket(websocketpp::connection_hdl hdl) const noexcept;

		void pingTimer() noexcept;

		mutable SharedMutex cs;

		using WebSocketList = vector<WebSocketPtr>;
		std::map<websocketpp::connection_hdl, WebSocketPtr, std::owner_less<websocketpp::connection_hdl>> sockets;

		TimerPtr socketPingTimer;
		WebServerManager* wsm;

		void resetSocketSession(const WebSocketPtr& aSocket) noexcept;

		void on(WebUserManagerListener::SessionRemoved, const SessionPtr& aSession, int aReason) noexcept override;
	};
}

#endif // DCPLUSPLUS_DCPP_WEBSERVER_H
