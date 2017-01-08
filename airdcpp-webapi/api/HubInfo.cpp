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

#include <api/HubInfo.h>
#include <api/ApiModule.h>
#include <api/common/Serializer.h>
#include <api/FavoriteHubUtils.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ClientManager.h>


namespace webserver {
	const StringList HubInfo::subscriptionList = {
		"hub_updated",
		"hub_counts_updated",
		"hub_message",
		"hub_status",

		"hub_user_connected",
		"hub_user_updated",
		"hub_user_disconnected",
	};

	HubInfo::HubInfo(ParentType* aParentModule, const ClientPtr& aClient) :
		SubApiModule(aParentModule, aClient->getClientId(), subscriptionList), client(aClient),
		chatHandler(this, aClient, "hub", Access::HUBS_VIEW, Access::HUBS_EDIT, Access::HUBS_SEND), 
		view("hub_user_view", this, OnlineUserUtils::propertyHandler, std::bind(&HubInfo::getUsers, this), 500), 
		timer(getTimer([this] { onTimer(); }, 1000)) {

		METHOD_HANDLER("reconnect", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), false, HubInfo::handleReconnect);
		METHOD_HANDLER("favorite", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), false, HubInfo::handleFavorite);
		METHOD_HANDLER("password", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), true, HubInfo::handlePassword);
		METHOD_HANDLER("redirect", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), false, HubInfo::handleRedirect);

		METHOD_HANDLER("counts", Access::HUBS_VIEW, ApiRequest::METHOD_GET, (), false, HubInfo::handleGetCounts);
	}

	HubInfo::~HubInfo() {
		timer->stop(true);

		client->removeListener(this);
	}

	void HubInfo::init() noexcept {
		client->addListener(this);

		timer->start(false);
	}

	api_return HubInfo::handleGetCounts(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeCounts(client));
		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handleReconnect(ApiRequest& aRequest) {
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

	api_return HubInfo::handleRedirect(ApiRequest& aRequest) {
		client->doRedirect();
		return websocketpp::http::status_code::no_content;
	}

	json HubInfo::serializeIdentity(const ClientPtr& aClient) noexcept {
		return {
			{ "name", aClient->getHubName() },
			{ "description", aClient->getHubDescription() },
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
				{ "hub_url", aClient->getRedirectUrl() }
			};
		}

		string id;
		switch (aClient->getConnectState()) {
			case Client::STATE_CONNECTING:
			case Client::STATE_PROTOCOL:
			case Client::STATE_IDENTIFY: id = "connecting"; break;
			case Client::STATE_VERIFY: {
				return {
					{ "id", "password" },
					{ "has_password", !aClient->getPassword().empty() }
				};
				break;
			}
			case Client::STATE_NORMAL: id = "connected"; break;
			case Client::STATE_DISCONNECTED: 
			{
				id = aClient->isKeyprintMismatch() ? "keyprint_mismatch" : "disconnected";
				break;
			}
		}

		return {
			{ "id", id },
			{ "encryption", Serializer::serializeEncryption(aClient->getEncryptionInfo(), aClient->isTrusted()) },
		};
	}

	void HubInfo::on(ClientListener::Close, const Client*) noexcept {

	}

	void HubInfo::on(ClientListener::Redirected, const string&, const ClientPtr& aNewClient) noexcept {
		client->removeListener(this);
		client = aNewClient;
		aNewClient->addListener(this);

		sendConnectState();
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

	void HubInfo::on(ClientListener::GetPassword, const Client*) noexcept {
		sendConnectState();
	}

	void HubInfo::on(ClientListener::HubUpdated, const Client*) noexcept {
		onHubUpdated({
			{ "identity", serializeIdentity(client) }
		});
	}

	void HubInfo::on(ClientListener::HubTopic, const Client*, const string&) noexcept {

	}

	void HubInfo::sendConnectState() noexcept {
		onHubUpdated({
			{ "connect_state", serializeConnectState(client) }
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
			OnlineUserUtils::PROP_FILES, OnlineUserUtils::PROP_FLAGS,
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

	void HubInfo::on(ClientListener::UsersUpdated, const Client* c, const OnlineUserList& aUsers) noexcept {
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