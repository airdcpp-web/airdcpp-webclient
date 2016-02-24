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
#include <api/OnlineUserUtils.h>

#include <web-server/JsonUtil.h>
#include <airdcpp/MessageManager.h>

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

	const PropertyList HubInfo::properties = {
		{ PROP_NICK, "nick", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
		{ PROP_SHARED, "share_size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_DESCRIPTION, "description", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_TAG, "tag", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_UPLOAD_SPEED, "upload_speed", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_DOWNLOAD_SPEED, "download_speed", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_IP4, "ip4", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_IP6, "ip6", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_EMAIL, "email", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		//{ PROP_ACTIVE4, "active4", TYPE_NUMERIC_OTHER , SERIALIZE_BOOL, SORT_NUMERIC },
		//{ PROP_ACTIVE6, "active6", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
		{ PROP_FILES, "file_count", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_HUB_URL, "hub_url", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_HUB_NAME , "hub_name", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_FLAGS, "flags", TYPE_LIST_TEXT, SERIALIZE_CUSTOM, SORT_NONE },
		{ PROP_CID, "cid", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
	};

	PropertyItemHandler<OnlineUserPtr> HubInfo::onlineUserPropertyHandler = {
		HubInfo::properties,
		OnlineUserUtils::getStringInfo, OnlineUserUtils::getNumericInfo, OnlineUserUtils::compareUsers, OnlineUserUtils::serializeUser
	};

	HubInfo::HubInfo(ParentType* aParentModule, const ClientPtr& aClient) :
		SubApiModule(aParentModule, aClient->getClientId(), subscriptionList), client(aClient),
		chatHandler(this, aClient, "hub"), 
		view("hub_user_view", this, onlineUserPropertyHandler, std::bind(&HubInfo::getUsers, this), 500), 
		timer(getTimer([this] { onTimer(); }, 1000)) {

		METHOD_HANDLER("reconnect", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), false, HubInfo::handleReconnect);
		METHOD_HANDLER("favorite", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), false, HubInfo::handleFavorite);
		METHOD_HANDLER("password", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), true, HubInfo::handlePassword);
		METHOD_HANDLER("redirect", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), false, HubInfo::handleRedirect);

		METHOD_HANDLER("counts", Access::HUBS_VIEW, ApiRequest::METHOD_GET, (), false, HubInfo::handleGetCounts);
	}

	HubInfo::~HubInfo() {
		timer->stop(true);

		MessageManager::getInstance()->removeListener(this);
		client->removeListener(this);
	}

	void HubInfo::init() noexcept {
		MessageManager::getInstance()->addListener(this);
		client->addListener(this);

		timer->start(false);
	}

	api_return HubInfo::handleGetCounts(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeCounts(client));
		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handleReconnect(ApiRequest& aRequest) {
		client->reconnect();
		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handleFavorite(ApiRequest& aRequest) {
		if (!client->saveFavorite()) {
			aRequest.setResponseErrorStr(STRING(FAVORITE_HUB_ALREADY_EXISTS));
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handlePassword(ApiRequest& aRequest) {
		auto password = JsonUtil::getField<string>("password", aRequest.getRequestBody(), false);

		client->password(password);
		return websocketpp::http::status_code::ok;
	}

	api_return HubInfo::handleRedirect(ApiRequest& aRequest) {
		client->doRedirect();
		return websocketpp::http::status_code::ok;
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
			case Client::STATE_DISCONNECTED: id = "disconnected"; break;
		}

		return {
			{ "id", id },
			{ "encryption", Serializer::serializeEncryption(aClient->getEncryptionInfo(), aClient->isTrusted()) },
		};
	}

	void HubInfo::on(ClientListener::Disconnecting, const Client*) noexcept {

	}

	void HubInfo::on(ClientListener::Redirected, const string&, const ClientPtr& aNewClient) noexcept {
		client->removeListener(this);
		client = aNewClient;
		aNewClient->addListener(this);

		sendConnectState();
	}

	void HubInfo::on(Failed, const string&, const string&) noexcept {
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

		maybeSend("hub_user_connected", [&] { return Serializer::serializeItem(aUser, onlineUserPropertyHandler); });
	}

	void HubInfo::onUserUpdated(const OnlineUserPtr& ou) noexcept {
		// Don't update all properties to avoid unneeded sorting
		onUserUpdated(ou, { PROP_SHARED, PROP_DESCRIPTION, PROP_TAG,
			PROP_UPLOAD_SPEED, PROP_DOWNLOAD_SPEED,
			PROP_EMAIL, PROP_FILES, PROP_FLAGS
		});
	}

	void HubInfo::onUserUpdated(const OnlineUserPtr& aUser, const PropertyIdSet& aUpdatedProperties) noexcept {
		if (!aUser->isHidden()) {
			view.onItemUpdated(aUser, aUpdatedProperties);
		}

		maybeSend("hub_user_updated", [&] { return Serializer::serializeItem(aUser, onlineUserPropertyHandler); });
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

		maybeSend("hub_user_disconnected", [&] { return Serializer::serializeItem(aUser, onlineUserPropertyHandler); });
	}

	void HubInfo::onFlagsUpdated(const UserPtr& aUser) noexcept {
		auto ou = ClientManager::getInstance()->findOnlineUser(aUser->getCID(), client->getHubUrl(), false);
		if (ou) {
			onUserUpdated(ou, { PROP_FLAGS });
		}
	}

	void HubInfo::on(MessageManagerListener::IgnoreAdded, const UserPtr& aUser) noexcept {
		onFlagsUpdated(aUser);
	}

	void HubInfo::on(MessageManagerListener::IgnoreRemoved, const UserPtr& aUser) noexcept {
		onFlagsUpdated(aUser);
	}
}