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

#ifndef DCPLUSPLUS_WEBSERVER_SESSION_H
#define DCPLUSPLUS_WEBSERVER_SESSION_H

#include "forward.h"

#include <web-server/LazyInitWrapper.h>
#include <web-server/SessionListener.h>

#include <api/base/ApiModule.h>

#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/core/header/typedefs.h>
#include <airdcpp/core/Speaker.h>

namespace webserver {
	// Sessions are owned by WebUserManager and WebSockets (websockets are closed when session is removed)
	class Session : public Speaker<SessionListener> {
	public:
		enum SessionType {
			TYPE_PLAIN,
			TYPE_SECURE,
			TYPE_BASIC_AUTH,
			TYPE_EXTENSION,
		};

		Session(const WebUserPtr& aUser, const std::string& aToken, SessionType aSessionType, WebServerManager* aServer, uint64_t maxInactivityMinutes, const string& aIP);
		~Session() override;

		const std::string& getAuthToken() const noexcept {
			return token;
		}

		LocalSessionId getId() const noexcept {
			return id;
		}

		WebUserPtr getUser() const noexcept {
			return user;
		}

		SessionType getSessionType() const noexcept {
			return sessionType;
		}

		ApiModule* getModule(const std::string& aApiID);

		http_status handleRequest(ApiRequest& aRequest);

		Session(Session&) = delete;
		Session& operator=(Session&) = delete;

		void onSocketConnected(const WebSocketPtr& aSocket) noexcept;
		void onSocketDisconnected() noexcept;
		
		WebServerManager* getServer() noexcept {
			return server;
		}

		void updateActivity() noexcept;
		uint64_t getLastActivity() const noexcept {
			return lastActivity;
		}

		uint64_t getMaxInactivity() const noexcept {
			return maxInactivity;
		}

		const string& getIp() const noexcept {
			return ip;
		}

		void reportError(const string& aError) noexcept;
		bool isTimeout(uint64_t aTick) const noexcept;
	private:
		const uint64_t maxInactivity;
		const time_t started;

		uint64_t lastActivity;
		bool hasSocket = false;

		const LocalSessionId id;
		const std::string token;
		const SessionType sessionType;
		const string ip;

		WebUserPtr user;
		WebServerManager* server;

		mutable CriticalSection cs;

		using LazyModuleWrapper = LazyInitWrapper<ApiModule>;
		std::map<std::string, LazyModuleWrapper> apiHandlers;
	};
}

#endif