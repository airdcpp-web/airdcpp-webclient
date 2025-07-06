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

#include <api/HubInfo.h>
#include <api/base/ApiModule.h>
#include <api/common/Serializer.h>
#include <api/FavoriteHubUtils.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/hub/ClientManager.h>


namespace webserver {
	const StringList HubInfo::subscriptionList = {
		"hub_updated",
		"hub_counts_updated",
		"hub_message",
		"hub_status",
		"hub_text_command",

		"hub_user_connected",
		"hub_user_updated",
		"hub_user_disconnected",
	};

	HubInfo::HubInfo(ParentType* aParentModule, const ClientPtr& aClient) :
		SubApiModule(aParentModule, aClient->getToken()), client(aClient),
		chatHandler(this, aClient.get(), "hub", Access::HUBS_VIEW, Access::HUBS_EDIT, Access::HUBS_SEND),
		view("hub_user_view", this, OnlineUserUtils::propertyHandler, std::bind(&HubInfo::getUsers, this), 500), 
		timer(getTimer([this] { onTimer(); }, 1000)) 
	{
		createSubscriptions(subscriptionList);

		METHOD_HANDLER(Access::HUBS_EDIT, METHOD_PATCH, (),							HubInfo::handleUpdateHub);

		METHOD_HANDLER(Access::HUBS_EDIT, METHOD_POST,	(EXACT_PARAM("reconnect")),	HubInfo::handleReconnect);
		METHOD_HANDLER(Access::HUBS_EDIT, METHOD_POST,	(EXACT_PARAM("favorite")),	HubInfo::handleFavorite);
		METHOD_HANDLER(Access::HUBS_EDIT, METHOD_POST,	(EXACT_PARAM("password")),	HubInfo::handlePassword);
		METHOD_HANDLER(Access::HUBS_EDIT, METHOD_POST,	(EXACT_PARAM("redirect")),	HubInfo::handleRedirect);

		METHOD_HANDLER(Access::HUBS_VIEW, METHOD_GET,	(EXACT_PARAM("counts")),	HubInfo::handleGetCounts);

		METHOD_HANDLER(Access::HUBS_VIEW, METHOD_GET,	(EXACT_PARAM("users"), RANGE_START_PARAM, RANGE_MAX_PARAM), HubInfo::handleGetUsers);
		METHOD_HANDLER(Access::HUBS_VIEW, METHOD_GET,	(EXACT_PARAM("users"), CID_PARAM),							HubInfo::handleGetUserCid);
		METHOD_HANDLER(Access::HUBS_VIEW, METHOD_GET,	(EXACT_PARAM("users"), TOKEN_PARAM),						HubInfo::handleGetUserId);
	}

	HubInfo::~HubInfo() {
		timer->stop(true);

		client->removeListener(this);
	}

	void HubInfo::init() noexcept {
		client->addListener(this);

		timer->start(false);
	}

	ClientToken HubInfo::getId() const noexcept {
		return client->getToken();
	}

	api_return HubInfo::handleUpdateHub(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		for (const auto& i : reqJson.items()) {
			auto key = i.key();
			if (key == "use_main_chat_notify") {
				client->setHubSetting(HubSettings::ChatNotify, JsonUtil::parseValue<bool>("chat_notify", i.value()));
			} else if (key == "show_joins") {
				client->setHubSetting(HubSettings::ShowJoins, JsonUtil::parseValue<bool>("show_joins", i.value()));
			} else if (key == "fav_show_joins") {
				client->setHubSetting(HubSettings::FavShowJoins, JsonUtil::parseValue<bool>("fav_show_joins", i.value()));
			}
		}

		return websocketpp::http::status_code::no_content;
	}

