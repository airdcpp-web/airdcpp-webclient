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

#include <web-server/WebUserManager.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/typedefs.h>

#include <airdcpp/ActivityManager.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/TimerManager.h>
#include <airdcpp/Util.h>

#ifdef _WIN32
#include <Wincrypt.h>
#endif

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


namespace webserver {
	WebUserManager::WebUserManager(WebServerManager* aServer) : server(aServer){
		aServer->addListener(this);
	}

	WebUserManager::~WebUserManager() {
		server->removeListener(this);
	}

	SessionPtr WebUserManager::parseHttpSession(const websocketpp::http::parser::request& aRequest, string& error_, const string& aIp) noexcept {
		bool basicAuth = false;

		auto token = aRequest.get_header("Authorization");
		if (token != websocketpp::http::empty_header) {
			if (token.length() > 6 && token.substr(0, 6) == "Basic ") {
				token = websocketpp::base64_decode(token.substr(6));
				basicAuth = true;
			}
		} else {
			return nullptr;
		}

		auto session = getSession(token);
		if (!session) {
			if (basicAuth) {
				session = authenticateBasicHttp(token, aIp);
				if (!session) {
					error_ = "Invalid username or password";
				}
			} else {
				error_ = "Invalid authorization token (session expired?)";
			}
		}

		return session;
	}

	SessionPtr WebUserManager::authenticateSession(const string& aUserName, const string& aPassword, bool aIsSecure, uint64_t aMaxInactivityMinutes, const string& aIP) noexcept {
		auto user = getUser(aUserName);
		if (!user) {
			return nullptr;
		}

		if (user->getPassword() != aPassword) {
			return nullptr;
		}

		auto uuid = boost::uuids::to_string(boost::uuids::random_generator()());
		return createSession(user, uuid, aIsSecure ? Session::TYPE_SECURE : Session::TYPE_PLAIN, aMaxInactivityMinutes, aIP);
	}

	SessionPtr WebUserManager::authenticateBasicHttp(const string& aAuthString, const string& aIP) noexcept {

		string username, password;

		auto i = aAuthString.rfind(':');
		if (i == string::npos) {
			return nullptr;
		}

		username = aAuthString.substr(0, i);
		password = aAuthString.substr(i + 1);

		auto user = getUser(username);
		if (!user) {
			return nullptr;
		}

		if (user->getPassword() != password) {
			return nullptr;
		}

		return createSession(user, aAuthString, Session::TYPE_BASIC_AUTH, 60, aIP);
	}

	SessionPtr WebUserManager::createSession(const WebUserPtr& aUser, const string& aSessionToken, Session::SessionType aType, uint64_t aMaxInactivityMinutes, const string& aIP) {
		auto session = std::make_shared<Session>(aUser, aSessionToken, aType, server, aMaxInactivityMinutes, aIP);

		aUser->setLastLogin(GET_TIME());
		aUser->addSession();
		fire(WebUserManagerListener::UserUpdated(), aUser);

		{
			WLock l(cs);
			sessionsRemoteId.emplace(session->getAuthToken(), session);
			sessionsLocalId.emplace(session->getId(), session);
		}

		fire(WebUserManagerListener::SessionCreated(), session);
		return session;
	}

	SessionList WebUserManager::getSessions() const noexcept {
		SessionList ret;

		RLock l(cs);
		boost::range::copy(sessionsLocalId | map_values, back_inserter(ret));

		return ret;
	}

	SessionPtr WebUserManager::getSession(const string& aSession) const noexcept {
		RLock l(cs);
		auto s = sessionsRemoteId.find(aSession);

		if (s == sessionsRemoteId.end()) {
			return nullptr;
		}

		return s->second;
	}

	SessionPtr WebUserManager::getSession(LocalSessionId aId) const noexcept {
		RLock l(cs);
		auto s = sessionsLocalId.find(aId);
		if (s == sessionsLocalId.end()) {
			return nullptr;
		}

		return s->second;
	}

	size_t WebUserManager::getSessionCount() const noexcept {
		RLock l(cs);
		return sessionsLocalId.size();
	}

	void WebUserManager::logout(const SessionPtr& aSession) {
		aSession->onSocketDisconnected();
		
		removeSession(aSession, false);
	}

	void WebUserManager::checkExpiredSessions() noexcept {
		SessionList removedSession;
		auto tick = GET_TICK();

		{
			RLock l(cs);
			boost::algorithm::copy_if(sessionsLocalId | map_values, back_inserter(removedSession), [=](const SessionPtr& s) {
				return s->getMaxInactivity() > 0 && s->getLastActivity() + s->getMaxInactivity() < tick;
			});
		}

		for (const auto& s : removedSession) {
			// Don't remove sessions with active socket
			if (!server->getSocket(s->getId())) {
				removeSession(s, true);
			}
		}
	}

