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

	HubApi::HubApi(Session* aSession) : ParentApiModule("session", TOKEN_PARAM, Access::HUBS_VIEW, aSession, subscriptionList, HubInfo::subscriptionList, [](const string& aId) { return Util::toUInt32(aId); }) {
		ClientManager::getInstance()->addListener(this);

		METHOD_HANDLER("sessions", Access::HUBS_VIEW, ApiRequest::METHOD_GET, (), false, HubApi::handleGetHubs);

		METHOD_HANDLER("session", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), true, HubApi::handleConnect);
		METHOD_HANDLER("session", Access::HUBS_EDIT, ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, HubApi::handleDisconnect);

		METHOD_HANDLER("search_nicks", Access::ANY, ApiRequest::METHOD_POST, (), true, HubApi::handleSearchNicks);

		METHOD_HANDLER("stats", Access::ANY, ApiRequest::METHOD_GET, (), false, HubApi::handleGetStats);

		METHOD_HANDLER("message", Access::HUBS_SEND, ApiRequest::METHOD_POST, (), true, HubApi::handlePostMessage);
		METHOD_HANDLER("status", Access::HUBS_EDIT, ApiRequest::METHOD_POST, (), true, HubApi::handlePostStatus);

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
		//j["profile_root_count"] = stats.;

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
		json j = {
			{ "identity", HubInfo::serializeIdentity(aClient) },
			{ "connect_state", HubInfo::serializeConnectState(aClient) },
			{ "hub_url", aClient->getHubUrl() },
			{ "id", aClient->getClientId() },
			{ "favorite_hub", aClient->getFavToken() },
			{ "share_profile", Serializer::serializeShareProfileSimple(aClient->get(HubSettings::ShareProfile)) }
		};

		Serializer::serializeCacheInfo(j, aClient->getCache(), Serializer::serializeUnreadChat);
		return j;
	}

	void HubApi::addHub(const ClientPtr& aClient) noexcept {
		addSubModule(aClient->getClientId(), std::make_shared<HubInfo>(this, aClient));
	}

	api_return HubApi::handleGetHubs(ApiRequest& aRequest) {
		auto retJson = json::array();
		forEachSubModule([&](const HubInfo& aInfo) {
			retJson.push_back(serializeClient(aInfo.getClient()));
		});

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
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

			send("hub_removed", {
				{ "id", aClient->getClientId() }
			});
		});
	}

	api_return HubApi::handleConnect(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto address = JsonUtil::getField<string>("hub_url", reqJson, false);

		RecentHubEntryPtr r = new RecentHubEntry(address);
		auto client = ClientManager::getInstance()->createClient(r);
		if (!client) {
			aRequest.setResponseErrorStr("Hub exists");
			return websocketpp::http::status_code::bad_request;
		}

		aRequest.setResponseBody({
			{ "id", client->getClientId() }
		});

		return websocketpp::http::status_code::ok;
	}

	api_return HubApi::handleDisconnect(ApiRequest& aRequest) {
		if (!ClientManager::getInstance()->putClient(aRequest.getTokenParam(0))) {
			return websocketpp::http::status_code::not_found;
		}

		return websocketpp::http::status_code::ok;
	}

	api_return HubApi::handleSearchNicks(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto pattern = JsonUtil::getField<string>("pattern", reqJson);
		auto maxResults = JsonUtil::getField<size_t>("max_results", reqJson);
		auto ignorePrefixes = JsonUtil::getOptionalFieldDefault<bool>("ignore_prefixes", reqJson, true);

		auto users = ClientManager::getInstance()->searchNicks(pattern, maxResults, ignorePrefixes);

		auto retJson = json::array();
		for (const auto& u : users) {
			retJson.push_back(Serializer::serializeOnlineUser(u));
		}

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
	}
}