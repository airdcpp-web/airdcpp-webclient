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

#include "stdinc.h"

#include <web-server/WebUserManager.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>

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
#include <boost/range/algorithm/count_if.hpp>


#define FLOOD_COUNT 5
#define FLOOD_PERIOD 45

namespace webserver {
	WebUserManager::WebUserManager(WebServerManager* aServer) : server(aServer), authFloodCounter(FLOOD_COUNT, FLOOD_PERIOD) {
		aServer->addListener(this);
	}

	WebUserManager::~WebUserManager() {
		server->removeListener(this);
	}

	SessionPtr WebUserManager::parseHttpSession(const string& aAuthToken, const string& aIP) {
		auto token = aAuthToken;

		bool basicAuth = false;
		if (token.length() > 6 && token.substr(0, 6) == "Basic ") {
			token = websocketpp::base64_decode(token.substr(6));
			basicAuth = true;
		}

		auto session = getSession(token);
		if (!session) {
			if (basicAuth) {
				string username, password;

				auto i = token.rfind(':');
				if (i == string::npos) {
					throw std::domain_error("Invalid authorization token format");
				}

				username = token.substr(0, i);
				password = token.substr(i + 1);

				return authenticateSession(username, password, Session::TYPE_BASIC_AUTH, 60, aIP, token);
			} else {
				throw std::domain_error("Invalid authorization token (session expired?)");
			}
		}

		return session;
	}

	SessionPtr WebUserManager::authenticateSession(const string& aUserName, const string& aPassword, Session::SessionType aType, uint64_t aMaxInactivityMinutes, const string& aIP) {
		auto uuid = boost::uuids::to_string(boost::uuids::random_generator()());
		return authenticateSession(aUserName, aPassword, aType, aMaxInactivityMinutes, aIP, uuid);
	}

	SessionPtr WebUserManager::authenticateSession(const string& aUserName, const string& aPassword, Session::SessionType aType, uint64_t aMaxInactivityMinutes, const string& aIP, const string& aSessionToken) {
		if (!authFloodCounter.checkFlood(aIP)) {
			server->log("Multiple failed login attempts detected from IP " + aIP, LogMessage::SEV_WARNING);
			throw std::domain_error("Too many failed login attempts detected (wait for a while before retrying)");
		}

		auto user = getUser(aUserName);
		if (!user || user->getPassword() != aPassword) {
			authFloodCounter.addAttempt(aIP);
			throw std::domain_error("Invalid username or password");
		}

		return createSession(user, aSessionToken, aType, aMaxInactivityMinutes, aIP);
	}

	SessionPtr WebUserManager::createSession(const WebUserPtr& aUser, const string& aSessionToken, Session::SessionType aType, uint64_t aMaxInactivityMinutes, const string& aIP) noexcept {
		dcassert(aType != Session::TYPE_BASIC_AUTH || aSessionToken.find(':') != string::npos);

		auto session = std::make_shared<Session>(aUser, aSessionToken, aType, server, aMaxInactivityMinutes, aIP);

		aUser->setLastLogin(GET_TIME());
		aUser->addSession();
		fire(WebUserManagerListener::UserUpdated(), aUser);

		{
			WLock l(cs);

			// Single session per user when using basic auth
			dcassert(aType != Session::TYPE_BASIC_AUTH || boost::find_if(sessionsRemoteId | map_values, [&](const SessionPtr& aSession) {
				return aSession->getSessionType() == Session::TYPE_BASIC_AUTH && aSession->getUser() == aUser;
			}).base() == sessionsRemoteId.end());

			sessionsRemoteId.emplace(session->getAuthToken(), session);
			sessionsLocalId.emplace(session->getId(), session);
		}

		fire(WebUserManagerListener::SessionCreated(), session);
		return session;
	}

	SessionPtr WebUserManager::createExtensionSession(const string& aExtensionName) noexcept {
		auto uuid = boost::uuids::to_string(boost::uuids::random_generator()());

		// For internal use only (can't be used for logging in)
		auto user = std::make_shared<WebUser>(aExtensionName, Util::emptyString, true);

		return createSession(user, uuid, Session::TYPE_EXTENSION, WEBCFG(DEFAULT_SESSION_IDLE_TIMEOUT).uint64(), "localhost");
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

	size_t WebUserManager::getUserSessionCount() const noexcept {
		RLock l(cs);
		return boost::count_if(sessionsLocalId | map_values, [=](const SessionPtr& s) {
			return s->getSessionType() != Session::TYPE_EXTENSION;
		});
	}

	void WebUserManager::logout(const SessionPtr& aSession) {
		removeSession(aSession, false);

		auto socket = server->getSocket(aSession->getId());
		if (socket) {
			resetSocketSession(socket);
		} else {
			dcdebug("No socket for session %s\n", aSession->getAuthToken().c_str());
		}

		dcdebug("Session %s logging out, use count: %ld\n", aSession->getAuthToken().c_str(), aSession.use_count());
	}

	void WebUserManager::resetSocketSession(const WebSocketPtr& aSocket) noexcept {
		if (aSocket->getSession()) {
			dcdebug("Resetting socket for session %s\n", aSocket->getSession()->getAuthToken().c_str());
			aSocket->getSession()->onSocketDisconnected();
			aSocket->setSession(nullptr);
		}
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
		expirationTimer = server->addTimer([this] { 
			checkExpiredSessions(); 
			authFloodCounter.prune();
		}, FLOOD_PERIOD * 1000);

		expirationTimer->start(false);
	}

	void WebUserManager::on(WebServerManagerListener::Stopping) noexcept {

	}

	void WebUserManager::on(WebServerManagerListener::Stopped) noexcept {
		expirationTimer = nullptr;

		// Let the modules handle deletion in a clean way before we are shutting down...
		SessionList sessions;

		{
			WLock l(cs);
			boost::copy(sessionsLocalId | map_values, back_inserter(sessions));

			sessionsLocalId.clear();
			sessionsRemoteId.clear();
		}

		while (true) {
			if (all_of(sessions.begin(), sessions.end(), [](const SessionPtr& aSession) {
				return aSession.unique();
			})) {
				break;
			}

			Thread::sleep(50);
		}
	}

	void WebUserManager::on(WebServerManagerListener::SocketDisconnected, const WebSocketPtr& aSocket) noexcept {
		resetSocketSession(aSocket);
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