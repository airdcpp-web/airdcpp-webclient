/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#include "stdinc.h"

#include <api/SearchApi.h>

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>

#include <airdcpp/BundleInfo.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/DirectSearch.h>
#include <airdcpp/SearchInstance.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/ShareManager.h>


#define DEFAULT_INSTANCE_EXPIRATION_MINUTES 30
#define SEARCH_TYPE_ID "search_type"

namespace webserver {
	StringList SearchApi::subscriptionList = {
		"search_types_updated"
	};

	SearchApi::SearchApi(Session* aSession) : 
		ParentApiModule(TOKEN_PARAM, Access::SEARCH, aSession, subscriptionList, SearchEntity::subscriptionList,
			[](const string& aId) { return Util::toUInt32(aId); },
			[](const SearchEntity& aInfo) { return serializeSearchInstance(aInfo); }
		),
		timer(getTimer([this] { onTimer(); }, 30 * 1000)) 
	{

		METHOD_HANDLER(Access::SEARCH,	METHOD_POST,	(),						SearchApi::handleCreateInstance);

		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("types")),								SearchApi::handleGetTypes);
		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("types"), STR_PARAM(SEARCH_TYPE_ID)),	SearchApi::handleGetType);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(EXACT_PARAM("types")),								SearchApi::handlePostType);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_PATCH,	(EXACT_PARAM("types"), STR_PARAM(SEARCH_TYPE_ID)),	SearchApi::handleUpdateType);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(EXACT_PARAM("types"), STR_PARAM(SEARCH_TYPE_ID)),	SearchApi::handleRemoveType);

		// Create an initial search instance
		if (aSession->getSessionType() != Session::TYPE_BASIC_AUTH) {
			createInstance(0);
		}

		timer->start(false);
		SearchManager::getInstance()->addListener(this);
	}

	SearchApi::~SearchApi() {
		timer->stop(true);
		SearchManager::getInstance()->removeListener(this);
	}

	void SearchApi::onTimer() noexcept {
		vector<SearchInstanceToken> expiredIds;
		forEachSubModule([&](const SearchEntity& aInstance) {
			auto expiration = aInstance.getTimeToExpiration();
			if (expiration && *expiration <= 0) {
				expiredIds.push_back(aInstance.getId());
				dcdebug("Removing an expired search instance (expiration: " U64_FMT ", now: " U64_FMT ")\n", *expiration, GET_TICK());
			}
		});

		for (const auto& id : expiredIds) {
			removeSubModule(id);
		}
	}

	json SearchApi::serializeSearchInstance(const SearchEntity& aSearch) noexcept {
		auto expiration = aSearch.getTimeToExpiration();
		return {
			{ "id", aSearch.getId() },
			{ "expires_in", expiration ? json(*expiration) : json() },
			{ "current_search_id", aSearch.getSearch()->getCurrentSearchToken() },
			{ "searches_sent_ago", aSearch.getSearch()->getTimeFromLastSearch() },
			{ "queue_time", aSearch.getSearch()->getQueueTime() },
			{ "queued_count", aSearch.getSearch()->getQueueCount() },
			{ "result_count", aSearch.getSearch()->getResultCount() },
		};
	}

	SearchEntity::Ptr SearchApi::createInstance(uint64_t aExpirationTick) {
		auto id = instanceIdCounter++;
		auto module = std::make_shared<SearchEntity>(this, make_shared<SearchInstance>(), id, aExpirationTick);

		addSubModule(id, module);
		return module;
	}

	api_return SearchApi::handleCreateInstance(ApiRequest& aRequest) {
		auto expirationMinutes = JsonUtil::getOptionalFieldDefault<int>("expiration", aRequest.getRequestBody(), DEFAULT_INSTANCE_EXPIRATION_MINUTES);

		auto instance = createInstance(GET_TICK() + expirationMinutes * 60 * 1000);

		aRequest.setResponseBody(serializeSearchInstance(*instance.get()));
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handleDeleteSubmodule(ApiRequest& aRequest) {
		auto instance = getSubModule(aRequest);
		removeSubModule(instance->getId());
		return websocketpp::http::status_code::no_content;
	}

	api_return SearchApi::handleGetTypes(ApiRequest& aRequest) {
		auto types = SearchManager::getInstance()->getSearchTypes();

		json retJ;
		for (const auto& s : types) {
			retJ.push_back(serializeSearchType(s.first, s.second));
		}

		aRequest.setResponseBody(retJ);
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handleGetType(ApiRequest& aRequest) {
		auto id = parseSearchTypeId(aRequest);

		StringList extList;
		auto ftype = Search::TYPE_ANY;
		SearchManager::getInstance()->getSearchType(id, ftype, extList);

		aRequest.setResponseBody(serializeSearchType(id, extList));
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handlePostType(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto name = JsonUtil::getField<string>("name", reqJson, false);
		auto extensions = JsonUtil::getField<StringList>("extensions", reqJson, false);

		SearchManager::getInstance()->addSearchType(name, extensions);
		return websocketpp::http::status_code::no_content;
	}

	api_return SearchApi::handleUpdateType(ApiRequest& aRequest) {
		auto id = parseSearchTypeId(aRequest);

		const auto& reqJson = aRequest.getRequestBody();

		auto name = JsonUtil::getOptionalField<string>("name", reqJson);
		auto extensions = JsonUtil::getOptionalField<StringList>("extensions", reqJson);

		if (name) {
			SearchManager::getInstance()->renameSearchType(id, *name);
		}

		if (extensions) {
			SearchManager::getInstance()->modSearchType(id, *extensions);
		}

		return websocketpp::http::status_code::no_content;
	}

	api_return SearchApi::handleRemoveType(ApiRequest& aRequest) {
		auto id = parseSearchTypeId(aRequest);
		SearchManager::getInstance()->delSearchType(id);
		return websocketpp::http::status_code::no_content;
	}

	void SearchApi::on(SearchManagerListener::SearchTypesChanged) noexcept {
		if (!subscriptionActive("search_types_updated"))
			return;

		send("search_types_updated", json());
	}


	json SearchApi::serializeSearchType(const string& aId, const StringList& aExtensions) noexcept {
		auto name = aId;
		if (SearchManager::isDefaultTypeStr(aId)) {
			name = string(SearchManager::getTypeStr(aId[0] - '0'));
		}

		return {
			{ "id", Serializer::getFileTypeId(aId) },
			{ "str", name },
			{ "name", name },
			{ "extensions", aExtensions },
			{ "default_type", SearchManager::isDefaultTypeStr(aId) }
		};
	}


	string SearchApi::parseSearchTypeId(ApiRequest& aRequest) noexcept {
		auto id = aRequest.getStringParam(SEARCH_TYPE_ID);

		if (id == Serializer::getFileTypeId(Util::toString(Search::TYPE_AUDIO))) {
			return Util::toString(Search::TYPE_AUDIO);
		} else if (id == Serializer::getFileTypeId(Util::toString(Search::TYPE_COMPRESSED))) {
			return Util::toString(Search::TYPE_COMPRESSED);
		} else if (id == Serializer::getFileTypeId(Util::toString(Search::TYPE_DOCUMENT))) {
			return Util::toString(Search::TYPE_DOCUMENT);
		} else if (id == Serializer::getFileTypeId(Util::toString(Search::TYPE_EXECUTABLE))) {
			return Util::toString(Search::TYPE_EXECUTABLE);
		} else if (id == Serializer::getFileTypeId(Util::toString(Search::TYPE_PICTURE))) {
			return Util::toString(Search::TYPE_PICTURE);
		} else if (id == Serializer::getFileTypeId(Util::toString(Search::TYPE_VIDEO))) {
			return Util::toString(Search::TYPE_VIDEO);
		}

		return id;
	}
}