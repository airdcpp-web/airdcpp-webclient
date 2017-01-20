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

#include <api/UserApi.h>

#include <web-server/JsonUtil.h>
#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <airdcpp/ClientManager.h>
#include <airdcpp/MessageManager.h>


namespace webserver {
	UserApi::UserApi(Session* aSession) : SubscribableApiModule(aSession, Access::ANY) {

		ClientManager::getInstance()->addListener(this);
		MessageManager::getInstance()->addListener(this);

		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("user"), CID_PARAM),	UserApi::handleGetUser);
		METHOD_HANDLER(Access::ANY,				METHOD_POST,	(EXACT_PARAM("search_nicks")),		UserApi::handleSearchNicks);

		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_GET,		(EXACT_PARAM("ignores")),			UserApi::handleGetIgnores);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("ignores"), CID_PARAM),	UserApi::handleIgnore);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(EXACT_PARAM("ignores"), CID_PARAM),	UserApi::handleUnignore);

		createSubscription("user_connected");
		createSubscription("user_updated");
		createSubscription("user_disconnected");

		createSubscription("ignored_user_added");
		createSubscription("ignored_user_removed");
	}

	UserApi::~UserApi() {
		ClientManager::getInstance()->removeListener(this);
		MessageManager::getInstance()->removeListener(this);
	}

	UserPtr UserApi::getUser(ApiRequest& aRequest) {
		return Deserializer::getUser(aRequest.getCIDParam(), true);
	}

	api_return UserApi::handleGetUser(ApiRequest& aRequest) {
		auto user = getUser(aRequest);
		aRequest.setResponseBody(Serializer::serializeUser(user));
		return websocketpp::http::status_code::ok;
	}

	api_return UserApi::handleSearchNicks(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto pattern = JsonUtil::getField<string>("pattern", reqJson);
		auto maxResults = JsonUtil::getField<size_t>("max_results", reqJson);
		auto ignorePrefixes = JsonUtil::getOptionalFieldDefault<bool>("ignore_prefixes", reqJson, true);
		auto hubs = Deserializer::deserializeHubUrls(reqJson);

		auto users = ClientManager::getInstance()->searchNicks(pattern, maxResults, ignorePrefixes, hubs);
		aRequest.setResponseBody(Serializer::serializeList(users, Serializer::serializeOnlineUser));
		return websocketpp::http::status_code::ok;
	}

	api_return UserApi::handleIgnore(ApiRequest& aRequest) {
		auto u = getUser(aRequest);
		MessageManager::getInstance()->storeIgnore(u);
		return websocketpp::http::status_code::no_content;
	}

	api_return UserApi::handleUnignore(ApiRequest& aRequest) {
		auto u = getUser(aRequest);
		MessageManager::getInstance()->removeIgnore(u);
		return websocketpp::http::status_code::no_content;
	}

	api_return UserApi::handleGetIgnores(ApiRequest& aRequest) {
		auto j = json::array();

		auto users = MessageManager::getInstance()->getIgnoredUsers();
		for (const auto& u : users) {
			j.push_back({
				{ "user", Serializer::serializeUser(u.first) },
				{ "ignored_messages", u.second }
			});
		}

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	void UserApi::on(MessageManagerListener::IgnoreAdded, const UserPtr& aUser) noexcept {
		maybeSend("ignored_user_added", [&] {
			return Serializer::serializeUser(aUser);
		});
	}

	void UserApi::on(MessageManagerListener::IgnoreRemoved, const UserPtr& aUser) noexcept {
		maybeSend("ignored_user_removed", [&] {
			return Serializer::serializeUser(aUser);
		});
	}

	void UserApi::on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool aWasOffline) noexcept {
		if (aUser.getUser()->getCID() == CID())
			return;

		maybeSend("user_connected", [&] {
			return json({
				{ "user", Serializer::serializeUser(aUser) },
				{ "was_offline", aWasOffline },
			});
		});
	}

	void UserApi::on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept {
		if (aUser.getUser()->getCID() == CID())
			return;

		maybeSend("user_updated", [&] {
			return Serializer::serializeUser(aUser);
		});
	}

	void UserApi::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool aWentOffline) noexcept {
		if (aUser->getCID() == CID())
			return;

		maybeSend("user_disconnected", [&] {
			return json({
				{ "user", Serializer::serializeUser(aUser) },
				{ "went_offline", aWentOffline },
			});
		});
	}
}