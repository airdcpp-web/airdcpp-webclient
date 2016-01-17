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
	UserApi::UserApi(Session* aSession) : ApiModule(aSession, Access::ANY) {

		ClientManager::getInstance()->addListener(this);
		MessageManager::getInstance()->addListener(this);

		METHOD_HANDLER("ignores", Access::SETTINGS_VIEW, ApiRequest::METHOD_GET, (), false, UserApi::handleGetIgnores);
		METHOD_HANDLER("ignore", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (CID_PARAM), false, UserApi::handleIgnore);
		METHOD_HANDLER("ignore", Access::SETTINGS_EDIT, ApiRequest::METHOD_DELETE, (CID_PARAM), false, UserApi::handleUnignore);

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
		auto u = ClientManager::getInstance()->findUser(Deserializer::parseCID(aRequest.getStringParam(0)));
		if (!u) {
			throw std::invalid_argument("User not found");
		}

		return u;
	}

	api_return UserApi::handleIgnore(ApiRequest& aRequest) {
		auto u = getUser(aRequest);
		MessageManager::getInstance()->storeIgnore(u);
		return websocketpp::http::status_code::ok;
	}

	api_return UserApi::handleUnignore(ApiRequest& aRequest) {
		auto u = getUser(aRequest);
		MessageManager::getInstance()->removeIgnore(u);
		return websocketpp::http::status_code::ok;
	}

	api_return UserApi::handleGetIgnores(ApiRequest& aRequest) {
		auto j = json::array();

		auto users = MessageManager::getInstance()->getIgnoredUsers();
		for (const auto& u : users) {
			j.push_back(Serializer::serializeUser(u));
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