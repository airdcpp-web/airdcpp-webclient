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

#include <api/FavoriteHubApi.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/favorites/FavoriteManager.h>
#include <airdcpp/util/LinkUtil.h>
#include <airdcpp/share/ShareManager.h>


namespace webserver {
	FavoriteHubApi::FavoriteHubApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::FAVORITE_HUBS_VIEW),
		view("favorite_hub_view", this, FavoriteHubUtils::propertyHandler, getEntryList) {

		createSubscriptions({ "favorite_hub_created", "favorite_hub_updated", "favorite_hub_removed" });

		METHOD_HANDLER(Access::FAVORITE_HUBS_VIEW, METHOD_GET,		(RANGE_START_PARAM, RANGE_MAX_PARAM),	FavoriteHubApi::handleGetHubs);
		METHOD_HANDLER(Access::FAVORITE_HUBS_EDIT, METHOD_POST,		(),										FavoriteHubApi::handleAddHub);
		METHOD_HANDLER(Access::FAVORITE_HUBS_EDIT, METHOD_DELETE,	(TOKEN_PARAM),							FavoriteHubApi::handleRemoveHub);
		METHOD_HANDLER(Access::FAVORITE_HUBS_EDIT, METHOD_PATCH,	(TOKEN_PARAM),							FavoriteHubApi::handleUpdateHub);
		METHOD_HANDLER(Access::FAVORITE_HUBS_VIEW, METHOD_GET,		(TOKEN_PARAM),							FavoriteHubApi::handleGetHub);

		FavoriteManager::getInstance()->addListener(this);
	}

	FavoriteHubApi::~FavoriteHubApi() {
		FavoriteManager::getInstance()->removeListener(this);
	}

	FavoriteHubEntryList FavoriteHubApi::getEntryList() noexcept {
		return FavoriteManager::getInstance()->getFavoriteHubs();
	}

	api_return FavoriteHubApi::handleGetHubs(ApiRequest& aRequest) {
		auto j = Serializer::serializeItemList(aRequest.getRangeParam(START_POS), aRequest.getRangeParam(MAX_COUNT), FavoriteHubUtils::propertyHandler, getEntryList());
		aRequest.setResponseBody(j);

		return websocketpp::http::status_code::ok;
	}

	void FavoriteHubApi::updateProperties(FavoriteHubEntryPtr& aEntry, const json& j, bool aNewHub) {
		{
			// Required values
			auto name = JsonUtil::getOptionalField<string>("name", j, aNewHub);

			auto server = JsonUtil::getOptionalField<string>("hub_url", j, aNewHub);
			if (server) {
				if (!FavoriteManager::getInstance()->isUnique(*server, aEntry->getToken())) {
					JsonUtil::throwError("hub_url", JsonException::ERROR_EXISTS, STRING(FAVORITE_HUB_ALREADY_EXISTS));
				}
			}

			// We have valid values
			if (name) {
				aEntry->setName(*name);
			}

			if (server) {
				aEntry->setServer(*server);
			}
		}

		// Optional values
		for (const auto& i : j.items()) {
			const auto& key = i.key();
			if (key == "share_profile") {
				auto shareProfileToken = JsonUtil::getOptionalFieldDefault("share_profile", j, HUB_SETTING_DEFAULT_INT);
				if (shareProfileToken != HUB_SETTING_DEFAULT_INT) {
					if (!LinkUtil::isAdcHub(aEntry->getServer()) && shareProfileToken != SETTING(DEFAULT_SP) && shareProfileToken != SP_HIDDEN) {
						JsonUtil::throwError("share_profile", JsonException::ERROR_INVALID, "Share profiles can't be changed for NMDC hubs");
					}

					auto shareProfilePtr = ShareManager::getInstance()->getShareProfile(shareProfileToken, false);
					if (!shareProfilePtr) {
						JsonUtil::throwError("share_profile", JsonException::ERROR_INVALID, "Invalid share profile");
					}
				}

				aEntry->get(HubSettings::ShareProfile) = shareProfileToken;
			} else if (key == "auto_connect") {
				aEntry->setAutoConnect(JsonUtil::parseValue<bool>("auto_connect", i.value()));
			} else if (key == "hub_description") {
				aEntry->setDescription(JsonUtil::parseValue<string>("hub_description", i.value()));
			} else if (key == "password") {
				aEntry->setPassword(JsonUtil::parseValue<string>("password", i.value()));
			} else if (key == "nick") {
				aEntry->get(HubSettings::Nick) = JsonUtil::parseValue<string>("nick", i.value());
			} else if (key == "user_description") {
				aEntry->get(HubSettings::Description) = JsonUtil::parseValue<string>("user_description", i.value());
			} else if (key == "nmdc_encoding") {
				aEntry->get(HubSettings::NmdcEncoding) = JsonUtil::parseValue<string>("nmdc_encoding", i.value());
			} else if (key == "connection_mode_v4") {
				aEntry->get(HubSettings::Connection) = JsonUtil::parseRangeValueDefault<int>("connection_mode_v4", i.value(), HUB_SETTING_DEFAULT_INT, SettingsManager::INCOMING_DISABLED, SettingsManager::INCOMING_PASSIVE);
			} else if (key == "connection_mode_v6") {
				aEntry->get(HubSettings::Connection6) = JsonUtil::parseRangeValueDefault<int>("connection_mode_v6", i.value(), HUB_SETTING_DEFAULT_INT, SettingsManager::INCOMING_DISABLED, SettingsManager::INCOMING_PASSIVE);
			} else if (key == "connection_ip_v4") {
				aEntry->get(HubSettings::UserIp) = JsonUtil::parseValue<string>("connection_ip_v4", i.value());
			} else if (key == "connection_ip_v6") {
				aEntry->get(HubSettings::UserIp6) = JsonUtil::parseValue<string>("connection_ip_v6", i.value());
			} else if (key == "show_joins") {
				aEntry->get(HubSettings::ShowJoins) = deserializeTribool("show_joins", i.value());
			} else if (key == "fav_show_joins") {
				aEntry->get(HubSettings::FavShowJoins) = deserializeTribool("fav_show_joins", i.value());
			} else if (key == "use_main_chat_notify") {
				aEntry->get(HubSettings::ChatNotify) = deserializeTribool("use_main_chat_notify", i.value());
			} else if (key == "log_main") {
				aEntry->get(HubSettings::LogMainChat) = deserializeTribool("log_main", i.value());
			} else if (key == "away_message") {
				aEntry->get(HubSettings::AwayMsg) = JsonUtil::parseValue<string>("away_message", i.value());
			}
		}
	}

	tribool FavoriteHubApi::deserializeTribool(const string& aFieldName, const json& aJson) {
		auto value = JsonUtil::parseOptionalValue<bool>(aFieldName, aJson);
		if (!value) {
			return tribool(indeterminate);
		}

		return *value;
	}

	api_return FavoriteHubApi::handleAddHub(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto e = std::make_shared<FavoriteHubEntry>();
		updateProperties(e, reqJson, true);

		FavoriteManager::getInstance()->addFavoriteHub(e);

		aRequest.setResponseBody(Serializer::serializeItem(e, FavoriteHubUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	FavoriteHubEntryPtr FavoriteHubApi::parseFavoriteHubParam(ApiRequest& aRequest) {
		auto token = aRequest.getTokenParam();
		auto entry = FavoriteManager::getInstance()->getFavoriteHubEntry(token);
		if (!entry) {
			throw RequestException(websocketpp::http::status_code::not_found, "Favorite hub " + Util::toString(token) + " was not found");
		}

		return entry;
	}

	api_return FavoriteHubApi::handleRemoveHub(ApiRequest& aRequest) {
		auto entry = parseFavoriteHubParam(aRequest);
		FavoriteManager::getInstance()->removeFavoriteHub(entry->getToken());
		return websocketpp::http::status_code::no_content;
	}

	api_return FavoriteHubApi::handleGetHub(ApiRequest& aRequest) {
		auto entry = parseFavoriteHubParam(aRequest);
		aRequest.setResponseBody(Serializer::serializeItem(entry, FavoriteHubUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return FavoriteHubApi::handleUpdateHub(ApiRequest& aRequest) {
		auto entry = parseFavoriteHubParam(aRequest);

		updateProperties(entry, aRequest.getRequestBody(), false);
		FavoriteManager::getInstance()->onFavoriteHubUpdated(entry);

		aRequest.setResponseBody(Serializer::serializeItem(entry, FavoriteHubUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	void FavoriteHubApi::on(FavoriteManagerListener::FavoriteHubAdded, const FavoriteHubEntryPtr& e)  noexcept {
		view.onItemAdded(e);

		maybeSend("favorite_hub_created", [&] {
			return Serializer::serializeItem(e, FavoriteHubUtils::propertyHandler);
		});
	}
	void FavoriteHubApi::on(FavoriteManagerListener::FavoriteHubRemoved, const FavoriteHubEntryPtr& e) noexcept {
		view.onItemRemoved(e);

		maybeSend("favorite_hub_removed", [&] {
			return Serializer::serializeItem(e, FavoriteHubUtils::propertyHandler);
		});
	}
	void FavoriteHubApi::on(FavoriteManagerListener::FavoriteHubUpdated, const FavoriteHubEntryPtr& e) noexcept {
		view.onItemUpdated(e, toPropertyIdSet(FavoriteHubUtils::properties));

		maybeSend("favorite_hub_updated", [&] {
			return Serializer::serializeItem(e, FavoriteHubUtils::propertyHandler);
		});
	}
}