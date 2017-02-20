/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#include <api/HubApi.h>

#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ClientManager.h>
#include <airdcpp/HubEntry.h>

namespace webserver {
	StringList HubApi::subscriptionList = {
		"hub_created",
		"hub_removed"
	};

	ActionHookRejectionPtr HubApi::incomingMessageHook(const ChatMessagePtr& aMessage, const HookRejectionGetter& aRejectionGetter) {
		return HookCompletionData::toResult(
			fireHook("hub_incoming_message_hook", 2, [&]() {
				return Serializer::serializeChatMessage(aMessage);
			}),
			aRejectionGetter
		);
	};

	ActionHookRejectionPtr HubApi::outgoingMessageHook(const string& aMessage, bool aThirdPerson, const Client& aClient, const HookRejectionGetter& aRejectionGetter) {
		return HookCompletionData::toResult(
			fireHook("hub_outgoing_message_hook", 2, [&]() {
				return json({
					{ "text", aMessage },
					{ "third_person", aThirdPerson },
					{ "hub_url", aClient.getHubUrl() },
					{ "session_id", aClient.getClientId() },
				});
			}),
			aRejectionGetter
		);
	}

	HubApi::HubApi(Session* aSession) : 
		ParentApiModule("sessions", TOKEN_PARAM, Access::HUBS_VIEW, aSession, subscriptionList, HubInfo::subscriptionList,
			[](const string& aId) { return Util::toUInt32(aId); },
			[](const HubInfo& aInfo) { return serializeClient(aInfo.getClient()); },
			Access::HUBS_EDIT
		) 
	{

		ClientManager::getInstance()->addListener(this);

		createHook("hub_incoming_message_hook", [this](const string& aId, const string& aName) {
			return ClientManager::getInstance()->incomingHubMessageHook.addSubscriber(aId, aName, HOOK_HANDLER(HubApi::incomingMessageHook));
		}, [this](const string& aId) {
			ClientManager::getInstance()->incomingHubMessageHook.removeSubscriber(aId);
		});

		createHook("hub_outgoing_message_hook", [this](const string& aId, const string& aName) {
			return ClientManager::getInstance()->outgoingHubMessageHook.addSubscriber(aId, aName, HOOK_HANDLER(HubApi::outgoingMessageHook));
		}, [this](const string& aId) {
			ClientManager::getInstance()->outgoingHubMessageHook.removeSubscriber(aId);
		});

		METHOD_HANDLER(Access::HUBS_EDIT,	METHOD_POST,	(EXACT_PARAM("sessions")),				HubApi::handleConnect);
		METHOD_HANDLER(Access::HUBS_EDIT,	METHOD_DELETE,	(EXACT_PARAM("sessions"), TOKEN_PARAM),	HubApi::handleDisconnect);

		METHOD_HANDLER(Access::HUBS_VIEW,	METHOD_GET,		(EXACT_PARAM("stats")),					HubApi::handleGetStats);
		METHOD_HANDLER(Access::HUBS_VIEW,	METHOD_POST,	(EXACT_PARAM("find_by_url")),			HubApi::handleFindByUrl);

		METHOD_HANDLER(Access::HUBS_SEND,	METHOD_POST,	(EXACT_PARAM("chat_message")),			HubApi::handlePostMessage);
		METHOD_HANDLER(Access::HUBS_EDIT,	METHOD_POST,	(EXACT_PARAM("status_message")),		HubApi::handlePostStatus);

		auto rawHubs = ClientManager::getInstance()->getClients();
		for (const auto& c : rawHubs | map_values) {
			addHub(c);
		}
	}

	HubApi::~HubApi() {
		ClientManager::getInstance()->removeListener(this);
	}

	api_return HubApi::handlePostMessage(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto message = Deserializer::deserializeChatMessage(reqJson);
		auto hubs = Deserializer::deserializeHubUrls(reqJson);
		int succeed = 0;

		string lastError;
		for (const auto& url : hubs) {
			auto c = ClientManager::getInstance()->getClient(url);
			if (c && c->isConnected() && c->sendMessage(message.first, lastError, message.second)) {
				succeed++;
			}
		}

		aRequest.setResponseBody({
			{ "sent", succeed },
		});

		return websocketpp::http::status_code::ok;
	}

