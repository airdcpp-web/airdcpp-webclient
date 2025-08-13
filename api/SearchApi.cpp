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

#include <api/SearchApi.h>

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>

#include <web-server/WebServerSettings.h>
#include <web-server/WebUser.h>

#include <airdcpp/search/SearchInstance.h>
#include <airdcpp/search/SearchManager.h>
#include <airdcpp/search/SearchQuery.h>
#include <airdcpp/search/SearchTypes.h>


#define DEFAULT_INSTANCE_EXPIRATION_MINUTES 30
#define SEARCH_TYPE_ID "search_type"


#define HOOK_INCOMING_USER_RESULT "search_incoming_user_result_hook"

namespace webserver {
	StringList SearchApi::subscriptionList = {
		"search_instance_created",
		"search_instance_removed",
		"search_types_updated",

		"search_incoming_search",
	};

	SearchApi::SearchApi(Session* aSession) : 
		ParentApiModule(TOKEN_PARAM, Access::SEARCH, aSession,
			[](const string& aId) { return Util::toUInt32(aId); },
			[](const SearchEntity& aInfo) { return serializeSearchInstance(aInfo.getSearch()); },
			Access::SEARCH
		)
	{
		createSubscriptions(subscriptionList, SearchEntity::subscriptionList);

		// Hooks
		HOOK_HANDLER(HOOK_INCOMING_USER_RESULT, SearchManager::getInstance()->incomingSearchResultHook, SearchApi::incomingUserResultHook);

		// Methods
		METHOD_HANDLER(Access::SEARCH,			METHOD_POST,	(),				SearchApi::handleCreateInstance);

		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("types")),								SearchApi::handleGetTypes);
		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("types"), STR_PARAM(SEARCH_TYPE_ID)),	SearchApi::handleGetType);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("types")),								SearchApi::handlePostType);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_PATCH,	(EXACT_PARAM("types"), STR_PARAM(SEARCH_TYPE_ID)),	SearchApi::handleUpdateType);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(EXACT_PARAM("types"), STR_PARAM(SEARCH_TYPE_ID)),	SearchApi::handleRemoveType);

		// Listeners
		SearchManager::getInstance()->addListener(this);

		// Init
		for (const auto instance: SearchManager::getInstance()->getSearchInstances()) {
			auto module = std::make_shared<SearchEntity>(this, instance);
			addSubModule(instance->getToken(), module);
		}
	}

	SearchApi::~SearchApi() {
		SearchManager::getInstance()->removeListener(this);

		if (session->getSessionType() != Session::TYPE_BASIC_AUTH) {
			// Remove instances created by this session
			auto ownerId = createCurrentSessionOwnerId("");
			for (const auto& i : SearchManager::getInstance()->getSearchInstances()) {
				if (i->getOwnerId().length() >= ownerId.length() && 
					i->getOwnerId().substr(0, ownerId.length()) == ownerId) 
				{
					SearchManager::getInstance()->removeSearchInstance(i->getToken());
				}
			}
		}
	}

	ActionHookResult<> SearchApi::incomingUserResultHook(const SearchResultPtr& aResult, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_INCOMING_USER_RESULT, WEBCFG(SEARCH_INCOMING_USER_RESULT_HOOK_TIMEOUT).num(), [&]() {
				return SearchEntity::serializeSearchResult(aResult);
			}),
			aResultGetter,
			this
		);
	}

	void SearchApi::on(SearchManagerListener::SearchInstanceCreated, const SearchInstancePtr& aInstance) noexcept {
		auto module = std::make_shared<SearchEntity>(this, aInstance);
		addSubModule(aInstance->getToken(), module);
		maybeSend("search_instance_created", [=] { return serializeSearchInstance(aInstance); });
	}

	void SearchApi::on(SearchManagerListener::SearchInstanceRemoved, const SearchInstancePtr& aInstance) noexcept {
		removeSubModule(aInstance->getToken());
		maybeSend("search_instance_removed", [=] { return serializeSearchInstance(aInstance); });
	}

	string SearchApi::serializeSearchQueryItemType(const SearchQuery& aQuery) noexcept {
		if (aQuery.root) {
			return "tth";
		}

		switch (aQuery.itemType) {
			case SearchQuery::ItemType::DIRECTORY: return "directory";
			case SearchQuery::ItemType::FILE: return "file";
			case SearchQuery::ItemType::ANY: 
			default: return "any";
		};
	}

	json SearchApi::serializeSearchQuery(const SearchQuery& aQuery) noexcept {
		return {
			{ "pattern", aQuery.root ? (*aQuery.root).toBase32() : aQuery.include.toString() },
			{ "min_size", aQuery.gt },
			{ "max_size", aQuery.lt },
			{ "file_type", serializeSearchQueryItemType(aQuery) },
			{ "extensions", aQuery.ext },
			{ "excluded", aQuery.exclude.toStringList() },
		};
	}

	void SearchApi::on(SearchManagerListener::IncomingSearch, Client* aClient, const OnlineUserPtr& aAdcUser, const SearchQuery& aQuery, const SearchResultList& aResults, bool) noexcept {
		maybeSend("search_incoming_search", [&] {
			return json({
				{ "hub", Serializer::serializeClient(aClient) },
				{ "user", aAdcUser ? Serializer::serializeOnlineUser(aAdcUser) : json() },
				{ "results", Serializer::serializeList(aResults, SearchEntity::serializeSearchResult) },
				{ "query", serializeSearchQuery(aQuery) },
			});
		});
	}

	json SearchApi::serializeSearchInstance(const SearchInstancePtr& aSearch) noexcept {
		auto expiration = aSearch->getTimeToExpiration();
		return {
			{ "id", aSearch->getToken() },
			{ "owner", aSearch->getOwnerId() },
			{ "expires_in", expiration ? json(*expiration) : json() },
			{ "current_search_id", aSearch->getCurrentSearchToken() },
			{ "searches_sent_ago", aSearch->getTimeFromLastSearch() },
			{ "queue_time", aSearch->getQueueTime() },
			{ "queued_count", aSearch->getQueueCount() },
			{ "result_count", aSearch->getResultCount() },
			{ "query", SearchEntity::serializeSearchQuery(aSearch->getCurrentParams()) },
		};
	}


	string SearchApi::createCurrentSessionOwnerId(const string& aSuffix) const noexcept {
		string ret;

		switch (session->getSessionType()) {
		case Session::TYPE_EXTENSION:
			ret = "extension:" + session->getUser()->getUserName();
			break;
		case Session::TYPE_BASIC_AUTH:
			ret = "basic_auth";
			break;
		default:
			ret = "session:" + Util::toString(session->getId());
			break;
		}

		if (!aSuffix.empty()) {
			ret += ":" + aSuffix;
		}

		return ret;
	}

	api_return SearchApi::handleCreateInstance(ApiRequest& aRequest) const {
		auto expirationMinutes = JsonUtil::getRangeFieldDefault<int>("expiration", aRequest.getRequestBody(), DEFAULT_INSTANCE_EXPIRATION_MINUTES, 0);
		auto ownerIdSuffix = JsonUtil::getOptionalFieldDefault<string>(
			"owner_suffix", aRequest.getRequestBody(), 
			""
		);

		auto instance = SearchManager::getInstance()->createSearchInstance(
			createCurrentSessionOwnerId(ownerIdSuffix),
			expirationMinutes > 0 ? GET_TICK() + (expirationMinutes * 60 * 1000) : 0
		);

		aRequest.setResponseBody(serializeSearchInstance(instance));
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handleDeleteSubmodule(ApiRequest& aRequest) {
		auto instance = getSubModule(aRequest);
		SearchManager::getInstance()->removeSearchInstance(instance->getSearch()->getToken());
		return websocketpp::http::status_code::no_content;
	}

	api_return SearchApi::handleGetTypes(ApiRequest& aRequest) const {
		const auto& typeManager = SearchManager::getInstance()->getSearchTypes();
		auto types = typeManager.getSearchTypes();
		aRequest.setResponseBody(Serializer::serializeList(types, serializeSearchType));
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handleGetType(ApiRequest& aRequest) const {
		auto id = parseSearchTypeId(aRequest);

		const auto& typeManager = SearchManager::getInstance()->getSearchTypes();
		auto type = typeManager.getSearchType(id);
		aRequest.setResponseBody(serializeSearchType(type));
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handlePostType(ApiRequest& aRequest) const {
		const auto& reqJson = aRequest.getRequestBody();

		auto name = JsonUtil::getField<string>("name", reqJson, false);
		auto extensions = JsonUtil::getField<StringList>("extensions", reqJson, false);

		auto& typeManager = SearchManager::getInstance()->getSearchTypes();
		auto type = typeManager.addSearchType(name, extensions);
		aRequest.setResponseBody(serializeSearchType(type));

		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handleUpdateType(ApiRequest& aRequest) const {
		auto id = parseSearchTypeId(aRequest);

		const auto& reqJson = aRequest.getRequestBody();

		auto name = JsonUtil::getOptionalField<string>("name", reqJson);
		auto extensions = JsonUtil::getOptionalField<StringList>("extensions", reqJson);

		auto& typeManager = SearchManager::getInstance()->getSearchTypes();
		auto type = typeManager.modSearchType(id, name, extensions);
		aRequest.setResponseBody(serializeSearchType(type));
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handleRemoveType(ApiRequest& aRequest) const {
		auto id = parseSearchTypeId(aRequest);
		auto& typeManager = SearchManager::getInstance()->getSearchTypes();
		typeManager.delSearchType(id);
		return websocketpp::http::status_code::no_content;
	}

	void SearchApi::on(SearchManagerListener::SearchTypesChanged) noexcept {
		if (!subscriptionActive("search_types_updated"))
			return;

		send("search_types_updated", json());
	}


	json SearchApi::serializeSearchType(const SearchTypePtr& aType) noexcept {
		auto name = aType->getDisplayName();
		return {
			{ "id", Serializer::getFileTypeId(aType->getId()) },
			{ "str", name },
			{ "name", name },
			{ "extensions", aType->getExtensions() },
			{ "default_type", aType->isDefault() }
		};
	}


	string SearchApi::parseSearchTypeId(ApiRequest& aRequest) noexcept {
		return FileSearchParser::parseSearchType(aRequest.getStringParam(SEARCH_TYPE_ID));
	}
}