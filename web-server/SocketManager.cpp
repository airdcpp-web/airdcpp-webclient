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

#include "stdinc.h"

#include <web-server/SocketManager.h>

#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/WebUserManager.h>

#include <airdcpp/typedefs.h>

#include <airdcpp/Thread.h>
#include <airdcpp/TimerManager.h>


namespace webserver {
	constexpr auto AUTHENTICATION_TIMEOUT = 60; // seconds;

	using namespace dcpp;

	void SocketManager::start() {
		wsm->getUserManager().addListener(this);

		// Add timers
		{
			socketPingTimer = wsm->addTimer(
				[this] {
					pingTimer();
				},
				WEBCFG(PING_INTERVAL).num() * 1000
			);

			socketPingTimer->start(false);
		}
	}

	void SocketManager::stop() noexcept {
		wsm->getUserManager().removeListener(this);

		if (socketPingTimer)
			socketPingTimer->stop(true);

		disconnectSockets(STRING(WEB_SERVER_SHUTTING_DOWN));

		for (;;) {
			{
				RLock l(cs);
				if (sockets.empty()) {
					break;
				}
			}

			Thread::sleep(50);
		}
	}

	WebSocketPtr SocketManager::getSocket(websocketpp::connection_hdl hdl) const noexcept {
		RLock l(cs);
		auto s = sockets.find(hdl);
		if (s != sockets.end()) {
			return s->second;
		}

		return nullptr;
	}

	// For debugging only
	void SocketManager::handlePongReceived(websocketpp::connection_hdl hdl, const string& /*aPayload*/) {
		auto socket = getSocket(hdl);
		if (!socket) {
			return;
		}

		socket->debugMessage("PONG succeed");
	}

	void SocketManager::handlePongTimeout(websocketpp::connection_hdl hdl, const string&) {
		auto socket = getSocket(hdl);
		if (!socket) {
			return;
		}

		if (socket->getSession() && socket->getSession()->getSessionType() == Session::SessionType::TYPE_EXTENSION && WEBCFG(EXTENSIONS_DEBUG_MODE).boolean()) {
			wsm->log("Disconnecting extension " + socket->getSession()->getUser()->getUserName() + " because of ping timeout", LogMessage::SEV_INFO);
		}

		socket->debugMessage("PONG timed out");

		socket->close(websocketpp::close::status::internal_endpoint_error, "PONG timed out");
	}

	void SocketManager::pingTimer() noexcept {
		vector<WebSocketPtr> inactiveSockets;
		auto tick = GET_TICK();

		{
			RLock l(cs);
			for (const auto& socket : sockets | views::values) {
				socket->ping();

				// Disconnect sockets without a session after one minute
				if (!socket->getSession() && socket->getTimeCreated() + AUTHENTICATION_TIMEOUT * 1000ULL < tick) {
					inactiveSockets.push_back(socket);
				}
			}
		}

		for (const auto& s : inactiveSockets) {
			s->close(websocketpp::close::status::policy_violation, "Authentication timeout");
		}
	}

	void SocketManager::disconnectSockets(const string& aMessage) noexcept {
		RLock l(cs);
		for (const auto& socket : sockets | views::values) {
			socket->close(websocketpp::close::status::going_away, aMessage);
		}
	}

	WebSocketPtr SocketManager::getSocket(LocalSessionId aSessionToken) noexcept {
		RLock l(cs);
		auto i = ranges::find_if(sockets | views::values, [&](const WebSocketPtr& s) {
			return s->getSession() && s->getSession()->getId() == aSessionToken;
		});

		return i.base() == sockets.end() ? nullptr : *i;
	}

	void SocketManager::addSocket(websocketpp::connection_hdl hdl, const WebSocketPtr& aSocket) noexcept {
		{
			WLock l(cs);
			sockets.try_emplace(hdl, aSocket);
		}

		fire(SocketManagerListener::SocketConnected(), aSocket);
	}

	void SocketManager::handleSocketDisconnected(websocketpp::connection_hdl hdl) {
		auto socket = getSocket(hdl);
		if (!socket) {
			dcassert(0);
			return;
		}

		// Process all listener events before removing the socket from the list to avoid issues on shutdown
		dcdebug("Socket disconnected: %s\n", socket->getSession() ? socket->getSession()->getAuthToken().c_str() : "(no session)");
		fire(SocketManagerListener::SocketDisconnected(), socket);
		resetSocketSession(socket);

		{
			WLock l(cs);
			auto s = sockets.find(hdl);
			dcassert(s != sockets.end());
			if (s == sockets.end()) {
				return;
			}

			sockets.erase(s);
		}

		dcassert(socket.use_count() == 1);
	}

	void SocketManager::onAuthenticated(const SessionPtr& aSession, const WebSocketPtr& aSocket) noexcept {
		auto oldSocket = getSocket(aSession->getId());
		if (oldSocket) {
			dcdebug("Replace socket for session %s\n", aSession->getAuthToken().c_str());
			resetSocketSession(oldSocket);

			oldSocket->close(websocketpp::close::status::policy_violation, "Another socket was connected to this session");
		}

		aSession->onSocketConnected(aSocket);
		aSocket->setSession(aSession);
	}

	void SocketManager::on(WebUserManagerListener::SessionRemoved, const SessionPtr& aSession, int aReason) noexcept {
		auto socket = getSocket(aSession->getId());
		if (!socket) {
			dcdebug("No socket for a removed session %s\n", aSession->getAuthToken().c_str());
			return;
		}

		resetSocketSession(socket);

		if (static_cast<WebUserManager::SessionRemovalReason>(aReason) == WebUserManager::SessionRemovalReason::USER_CHANGED) {
			socket->close(websocketpp::close::status::normal, "Re-authentication required");
		}
	}

	void SocketManager::resetSocketSession(const WebSocketPtr& aSocket) noexcept {
		if (aSocket->getSession()) {
			dcdebug("Resetting socket for session %s\n", aSocket->getSession()->getAuthToken().c_str());
			aSocket->getSession()->onSocketDisconnected();
			aSocket->setSession(nullptr);
		}
	}
}