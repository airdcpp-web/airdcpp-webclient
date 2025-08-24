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

#include "stdinc.h"

#include <api/WebUserApi.h>
#include <api/WebUserUtils.h>

#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>

#include <web-server/JsonUtil.h>
#include <web-server/Session.h>
#include <web-server/WebUserManager.h>

#define USERNAME_PARAM "username"
namespace webserver {
	WebUserApi::WebUserApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::ADMIN),
		view("web_user_view", this, WebUserUtils::propertyHandler, std::bind(&WebUserApi::getUsers, this)),
		um(aSession->getServer()->getUserManager()) 
	{
		createSubscriptions({ "web_user_added", "web_user_updated", "web_user_removed" });

		METHOD_HANDLER(Access::ADMIN, METHOD_GET,		(),								WebUserApi::handleGetUsers);

		METHOD_HANDLER(Access::ADMIN, METHOD_POST,		(),								WebUserApi::handleAddUser);
		METHOD_HANDLER(Access::ADMIN, METHOD_GET,		(STR_PARAM(USERNAME_PARAM)),	WebUserApi::handleGetUser);
		METHOD_HANDLER(Access::ADMIN, METHOD_PATCH,		(STR_PARAM(USERNAME_PARAM)),	WebUserApi::handleUpdateUser);
		METHOD_HANDLER(Access::ADMIN, METHOD_DELETE,	(STR_PARAM(USERNAME_PARAM)),	WebUserApi::handleRemoveUser);

		um.addListener(this);
	}

	WebUserApi::~WebUserApi() {
		um.removeListener(this);
	}

	WebUserList WebUserApi::getUsers() const noexcept {
		return um.getUsers();
	}

	api_return WebUserApi::handleGetUsers(ApiRequest& aRequest) {
		auto j = Serializer::serializeItemList(WebUserUtils::propertyHandler, getUsers());
		aRequest.setResponseBody(j);
		return http_status::ok;
	}

	WebUserPtr WebUserApi::parseUserNameParam(ApiRequest& aRequest) {
		const auto& userName = aRequest.getStringParam(USERNAME_PARAM);
		auto user = um.getUser(userName);
		if (!user) {
			throw RequestException(http_status::not_found, "User " + userName + " was not found");
		}

		return user;
	}

	api_return WebUserApi::handleGetUser(ApiRequest& aRequest) {
		const auto& user = parseUserNameParam(aRequest);

		aRequest.setResponseBody(Serializer::serializeItem(user, WebUserUtils::propertyHandler));
		return http_status::ok;
	}

	bool WebUserApi::updateUserProperties(WebUserPtr& aUser, const json& j, bool aIsNew) {
		auto hasChanges = false;

		{
			auto password = JsonUtil::getOptionalField<string>("password", j, aIsNew);
			if (password) {
				aUser->setPassword(*password);
				hasChanges = true;
			}
		}

		{
			auto permissions = JsonUtil::getOptionalField<StringList>("permissions", j);
			if (permissions) {
				aUser->setPermissions(*permissions);
				hasChanges = true;
			}
		}

		return hasChanges;
	}

	api_return WebUserApi::handleAddUser(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto userName = JsonUtil::getField<string>("username", reqJson, false);
		if (!WebUser::validateUsername(userName)) {
			JsonUtil::throwError("username", JsonException::ERROR_INVALID, "The username should only contain alphanumeric characters");
		}

		auto user = std::make_shared<WebUser>(userName, Util::emptyString);

		updateUserProperties(user, reqJson, true);

		if (!um.addUser(user)) {
			JsonUtil::throwError("username", JsonException::ERROR_EXISTS, "User with the same name exists already");
		}

		aRequest.setResponseBody(Serializer::serializeItem(user, WebUserUtils::propertyHandler));
		return http_status::ok;
	}

	api_return WebUserApi::handleUpdateUser(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		auto user = parseUserNameParam(aRequest);

		auto hasChanges = updateUserProperties(user, reqJson, false);
		if (hasChanges) {
			um.updateUser(user, aRequest.getSession()->getUser() != user);
		}

		aRequest.setResponseBody(Serializer::serializeItem(user, WebUserUtils::propertyHandler));
		return http_status::ok;
	}

	api_return WebUserApi::handleRemoveUser(ApiRequest& aRequest) {
		const auto& userName = aRequest.getStringParam(USERNAME_PARAM);
		if (!um.removeUser(userName)) {
			aRequest.setResponseErrorStr("User " + userName + " was not found");
			return http_status::not_found;
		}

		return http_status::no_content;
	}

	void WebUserApi::on(WebUserManagerListener::UserAdded, const WebUserPtr& aUser) noexcept {
		view.onItemAdded(aUser);

		maybeSend("web_user_added", [&] { 
			return Serializer::serializeItem(aUser, WebUserUtils::propertyHandler); 
		});
	}

	void WebUserApi::on(WebUserManagerListener::UserUpdated, const WebUserPtr& aUser) noexcept {
		view.onItemUpdated(aUser, toPropertyIdSet(WebUserUtils::properties));

		maybeSend("web_user_updated", [&] { 
			return Serializer::serializeItem(aUser, WebUserUtils::propertyHandler); 
		});
	}

	void WebUserApi::on(WebUserManagerListener::UserRemoved, const WebUserPtr& aUser) noexcept {
		view.onItemRemoved(aUser);

		maybeSend("web_user_removed", [&] { 
			return Serializer::serializeItem(aUser, WebUserUtils::propertyHandler); 
		});
	}
}