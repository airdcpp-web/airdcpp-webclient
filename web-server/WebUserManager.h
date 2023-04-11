/*
* Copyright (C) 2011-2023 AirDC++ Project
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

#ifndef DCPLUSPLUS_WEBSERVER_WEBUSERMANAGER_H
#define DCPLUSPLUS_WEBSERVER_WEBUSERMANAGER_H

#include "forward.h"

#include <airdcpp/CriticalSection.h>
#include <airdcpp/Speaker.h>

#include <web-server/FloodCounter.h>
#include <web-server/Session.h>
#include <web-server/Timer.h>
#include <web-server/WebServerManagerListener.h>
#include <web-server/WebUserManagerListener.h>
#include <web-server/WebUser.h>

namespace webserver {
	class WebUserManager : private WebServerManagerListener, public Speaker<WebUserManagerListener> {
	public:
		WebUserManager(WebServerManager* aServer);
		~WebUserManager();

		// Parse Authentication header from an HTTP request
		// Throws on errors, returns nullptr if no Authorization header is present
		SessionPtr parseHttpSession(const string& aAuthToken, const string& aIp);

		// Throws on errors
		SessionPtr authenticateSession(const string& aUserName, const string& aPassword, Session::SessionType aType, uint64_t aMaxInactivityMinutes, const string& aIP);
		SessionPtr authenticateSession(const string& aRefreshToken, Session::SessionType aType, uint64_t aMaxInactivityMinutes, const string& aIP);

		SessionPtr createExtensionSession(const string& aExtensionName) noexcept;

		SessionList getSessions() const noexcept;
		SessionPtr getSession(const string& aAuthToken) const noexcept;
		SessionPtr getSession(LocalSessionId aId) const noexcept;
		void logout(const SessionPtr& aSession);

		bool hasUsers() const noexcept;
		bool hasUser(const string& aUserName) const noexcept;
		WebUserPtr getUser(const string& aUserName) const noexcept;

		bool addUser(const WebUserPtr& aUser) noexcept;
		bool updateUser(const WebUserPtr& aUser, bool aRemoveSessions) noexcept;
		bool removeUser(const string& aUserName) noexcept;

		WebUserList getUsers() const noexcept;
		void replaceWebUsers(const WebUserList& newUsers) noexcept;

		StringList getUserNames() const noexcept;

		size_t getUserSessionCount() const noexcept;
		string createRefreshToken(const WebUserPtr& aUser) noexcept;

		WebUserManager(WebUserManager&) = delete;
		WebUserManager& operator=(WebUserManager&) = delete;
	private:
		enum AuthType {
			AUTH_UNKNOWN,
			AUTH_BASIC,
			AUTH_BEARER,
		};

		struct TokenInfo {
			const string token;
			const WebUserPtr user;
			const time_t expiresOn;

			typedef vector<TokenInfo> List;
		};

		FloodCounter authFloodCounter;

		mutable SharedMutex cs;

		std::map<std::string, WebUserPtr> users;

		std::map<std::string, SessionPtr> sessionsRemoteId;
		std::map<LocalSessionId, SessionPtr> sessionsLocalId;
		std::map<string, TokenInfo> refreshTokens;

		void checkExpiredSessions() noexcept;
		void checkExpiredTokens() noexcept;
		void resetSocketSession(const WebSocketPtr& aSocket) noexcept;
		void removeSession(const SessionPtr& aSession, bool aTimedOut) noexcept;
		void removeRefreshTokens(const WebUserPtr& aUser) noexcept;
		void removeSessions(const WebUserPtr& aUser) noexcept;
		TimerPtr expirationTimer;

		// Throws on errors
		SessionPtr authenticateSession(const string& aUserName, const string& aPassword, Session::SessionType aType, uint64_t aMaxInactivityMinutes, const string& aIP, const string& aSessionToken);

		SessionPtr createSession(const WebUserPtr& aUser, const string& aSessionToken, Session::SessionType aType, uint64_t aMaxInactivityMinutes, const string& aIP) noexcept;

		void on(WebServerManagerListener::Started) noexcept override;
		void on(WebServerManagerListener::Stopping) noexcept override;
		void on(WebServerManagerListener::Stopped) noexcept override;
		void on(WebServerManagerListener::SocketDisconnected, const WebSocketPtr& aSocket) noexcept override;

		void on(WebServerManagerListener::LoadLegacySettings, SimpleXML& aXml) noexcept override;
		void on(WebServerManagerListener::LoadSettings, const MessageCallback& aErrorF) noexcept override;
		void on(WebServerManagerListener::SaveSettings, const MessageCallback& aErrorF) noexcept override;

		WebServerManager* wsm;
		void setDirty() noexcept;

		bool isDirty = false;
	};
}

#endif