	api_return HubApi::handlePostStatus(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto message = Deserializer::deserializeStatusMessage(reqJson);
		auto hubs = Deserializer::deserializeHubUrls(reqJson);

		int succeed = 0;
		for (const auto& url : hubs) {
			auto c = ClientManager::getInstance()->getClient(url);
			if (c) {
				c->statusMessage(message.first, message.second);
				succeed++;
			}
		}

		aRequest.setResponseBody({
			{ "sent", succeed },
		});

		return websocketpp::http::status_code::ok;
	}

	api_return HubApi::handleGetStats(ApiRequest& aRequest) {
		json j;

		auto optionalStats = ClientManager::getInstance()->getClientStats();
		if (!optionalStats) {
			return websocketpp::http::status_code::no_content;
		}

		auto stats = *optionalStats;

		j["total_users"] = stats.totalUsers;
		j["unique_users"] = stats.uniqueUsers;
		j["unique_user_percentage"] = stats.uniqueUsersPercentage;
		j["adc_users"] = stats.adcUsers;
		j["nmdc_users"] = stats.nmdcUsers;
		j["total_share"] = stats.totalShare;
		j["share_per_user"] = stats.sharePerUser;
		j["adc_down_per_user"] = stats.downPerAdcUser;
		j["adc_up_per_user"] = stats.upPerAdcUser;
		j["nmdc_speed_user"] = stats.nmdcSpeedPerUser;

		stats.forEachClient([&](const string& aName, int aCount, double aPercentage) {
			j["clients"].push_back({
				{ "name", aName },
				{ "count", aCount },
				{ "percentage", aPercentage },
			});
		});

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	json HubApi::serializeClient(const ClientPtr& aClient) noexcept {
		return {
			{ "identity", HubInfo::serializeIdentity(aClient) },
			{ "connect_state", HubInfo::serializeConnectState(aClient) },
			{ "hub_url", aClient->getHubUrl() },
			{ "id", aClient->getClientId() },
			{ "favorite_hub", aClient->getFavToken() },
			{ "share_profile", Serializer::serializeShareProfileSimple(aClient->get(HubSettings::ShareProfile)) },
			{ "message_counts", Serializer::serializeCacheInfo(aClient->getCache(), Serializer::serializeUnreadChat) },
			{ "encryption", Serializer::serializeEncryption(aClient->getEncryptionInfo(), aClient->isTrusted()) },
		};
	}

	void HubApi::addHub(const ClientPtr& aClient) noexcept {
		addSubModule(aClient->getClientId(), std::make_shared<HubInfo>(this, aClient));
	}

	// Use async tasks because adding/removing HubInfos require calls to ClientListener (which is likely 
	// to cause deadlocks if done inside ClientManagerListener)
	void HubApi::on(ClientManagerListener::ClientCreated, const ClientPtr& aClient) noexcept {
		addAsyncTask([=] {
			addHub(aClient);
			if (!subscriptionActive("hub_created")) {
				return;
			}

			send("hub_created", serializeClient(aClient));
		});
	}

	void HubApi::on(ClientManagerListener::ClientRemoved, const ClientPtr& aClient) noexcept {
		addAsyncTask([=] {
			removeSubModule(aClient->getClientId());

			if (!subscriptionActive("hub_removed")) {
				return;
			}

			send("hub_removed", serializeClient(aClient));
		});
	}

	api_return HubApi::handleConnect(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto address = JsonUtil::getField<string>("hub_url", reqJson, false);

		auto client = ClientManager::getInstance()->createClient(address);
		if (!client) {
			aRequest.setResponseErrorStr("Hub with the same URL exists already");
			return websocketpp::http::status_code::conflict;
		}

		aRequest.setResponseBody(serializeClient(client));
		return websocketpp::http::status_code::ok;
	}

	api_return HubApi::handleDisconnect(ApiRequest& aRequest) {
		if (!ClientManager::getInstance()->putClient(aRequest.getTokenParam())) {
			return websocketpp::http::status_code::not_found;
		}

		return websocketpp::http::status_code::no_content;
	}

	api_return HubApi::handleFindByUrl(ApiRequest& aRequest) {
		auto address = JsonUtil::getField<string>("hub_url", aRequest.getRequestBody(), false);

		auto client = ClientManager::getInstance()->getClient(address);
		if (!client) {
			aRequest.setResponseErrorStr("Hub was not found");
			return websocketpp::http::status_code::not_found;
		}

		aRequest.setResponseBody(serializeClient(client));
		return websocketpp::http::status_code::ok;
	}
}