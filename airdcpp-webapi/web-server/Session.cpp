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
#include <web-server/Session.h>
#include <web-server/ApiRequest.h>

#include <api/ConnectivityApi.h>
#include <api/FavoriteDirectoryApi.h>
#include <api/FavoriteHubApi.h>
#include <api/FilelistApi.h>
#include <api/FilesystemApi.h>
#include <api/HashApi.h>
#include <api/HistoryApi.h>
#include <api/HubApi.h>
#include <api/LogApi.h>
#include <api/PrivateChatApi.h>
#include <api/QueueApi.h>
#include <api/RecentHubApi.h>
#include <api/SearchApi.h>
#include <api/SettingApi.h>
#include <api/ShareApi.h>
#include <api/ShareProfileApi.h>
#include <api/ShareRootApi.h>
#include <api/SystemApi.h>
#include <api/TransferApi.h>
#include <api/UserApi.h>
#include <api/WebUserApi.h>
#include <api/ViewFileApi.h>

#include <airdcpp/TimerManager.h>


namespace webserver {
#define ADD_MODULE(name, type) (apiHandlers.emplace(name, LazyModuleWrapper([this] { return unique_ptr<type>(new type(this)); })))

	Session::Session(WebUserPtr& aUser, const string& aToken, bool aIsSecure, WebServerManager* aServer, uint64_t maxInactivityMinutes, bool aIsUserSession) :
		id(Util::rand()), user(aUser), token(aToken), started(GET_TICK()), 
		lastActivity(GET_TICK()), secure(aIsSecure), server(aServer), 
		maxInactivity(maxInactivityMinutes*1000*60), userSession(aIsUserSession) {

		ADD_MODULE("connectivity", ConnectivityApi);
		ADD_MODULE("favorite_directories", FavoriteDirectoryApi);
		ADD_MODULE("favorite_hubs", FavoriteHubApi);
		ADD_MODULE("filelists", FilelistApi);
		ADD_MODULE("filesystem", FilesystemApi);
		ADD_MODULE("hash", HashApi);
		ADD_MODULE("histories", HistoryApi);
		ADD_MODULE("hubs", HubApi);
		ADD_MODULE("log", LogApi);
		ADD_MODULE("private_chat", PrivateChatApi);
		ADD_MODULE("queue", QueueApi);
		ADD_MODULE("recent_hubs", RecentHubApi);
		ADD_MODULE("search", SearchApi);
		ADD_MODULE("settings", SettingApi);
		ADD_MODULE("share", ShareApi);
		ADD_MODULE("share_profiles", ShareProfileApi);
		ADD_MODULE("share_roots", ShareRootApi);
		ADD_MODULE("system", SystemApi);
		ADD_MODULE("transfers", TransferApi);
		ADD_MODULE("users", UserApi);
		ADD_MODULE("web_users", WebUserApi);
		ADD_MODULE("view_files", ViewFileApi);
	}

	Session::~Session() {
		dcdebug("Session %s was deleted\n", token.c_str());
	}

	ApiModule* Session::getModule(const string& aModule) {
		auto h = apiHandlers.find(aModule);
		return h != apiHandlers.end() ? h->second.get() : nullptr;
	}

	websocketpp::http::status_code::value Session::handleRequest(ApiRequest& aRequest) {
		auto h = apiHandlers.find(aRequest.getApiModule());
		if (h != apiHandlers.end()) {
			if (aRequest.getApiVersion() != h->second->getVersion()) {
				aRequest.setResponseErrorStr("Invalid API version");
				return websocketpp::http::status_code::precondition_failed;
			}

			return h->second->handleRequest(aRequest);
		}

		aRequest.setResponseErrorStr("Section not found");
		return websocketpp::http::status_code::not_found;
	}

	void Session::onSocketConnected(const WebSocketPtr& aSocket) noexcept {
		auto oldSocket = getServer()->getSocket(id);
		if (oldSocket) {
			oldSocket->debugMessage("Replace session socket");

			// This must be called before the new socket is associated with this session
			fire(SessionListener::SocketConnected(), oldSocket);
			oldSocket->setSession(nullptr);

			oldSocket->close(websocketpp::close::status::policy_violation, "Another socket was connected to this session");
		}

		fire(SessionListener::SocketConnected(), aSocket);
	}

	void Session::onSocketDisconnected() noexcept {
		// Set the expiration time from this moment if there is no further activity
		updateActivity();

		fire(SessionListener::SocketDisconnected());
	}

	void Session::updateActivity() noexcept {
		lastActivity = GET_TICK();
	}
}
