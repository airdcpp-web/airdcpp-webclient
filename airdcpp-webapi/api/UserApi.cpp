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

#include <api/UserApi.h>

#include <web-server/JsonUtil.h>
#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/user/ignore/IgnoreManager.h>

#include <airdcpp/favorites/FavoriteUserManager.h>
#include <airdcpp/favorites/ReservedSlotManager.h>

namespace webserver {
	UserApi::UserApi(Session* aSession) : SubscribableApiModule(aSession, Access::ANY) {
		createSubscriptions({
			"user_connected",
			"user_updated",
			"user_disconnected",

			"ignored_user_added",
			"ignored_user_removed"
		});

		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("user"), CID_PARAM),		UserApi::handleGetUser); // DEPRECATED
		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(CID_PARAM),							UserApi::handleGetUser);
		METHOD_HANDLER(Access::ANY,				METHOD_POST,	(EXACT_PARAM("search_nicks")),			UserApi::handleSearchNicks);
		METHOD_HANDLER(Access::ANY,				METHOD_POST,	(EXACT_PARAM("search_hinted_user")),	UserApi::handleSearchHintedUser);

		METHOD_HANDLER(Access::SETTINGS_VIEW,	METHOD_GET,		(EXACT_PARAM("ignores")),				UserApi::handleGetIgnores);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("ignores"), CID_PARAM),	UserApi::handleIgnore);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(EXACT_PARAM("ignores"), CID_PARAM),	UserApi::handleUnignore);

		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("slots"), CID_PARAM),		UserApi::handleGrantSlot);

		ClientManager::getInstance()->addListener(this);
		IgnoreManager::getInstance()->addListener(this);
	}

	UserApi::~UserApi() {
		ClientManager::getInstance()->removeListener(this);
		IgnoreManager::getInstance()->removeListener(this);
	}

	UserPtr UserApi::getUser(ApiRequest& aRequest) {
		return Deserializer::getUser(aRequest.getCIDParam(), true);
	}

	api_return UserApi::handleGetUser(ApiRequest& aRequest) {
		auto user = getUser(aRequest);
		aRequest.setResponseBody(Serializer::serializeUser(user));
		return http_status::ok;
	}

	api_return UserApi::handleSearchNicks(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto pattern = JsonUtil::getField<string>("pattern", reqJson);
		auto maxResults = JsonUtil::getField<size_t>("max_results", reqJson);
		auto ignorePrefixes = JsonUtil::getOptionalFieldDefault<bool>("ignore_prefixes", reqJson, true);
		auto hubs = Deserializer::deserializeHubUrls(reqJson);

		auto users = ClientManager::getInstance()->searchNicks(pattern, maxResults, ignorePrefixes, hubs);
		aRequest.setResponseBody(Serializer::serializeList(users, Serializer::serializeOnlineUser));
		return http_status::ok;
	}

	api_return UserApi::handleSearchHintedUser(ApiRequest& aRequest) {
		const auto user = Deserializer::deserializeHintedUser(aRequest.getRequestBody(), true);
		aRequest.setResponseBody(Serializer::serializeHintedUser(user));
		return http_status::ok;
	}

	json UserApi::serializeConnectResult(const optional<UserConnectResult> aResult) noexcept {
		if (!aResult) {
			return nullptr;
		}

		return {
			{ "success", aResult->getIsSuccess() },
			{ "error", aResult->getError() },
		};
	}

	api_return UserApi::handleGrantSlot(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		auto user = getUser(aRequest);

		auto hubUrl = JsonUtil::getOptionalFieldDefault<string>("hub_url", reqJson, Util::emptyString);
		auto duration = JsonUtil::getOptionalFieldDefault<time_t>("duration", reqJson, 0);

		auto result = FavoriteUserManager::getInstance()->getReservedSlots().reserveSlot(HintedUser(user, hubUrl), duration);

		aRequest.setResponseBody({
			{ "connect_result", serializeConnectResult(result) }
		});

		return http_status::ok;
	}

	api_return UserApi::handleIgnore(ApiRequest& aRequest) {
		auto u = getUser(aRequest);
		IgnoreManager::getInstance()->storeIgnore(u);
		return http_status::no_content;
	}

	api_return UserApi::handleUnignore(ApiRequest& aRequest) {
		auto u = getUser(aRequest);
		IgnoreManager::getInstance()->removeIgnore(u);
		return http_status::no_content;
	}

	api_return UserApi::handleGetIgnores(ApiRequest& aRequest) {
		auto j = json::array();

		auto users = IgnoreManager::getInstance()->getIgnoredUsers();
		for (const auto& u : users) {
			j.push_back({
				{ "user", Serializer::serializeUser(u.first) },
				{ "ignored_messages", u.second }
			});
		}

		aRequest.setResponseBody(j);
		return http_status::ok;
	}

	void UserApi::on(IgnoreManagerListener::IgnoreAdded, const UserPtr& aUser) noexcept {
		maybeSend("ignored_user_added", [&] {
			return Serializer::serializeUser(aUser);
		});
	}

	void UserApi::on(IgnoreManagerListener::IgnoreRemoved, const UserPtr& aUser) noexcept {
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