	void WebUserManager::removeSession(const SessionPtr& aSession, bool aTimedOut) noexcept {
		aSession->getUser()->removeSession();
		fire(WebUserManagerListener::UserUpdated(), aSession->getUser());

		{
			WLock l(cs);
			sessionsRemoteId.erase(aSession->getAuthToken());
			sessionsLocalId.erase(aSession->getId());
		}

		fire(WebUserManagerListener::SessionRemoved(), aSession, aTimedOut);
	}

	void WebUserManager::on(WebServerManagerListener::Started) noexcept {
		expirationTimer = server->addTimer([this] { checkExpiredSessions(); }, 60*1000);
		expirationTimer->start(false);
	}

	void WebUserManager::on(WebServerManagerListener::Stopped) noexcept {
		expirationTimer = nullptr;

		// Let the modules handle deletion in a clean way before we are shutting down...
		WLock l(cs);
		sessionsLocalId.clear();
		sessionsRemoteId.clear();
	}

	void WebUserManager::on(WebServerManagerListener::LoadSettings, SimpleXML& xml_) noexcept {
		if (xml_.findChild("WebUsers")) {
			xml_.stepIn();
			while (xml_.findChild("WebUser")) {
				const auto& username = xml_.getChildAttrib("Username");
				const auto& password = xml_.getChildAttrib("Password");

				if (username.empty() || password.empty()) {
					continue;
				}

				const auto& permissions = xml_.getChildAttrib("Permissions");

				// Set as admin mainly for compatibility with old accounts if no permissions were found
				auto user = std::make_shared<WebUser>(username, password, permissions.empty());

				user->setLastLogin(xml_.getIntChildAttrib("LastLogin"));
				if (!permissions.empty()) {
					user->setPermissions(permissions);
				}

				users.emplace(username, user);

			}
			xml_.stepOut();
		}

		xml_.resetCurrentChild();
	}

	void WebUserManager::on(WebServerManagerListener::SaveSettings, SimpleXML& xml_) noexcept {
		xml_.addTag("WebUsers");
		xml_.stepIn();
		{
			RLock l(cs);
			for (const auto& u : users | map_values) {
				xml_.addTag("WebUser");
				xml_.addChildAttrib("Username", u->getUserName());
				xml_.addChildAttrib("Password", u->getPassword());
				xml_.addChildAttrib("LastLogin", u->getLastLogin());
				xml_.addChildAttrib("Permissions", u->getPermissionsStr());
			}
		}
		xml_.stepOut();
	}

	bool WebUserManager::hasUsers() const noexcept {
		RLock l(cs);
		return !users.empty();
	}

	bool WebUserManager::hasUser(const string& aUserName) const noexcept {
		RLock l(cs);
		return users.find(aUserName) != users.end();
	}

	bool WebUserManager::addUser(const WebUserPtr& aUser) noexcept {
		auto user = getUser(aUser->getUserName());
		if (user) {
			return false;
		}

		{
			WLock l(cs);
			users.emplace(aUser->getUserName(), aUser);
		}

		fire(WebUserManagerListener::UserAdded(), aUser);
		return true;
	}

	WebUserPtr WebUserManager::getUser(const string& aUserName) const noexcept {
		RLock l(cs);
		auto user = users.find(aUserName);
		if (user == users.end()) {
			return nullptr;
		}

		return user->second;
	}

	bool WebUserManager::updateUser(const WebUserPtr& aUser) noexcept {
		fire(WebUserManagerListener::UserUpdated(), aUser);
		return true;
	}

	bool WebUserManager::removeUser(const string& aUserName) noexcept {
		auto user = getUser(aUserName);
		if (!user) {
			return false;
		}

		{
			WLock l(cs);
			users.erase(aUserName);
		}

		fire(WebUserManagerListener::UserRemoved(), user);
		return true;
	}

	StringList WebUserManager::getUserNames() const noexcept {
		StringList ret;

		RLock l(cs);
		boost::copy(users | map_keys, back_inserter(ret));
		return ret;
	}

	WebUserList WebUserManager::getUsers() const noexcept {
		WebUserList ret;

		RLock l(cs);
		boost::copy(users | map_values, back_inserter(ret));
		return ret;
	}

	void WebUserManager::replaceWebUsers(const WebUserList& newUsers) noexcept {
		WLock l(cs);
		users.clear();
		for (auto u : newUsers)
			users.emplace(u->getUserName(), u);
	}

}