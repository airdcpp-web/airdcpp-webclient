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

namespace webserver {
	StringList SearchApi::subscriptionList = {

	};

	SearchApi::SearchApi(Session* aSession) : 
		ParentApiModule(TOKEN_PARAM, Access::SEARCH, aSession, subscriptionList, SearchEntity::subscriptionList,
			[](const string& aId) { return Util::toUInt32(aId); },
			[](const SearchEntity& aInfo) { return serializeSearchInstance(aInfo); }
		),
		timer(getTimer([this] { onTimer(); }, 30 * 1000)) 
	{

		METHOD_HANDLER(Access::SEARCH,	METHOD_POST,	(),						SearchApi::handleCreateInstance);

		METHOD_HANDLER(Access::ANY,		METHOD_GET,		(EXACT_PARAM("types")),	SearchApi::handleGetTypes);

		// Create an initial search instance
		if (aSession->getSessionType() != Session::TYPE_BASIC_AUTH) {
			createInstance(0);
		}

		timer->start(false);
	}

	SearchApi::~SearchApi() {
		timer->stop(true);
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
		auto getName = [](const string& aId) -> string {
			if (SearchManager::isDefaultTypeStr(aId)) {
				return string(SearchManager::getTypeStr(aId[0] - '0'));
			}

			return aId;
		};

		auto types = SearchManager::getInstance()->getSearchTypes();

		json retJ;
		for (const auto& s : types) {
			retJ.push_back({
				{ "id", Serializer::getFileTypeId(s.first) },
				{ "str", getName(s.first) },
				{ "extensions", s.second },
				{ "default_type", SearchManager::isDefaultTypeStr(s.first) }
			});
		}

		aRequest.setResponseBody(retJ);
		return websocketpp::http::status_code::ok;
	}
}