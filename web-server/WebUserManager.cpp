/*
* Copyright (C) 2011-2015 AirDC++ Project
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
#include <airdcpp/SimpleXML.h>
#include <airdcpp/TimerManager.h>
#include <airdcpp/Util.h>

namespace webserver {
	WebUserManager::WebUserManager(WebServerManager* aServer) : server(aServer){
		aServer->addListener(this);
	}

	WebUserManager::~WebUserManager() {
		server->removeListener(this);
	}

	SessionPtr WebUserManager::authenticate(const string& aUserName, const string& aPassword, bool aIsSecure, uint64_t aMaxInactivityMinutes) noexcept {
		WLock l(cs);

		auto u = users.find(aUserName);
		if (u == users.end()) {
			return nullptr;
		}

		if (u->second->getPassword() != aPassword) {
			return nullptr;
		}

		auto session = make_shared<Session>(u->second, Util::toString(Util::rand()), aIsSecure, server, aMaxInactivityMinutes);
		sessions.emplace(session->getToken(), session);
		return session;
	}

	SessionPtr WebUserManager::getSession(const string& aSession) const noexcept {
		RLock l(cs);
		auto s = sessions.find(aSession);

		if (s == sessions.end()) {
			return nullptr;
		}

		return s->second;
	}

	size_t WebUserManager::getSessionCount() const noexcept {
		RLock l(cs);
		return sessions.size();
	}

	void WebUserManager::logout(const SessionPtr& aSession) {
		aSession->onSocketDisconnected();

		WLock l(cs);
		sessions.erase(aSession->getToken());
	}

	void WebUserManager::checkExpiredSessions() noexcept {
		StringList removedTokens;
		auto tick = GET_TICK();

		{
			RLock l(cs);
			for (const auto& s: sessions | map_values) {
				if (s->getLastActivity() + s->getMaxInactivity() < tick) {
					removedTokens.push_back(s->getToken());
				}
			}
		}

		// Don't remove sessions with active socket
		removedTokens.erase(remove_if(removedTokens.begin(), removedTokens.end(), [this](const string& aToken) {
			return server->getSocket(aToken);
		}), removedTokens.end());

		if (!removedTokens.empty()) {
			WLock l(cs);
			for (const auto& token : removedTokens) {
				sessions.erase(token);
			}
		}
	}

	void WebUserManager::on(WebServerManagerListener::Started) noexcept {
		expirationTimer = server->addTimer([this] { checkExpiredSessions(); }, 60*1000);
		expirationTimer->start(false);
	}

	void WebUserManager::on(WebServerManagerListener::Stopped) noexcept {
		// Let the modules handle deletion in a clean way before we are shutting down...
		WLock l(cs);
		sessions.clear();
	}

	void WebUserManager::on(WebServerManagerListener::LoadSettings, SimpleXML& xml_) noexcept {
		if (xml_.findChild("WebUsers")) {
			xml_.stepIn();
			while (xml_.findChild("WebUser")) {
				const string& username = xml_.getChildAttrib("Username");
				const string& password = xml_.getChildAttrib("Password");

				if (username.empty() || password.empty()) {
					continue;
				}

				users.emplace(username, make_shared<WebUser>(username, password));

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
			for (auto& u : users | map_values) {
				xml_.addTag("WebUser");
				xml_.addChildAttrib("Username", u->getUserName());
				xml_.addChildAttrib("Password", u->getPassword());
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

	bool WebUserManager::addUser(const string& aUserName, const string& aPassword) noexcept {
		WLock l(cs);
		return users.emplace(aUserName, make_shared<WebUser>(aUserName, aPassword)).second;
	}

	bool WebUserManager::removeUser(const string& aUserName) noexcept {
		WLock l(cs);
		return users.erase(aUserName) > 0;
	}

	StringList WebUserManager::getUserNames() const noexcept {
		StringList ret;

		RLock l(cs);
		boost::copy(users | map_keys, back_inserter(ret));
		return ret;
	}

	std::vector<WebUserPtr> WebUserManager::getWebUsers() const noexcept {
		std::vector<WebUserPtr> ret;
		RLock l(cs);
		boost::copy(users | map_values, back_inserter(ret));
		return ret;
	}

	void WebUserManager::replaceWebUsers(std::vector<WebUserPtr>& newUsers) noexcept {
		WLock l(cs);
		users.clear();
		for (auto u : newUsers)
			users.emplace(u->getUserName(), u);
	}

}