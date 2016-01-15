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

#ifndef DCPLUSPLUS_DCPP_WEBUSERMANAGER_H
#define DCPLUSPLUS_DCPP_WEBUSERMANAGER_H

#include <web-server/stdinc.h>

#include <airdcpp/CriticalSection.h>
#include <airdcpp/Speaker.h>

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

		SessionPtr authenticate(const string& aUserName, const string& aPassword, bool aIsSecure, uint64_t aMaxInactivityMinutes, bool aUserSession) noexcept;

		SessionPtr getSession(const string& aAuthToken) const noexcept;
		SessionPtr getSession(LocalSessionId aId) const noexcept;
		void logout(const SessionPtr& aSession);

		bool hasUsers() const noexcept;
		bool hasUser(const string& aUserName) const noexcept;
		WebUserPtr getUser(const string& aUserName) const noexcept;

		bool addUser(const WebUserPtr& aUser) noexcept;
		bool updateUser(const WebUserPtr& aUser) noexcept;
		bool removeUser(const string& aUserName) noexcept;

		WebUserList getUsers() const noexcept;
		void replaceWebUsers(const WebUserList& newUsers) noexcept;

		StringList getUserNames() const noexcept;

		size_t getSessionCount() const noexcept;
		void setSessionAwayState(LocalSessionId aSessionId, bool aAway) noexcept;
	private:
		void checkAwayState() noexcept;
		mutable SharedMutex cs;

		std::map<std::string, WebUserPtr> users;

		std::map<std::string, SessionPtr> sessionsRemoteId;
		std::map<LocalSessionId, SessionPtr> sessionsLocalId;

		void checkExpiredSessions() noexcept;
		void removeSession(const SessionPtr& aSession) noexcept;
		TimerPtr expirationTimer;

		void on(WebServerManagerListener::Started) noexcept;
		void on(WebServerManagerListener::Stopped) noexcept;
		void on(WebServerManagerListener::LoadSettings, SimpleXML& aXml) noexcept;
		void on(WebServerManagerListener::SaveSettings, SimpleXML& aXml) noexcept;

		WebServerManager* server;
	};
}

#endif