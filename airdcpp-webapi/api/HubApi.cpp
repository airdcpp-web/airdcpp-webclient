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

#include <api/HubApi.h>

#include <api/common/MessageUtils.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>
#include <web-server/WebServerSettings.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/favorites/HubEntry.h>

namespace webserver {

#define HOOK_INCOMING_MESSAGE "hub_incoming_message_hook"
#define HOOK_OUTGOING_MESSAGE "hub_outgoing_message_hook"

	StringList HubApi::subscriptionList = {
		"hub_created",
		"hub_removed"
	};

	ActionHookResult<MessageHighlightList> HubApi::incomingMessageHook(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) {
		return HookCompletionData::toResult<MessageHighlightList>(
			maybeFireHook(HOOK_INCOMING_MESSAGE, WEBCFG(INCOMING_CHAT_MESSAGE_HOOK_TIMEOUT).num(), [&]() {
				return MessageUtils::serializeChatMessage(aMessage);
			}),
			aResultGetter,
			this,
			MessageUtils::getMessageHookHighlightDeserializer(aMessage->getText())
		);
	};

	ActionHookResult<> HubApi::outgoingMessageHook(const OutgoingChatMessage& aMessage, const Client& aClient, const ActionHookResultGetter<>& aResultGetter) {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_OUTGOING_MESSAGE, WEBCFG(OUTGOING_CHAT_MESSAGE_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "text", aMessage.text },
					{ "third_person", aMessage.thirdPerson },
					{ "hub_url", aClient.getHubUrl() },
					{ "session_id", aClient.getToken() },
				});
			}),
			aResultGetter,
			this
		);
	}

	HubApi::HubApi(Session* aSession) : 
		ParentApiModule(TOKEN_PARAM, Access::HUBS_VIEW, aSession,
			[](const string& aId) { return Util::toUInt32(aId); },
			[](const HubInfo& aInfo) { return serializeClient(aInfo.getClient()); },
			Access::HUBS_EDIT
		) 
	{
		createSubscriptions(subscriptionList, HubInfo::subscriptionList);

		// Hooks
		HOOK_HANDLER(HOOK_INCOMING_MESSAGE, ClientManager::getInstance()->incomingHubMessageHook, HubApi::incomingMessageHook);
		HOOK_HANDLER(HOOK_OUTGOING_MESSAGE, ClientManager::getInstance()->outgoingHubMessageHook, HubApi::outgoingMessageHook);

		// Methods
		METHOD_HANDLER(Access::HUBS_EDIT,	METHOD_POST,	(),										HubApi::handleConnect);

		METHOD_HANDLER(Access::HUBS_VIEW,	METHOD_GET,		(EXACT_PARAM("stats")),					HubApi::handleGetStats);
		METHOD_HANDLER(Access::HUBS_VIEW,	METHOD_POST,	(EXACT_PARAM("find_by_url")),			HubApi::handleFindByUrl);

		METHOD_HANDLER(Access::HUBS_SEND,	METHOD_POST,	(EXACT_PARAM("chat_message")),			HubApi::handlePostMessage);
		METHOD_HANDLER(Access::HUBS_EDIT,	METHOD_POST,	(EXACT_PARAM("status_message")),		HubApi::handlePostStatus);

		// Listeners
		ClientManager::getInstance()->addListener(this);

		// Init
		{
			auto cm = ClientManager::getInstance();

			RLock l(cm->getCS());
			auto rawHubs = cm->getClientsUnsafe();
			for (const auto& c : rawHubs | views::values) {
				addHub(c);
			}
		}
	}

	HubApi::~HubApi() {
		ClientManager::getInstance()->removeListener(this);
	}

	api_return HubApi::handlePostMessage(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		addAsyncTask([
			message = Deserializer::deserializeChatMessage(reqJson),
			hubs = Deserializer::deserializeHubUrls(reqJson),
			complete = aRequest.defer(),
			callerPtr = aRequest.getOwnerPtr()
		] {
			int succeed = 0;
			string lastError;
			for (const auto& url: hubs) {
				auto c = ClientManager::getInstance()->findClient(url);
				if (c && c->isConnected() && c->sendMessageHooked(OutgoingChatMessage(message.message, callerPtr, Util::emptyString, message.thirdPerson), lastError)) {
					succeed++;
				}
			}

			complete(
				websocketpp::http::status_code::ok,
				{
					{ "sent", succeed },
				},
				nullptr
			);
		});

		return CODE_DEFERRED;
	}

	api_return HubApi::handlePostStatus(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto message = Deserializer::deserializeChatStatusMessage(reqJson);
		auto label = MessageUtils::parseStatusMessageLabel(aRequest.getSession());
		auto hubs = Deserializer::deserializeHubUrls(reqJson);

		int succeed = 0;
		for (const auto& url : hubs) {
			auto c = ClientManager::getInstance()->findClient(url);
			if (c) {
				c->statusMessage(
					message.message, 
					message.severity, 
					message.type,
					label,
					message.ownerId
				);
				succeed++;
			}
		}

		aRequest.setResponseBody({
			{ "sent", succeed },
		});

		return websocketpp::http::status_code::ok;
	}

	api_return HubApi::handleGetStats(ApiRequest& aRequest) {
		auto optionalStats = ClientManager::getInstance()->getClientStats();
		if (!optionalStats) {
			return websocketpp::http::status_code::no_content;
		}

		auto stats = *optionalStats;

		json j = {
			{ "total_users", stats.totalUsers },
			{ "total_share", stats.totalShare },

			{ "unique_users", stats.uniqueUsers },
			{ "adc_users", stats.adcUsers },
			{ "nmdc_users", stats.nmdcUsers },
			{ "active_users", stats.activeUsers },

			{ "adc_down_per_user", stats.downPerAdcUser },
			{ "adc_up_per_user", stats.upPerAdcUser },
			{ "nmdc_speed_per_user", stats.nmdcSpeedPerUser },
		};

		for (const auto& c: stats.clients) {
			j["clients"].push_back({
				{ "name", c.first },
				{ "count", c.second },
			});
		}

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	json HubApi::serializeClient(const ClientPtr& aClient) noexcept {
		return {
			{ "identity", HubInfo::serializeIdentity(aClient) },
			{ "connect_state", HubInfo::serializeConnectState(aClient) },
			{ "hub_url", aClient->getHubUrl() },
			{ "id", aClient->getToken() },
			{ "favorite_hub", aClient->getFavToken() },
			{ "share_profile", Serializer::serializeShareProfileSimple(aClient->get(HubSettings::ShareProfile)) },
			{ "message_counts", MessageUtils::serializeCacheInfo(aClient->getCache(), MessageUtils::serializeUnreadChat) },
			{ "encryption", Serializer::serializeEncryption(aClient->getEncryptionInfo(), aClient->isTrusted()) },
			{ "settings", HubInfo::serializeSettings(aClient) },
		};
	}

	void HubApi::addHub(const ClientPtr& aClient) noexcept {
		addSubModule(aClient->getToken(), std::make_shared<HubInfo>(this, aClient));
	}

	// Use async tasks because adding/removing HubInfos require calls to ClientListener (which is likely 
	// to cause deadlocks if done inside ClientManagerListener)
	void HubApi::on(ClientManagerListener::ClientCreated, const ClientPtr& aClient) noexcept {
		addAsyncTask([this, aClient] {
			addHub(aClient);
			if (!subscriptionActive("hub_created")) {
				return;
			}

			send("hub_created", serializeClient(aClient));
		});
	}

	void HubApi::on(ClientManagerListener::ClientRemoved, const ClientPtr& aClient) noexcept {
		addAsyncTask([this, aClient] {
			removeSubModule(aClient->getToken());

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

	api_return HubApi::handleDeleteSubmodule(ApiRequest& aRequest) {
		auto hub = getSubModule(aRequest);
		ClientManager::getInstance()->putClient(hub->getClient());
		return websocketpp::http::status_code::no_content;
	}

	api_return HubApi::handleFindByUrl(ApiRequest& aRequest) {
		auto client = Deserializer::deserializeClient(aRequest.getRequestBody());
		aRequest.setResponseBody(serializeClient(client));
		return websocketpp::http::status_code::ok;
	}
}