	api_return HubInfo::handleGetUsers(ApiRequest& aRequest) {
		OnlineUserList users;
		client->getUserList(users, false);

		auto start = aRequest.getRangeParam(START_POS);
		auto count = aRequest.getRangeParam(MAX_COUNT);

		auto j = Serializer::serializeItemList(start, count, OnlineUserUtils::propertyHandler, users);
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handleGetUserCid(ApiRequest& aRequest) {
		auto user = Deserializer::getUser(aRequest.getCIDParam(), true);

		auto ou = ClientManager::getInstance()->findOnlineUser(user->getCID(), client->getHubUrl(), false);
		if (!ou) {
			aRequest.setResponseErrorStr("User was not found");
			return websocketpp::http::status_code::not_found;
		}

		aRequest.setResponseBody(Serializer::serializeOnlineUser(ou));
		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handleGetUserId(ApiRequest& aRequest) {
		auto ou = client->findUser(aRequest.getTokenParam());
		if (!ou) {
			aRequest.setResponseErrorStr("User " + Util::toString(aRequest.getTokenParam()) + " was not found");
			return websocketpp::http::status_code::not_found;
		}

		aRequest.setResponseBody(Serializer::serializeOnlineUser(ou));
		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handleGetCounts(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeCounts(client));
		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handleReconnect(ApiRequest&) {
		client->reconnect();
		return websocketpp::http::status_code::no_content;
	}

	api_return HubInfo::handleFavorite(ApiRequest& aRequest) {
		auto favHub = client->saveFavorite();
		if (!favHub) {
			aRequest.setResponseErrorStr(STRING(FAVORITE_HUB_ALREADY_EXISTS));
			return websocketpp::http::status_code::bad_request;
		}

		aRequest.setResponseBody(Serializer::serializeItem(favHub, FavoriteHubUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handlePassword(ApiRequest& aRequest) {
		auto password = JsonUtil::getField<string>("password", aRequest.getRequestBody(), false);

		client->password(password);
		return websocketpp::http::status_code::no_content;
	}

	api_return HubInfo::handleRedirect(ApiRequest&) {
		client->doRedirect();
		return websocketpp::http::status_code::no_content;
	}

	json HubInfo::serializeIdentity(const ClientPtr& aClient) noexcept {
		return {
			{ "name", aClient->getHubName() },
			{ "description", aClient->getHubDescription() },
			{ "supports", aClient->getSupports().getAll() },
		};
	}


	json HubInfo::serializeSettings(const ClientPtr& aClient) noexcept {
		return {
			{ "nick", Serializer::serializeHubSetting(aClient->get(HubSettings::Nick)) },
			{ "use_main_chat_notify", Serializer::serializeHubSetting(aClient->get(HubSettings::ChatNotify)) },
			{ "show_joins", Serializer::serializeHubSetting(aClient->get(HubSettings::ShowJoins)) },
			{ "fav_show_joins", Serializer::serializeHubSetting(aClient->get(HubSettings::FavShowJoins)) },
		};
	}

	json HubInfo::serializeCounts(const ClientPtr& aClient) noexcept {
		return {
			{ "user_count", aClient->getUserCount() },
			{ "share_size", aClient->getTotalShare() },
		};
	}

	json HubInfo::serializeConnectState(const ClientPtr& aClient) noexcept {
		if (!aClient->getRedirectUrl().empty()) {
			return{
				{ "id", "redirect" },
				{ "str", "Redirect" },
				{ "data", {
					{ "hub_url", aClient->getRedirectUrl() },
				} },
			};
		}

		switch (aClient->getConnectState()) {
			case Client::STATE_PROTOCOL:
			case Client::STATE_IDENTIFY:
			case Client::STATE_VERIFY: {
				if (aClient->getPassword().empty()) {
					return {
						{ "id", "password" },
						{ "str", "Password requested" },
					};
				}
			}
			case Client::STATE_CONNECTING: {
				return {
					{ "id", "connecting" },
					{ "str", STRING(CONNECTING) },
				};
			}
			case Client::STATE_DISCONNECTED: 
			{
				if (aClient->isKeyprintMismatch()) {
					return{
						{ "id", "keyprint_mismatch" },
						{ "str", STRING(KEYPRINT_MISMATCH) },
					};
				}

				return {
					{ "id", "disconnected" },
					{ "str", STRING(DISCONNECTED) },
				};
			}
			case Client::STATE_NORMAL: {
				return {
					{ "id", "connected" },
					{ "str", STRING(CONNECTED) },
				};
			}
		}

		dcassert(0);
		return nullptr;
	}

	void HubInfo::on(ClientListener::Close, const Client*) noexcept {

	}

	void HubInfo::on(ClientListener::Redirected, const string&, const ClientPtr& aNewClient) noexcept {
		client->removeListener(this);
		client = aNewClient;
		chatHandler.setChat(client.get());
		aNewClient->addListener(this);

		onHubUpdated({
			{ "identity", serializeIdentity(client) },
			{ "encryption", Serializer::serializeEncryption(client->getEncryptionInfo(), client->isTrusted()) },
			{ "connect_state", serializeConnectState(client) },
		});
	}

	void HubInfo::on(ClientListener::Disconnected, const string&, const string&) noexcept {
		sendConnectState();

		view.resetItems();
	}

	void HubInfo::on(ClientListener::Redirect, const Client*, const string&) noexcept {
		sendConnectState();
	}

	void HubInfo::on(ConnectStateChanged, const Client*, uint8_t aState) noexcept {
		if (aState == Client::STATE_IDENTIFY || aState == Client::STATE_PROTOCOL) {
			// Use the old "connecting" state still
			return;
		}

		sendConnectState();
	}

	void HubInfo::on(ClientListener::SettingsUpdated, const Client*) noexcept {
		onHubUpdated({
			{ "settings", serializeSettings(client) }
		});
	}

	void HubInfo::on(ClientListener::GetPassword, const Client*) noexcept {
		sendConnectState();
	}

	void HubInfo::on(ClientListener::HubUpdated, const Client*) noexcept {
		onHubUpdated({
			{ "identity", serializeIdentity(client) }
		});
	}

	void HubInfo::sendConnectState() noexcept {
		onHubUpdated({
			{ "encryption", Serializer::serializeEncryption(client->getEncryptionInfo(), client->isTrusted()) },
			{ "connect_state", serializeConnectState(client) },
		});
	}

	void HubInfo::onTimer() noexcept {
		if (!subscriptionActive("hub_counts_updated")) {
			return;
		}

		auto newCounts = serializeCounts(client);
		if (newCounts == previousCounts) {
			return;
		}

		send("hub_counts_updated", newCounts);
		previousCounts.swap(newCounts);
	}

	void HubInfo::onHubUpdated(const json& aData) noexcept {
		if (!subscriptionActive("hub_updated")) {
			return;
		}

		send("hub_updated", aData);
	}

	OnlineUserList HubInfo::getUsers() noexcept {
		OnlineUserList ret;
		client->getUserList(ret, false);
		return ret;
	}

	void HubInfo::on(ClientListener::UserConnected, const Client*, const OnlineUserPtr& aUser) noexcept {
		if (!aUser->isHidden()) {
			view.onItemAdded(aUser);
		}

		maybeSend("hub_user_connected", [&] { return Serializer::serializeItem(aUser, OnlineUserUtils::propertyHandler); });
	}

	void HubInfo::onUserUpdated(const OnlineUserPtr& ou) noexcept {
		// Don't update all properties to avoid unneeded sorting
		onUserUpdated(ou, { 
			OnlineUserUtils::PROP_SHARED, OnlineUserUtils::PROP_DESCRIPTION, 
			OnlineUserUtils::PROP_TAG, OnlineUserUtils::PROP_UPLOAD_SPEED, 
			OnlineUserUtils::PROP_DOWNLOAD_SPEED, OnlineUserUtils::PROP_EMAIL, 
			OnlineUserUtils::PROP_FILES, OnlineUserUtils::PROP_FLAGS, OnlineUserUtils::PROP_SUPPORTS,
			OnlineUserUtils::PROP_UPLOAD_SLOTS
		});
	}

	void HubInfo::onUserUpdated(const OnlineUserPtr& aUser, const PropertyIdSet& aUpdatedProperties) noexcept {
		if (!aUser->isHidden()) {
			view.onItemUpdated(aUser, aUpdatedProperties);
		}

		maybeSend("hub_user_updated", [&] { return Serializer::serializeItem(aUser, OnlineUserUtils::propertyHandler); });
	}

	void HubInfo::on(ClientListener::UserUpdated, const Client*, const OnlineUserPtr& aUser) noexcept {
		onUserUpdated(aUser);
	}

	void HubInfo::on(ClientListener::UsersUpdated, const Client*, const OnlineUserList& aUsers) noexcept {
		for (auto& u : aUsers) {
			onUserUpdated(u);
		}
	}

	void HubInfo::on(ClientListener::UserRemoved, const Client*, const OnlineUserPtr& aUser) noexcept {
		if (!aUser->isHidden()) {
			view.onItemRemoved(aUser);
		}

		maybeSend("hub_user_disconnected", [&] { return Serializer::serializeItem(aUser, OnlineUserUtils::propertyHandler); });
	}
}