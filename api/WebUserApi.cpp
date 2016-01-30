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

#include <api/WebUserApi.h>
#include <api/WebUserUtils.h>

#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>

#include <web-server/JsonUtil.h>
#include <web-server/Session.h>
#include <web-server/WebUserManager.h>

namespace webserver {
	WebUserApi::WebUserApi(Session* aSession) : ApiModule(aSession, Access::ADMIN), um(aSession->getServer()->getUserManager()), itemHandler(properties,
		WebUserUtils::getStringInfo, WebUserUtils::getNumericInfo, WebUserUtils::compareItems, WebUserUtils::serializeItem, WebUserUtils::filterItem),
		view("web_user_view", this, itemHandler, std::bind(&WebUserApi::getUsers, this)) {

		um.addListener(this);

		METHOD_HANDLER("users", Access::ADMIN, ApiRequest::METHOD_GET, (), false, WebUserApi::handleGetUsers);
		METHOD_HANDLER("user", Access::ADMIN, ApiRequest::METHOD_POST, (EXACT_PARAM("add")), true, WebUserApi::handleAddUser);
		METHOD_HANDLER("user", Access::ADMIN, ApiRequest::METHOD_POST, (EXACT_PARAM("update")), true, WebUserApi::handleUpdateUser);
		METHOD_HANDLER("user", Access::ADMIN, ApiRequest::METHOD_POST, (EXACT_PARAM("remove")), true, WebUserApi::handleRemoveUser);

		createSubscription("web_user_added");
		createSubscription("web_user_updated");
		createSubscription("web_user_removed");
	}

	WebUserApi::~WebUserApi() {
		um.removeListener(this);
	}

	WebUserList WebUserApi::getUsers() const noexcept {
		return um.getUsers();
	}

	api_return WebUserApi::handleGetUsers(ApiRequest& aRequest) {
		auto j = Serializer::serializeItemList(itemHandler, getUsers());
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return WebUserApi::handleAddUser(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto userName = JsonUtil::getField<string>("username", reqJson, false);

		auto user = std::make_shared<WebUser>(userName, Util::emptyString);

		parseUser(user, reqJson, true);

		if (!um.addUser(user)) {
			JsonUtil::throwError("username", JsonUtil::ERROR_EXISTS, "User with the same name exists");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
	}

	api_return WebUserApi::handleUpdateUser(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto userName = JsonUtil::getField<string>("username", reqJson, false);

		auto user = um.getUser(userName);
		if (!user) {
			aRequest.setResponseErrorStr("User not found");
			return websocketpp::http::status_code::not_found;
		}

		parseUser(user, reqJson, false);

		um.updateUser(user);
		return websocketpp::http::status_code::ok;
	}

	api_return WebUserApi::handleRemoveUser(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto userName = JsonUtil::getField<string>("username", reqJson, false);
		if (!um.removeUser(userName)) {
			aRequest.setResponseErrorStr("User not found");
			return websocketpp::http::status_code::not_found;
		}


		return websocketpp::http::status_code::ok;
	}

	void WebUserApi::on(WebUserManagerListener::UserAdded, const WebUserPtr& aUser) noexcept {
		view.onItemAdded(aUser);

		maybeSend("web_user_added", [&] { return Serializer::serializeItem(aUser, itemHandler); });
	}

	void WebUserApi::on(WebUserManagerListener::UserUpdated, const WebUserPtr& aUser) noexcept {
		view.onItemUpdated(aUser, toPropertyIdSet(properties));

		maybeSend("web_user_updated", [&] { return Serializer::serializeItem(aUser, itemHandler); });
	}

	void WebUserApi::on(WebUserManagerListener::UserRemoved, const WebUserPtr& aUser) noexcept {
		view.onItemRemoved(aUser);

		maybeSend("web_user_removed", [&] { return Serializer::serializeItem(aUser, itemHandler); });
	}

	void WebUserApi::parseUser(WebUserPtr& aUser, const json& j, bool aIsNew) {
		auto password = JsonUtil::getOptionalField<string>("password", j, false, aIsNew);
		if (password) {
			aUser->setPassword(*password);
		}

		auto permissions = JsonUtil::getOptionalField<StringList>("permissions", j, false, false);
		if (permissions) {
			// Only validate added profiles profiles
			aUser->setPermissions(*permissions);
		}
	}
}