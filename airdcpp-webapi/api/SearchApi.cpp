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

#include <api/SearchApi.h>

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>

#include <airdcpp/BundleInfo.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/DirectSearch.h>
#include <airdcpp/SearchInstance.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/ShareManager.h>


namespace webserver {
	StringList SearchApi::subscriptionList = {

	};

	SearchApi::SearchApi(Session* aSession) : 
		ParentApiModule("instance", TOKEN_PARAM, Access::SEARCH, aSession, subscriptionList, SearchEntity::subscriptionList,
			[](const string& aId) { return Util::toUInt32(aId); },
			[](const SearchEntity& aInfo) { return serializeSearchInstance(aInfo); }
		),
		searchView("search_view", this, SearchUtils::propertyHandler, std::bind(&SearchApi::getResultList, this)),
		timer(getTimer([this] { onTimer(); }, 30 * 1000)) {

		SearchManager::getInstance()->addListener(this);

		METHOD_HANDLER("instance", Access::SEARCH, ApiRequest::METHOD_POST, (), false, SearchApi::handleCreateInstance);
		METHOD_HANDLER("instance", Access::SEARCH, ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, SearchApi::handleDeleteInstance);

		METHOD_HANDLER("types", Access::ANY, ApiRequest::METHOD_GET, (), false, SearchApi::handleGetTypes);

		METHOD_HANDLER("query", Access::SEARCH, ApiRequest::METHOD_POST, (), true, SearchApi::handlePostHubSearch); // deprecated
		METHOD_HANDLER("query", Access::SEARCH, ApiRequest::METHOD_POST, (EXACT_PARAM("user")), true, SearchApi::handlePostUserSearch); // deprecated
		METHOD_HANDLER("query", Access::SEARCH, ApiRequest::METHOD_POST, (EXACT_PARAM("share")), true, SearchApi::handlePostShareSearch); // deprecated

		METHOD_HANDLER("results", Access::SEARCH, ApiRequest::METHOD_GET, (NUM_PARAM, NUM_PARAM), false, SearchApi::handleGetResults); // deprecated
		METHOD_HANDLER("result", Access::DOWNLOAD, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("download")), false, SearchApi::handleDownload); // deprecated
		METHOD_HANDLER("result", Access::SEARCH, ApiRequest::METHOD_GET, (TOKEN_PARAM, EXACT_PARAM("children")), false, SearchApi::handleGetChildren); // deprecated

		// Create an initial search instance
		if (aSession->getSessionType() != Session::TYPE_BASIC_AUTH) {
			createInstance(0);
		}

		timer->start(false);
	}

	SearchApi::~SearchApi() {
		timer->stop(true);
		SearchManager::getInstance()->removeListener(this);
	}

	void SearchApi::onTimer() noexcept {
		vector<SearchInstanceToken> expiredIds;
		forEachSubModule([&](const SearchEntity& aInstance) {
			if (aInstance.getExpirationTick() > 0 && GET_TICK() > aInstance.getExpirationTick()) {
				expiredIds.push_back(aInstance.getId());
				dcdebug("Removing an expired search instance (expiration: " U64_FMT ", now: " U64_FMT ")\n", aInstance.getExpirationTick(), GET_TICK());
			}
		});

		for (const auto& id : expiredIds) {
			removeSubModule(id);
		}
	}

	json SearchApi::serializeSearchInstance(const SearchEntity& aSearch) noexcept {
		return {
			{ "id", aSearch.getId() },
			{ "expiration", static_cast<int64_t>(aSearch.getExpirationTick()) - static_cast<int64_t>(GET_TICK()) },
		};
	}

	SearchEntity::Ptr SearchApi::createInstance(uint64_t aExpirationTick) {
		auto id = instanceIdCounter++;
		auto module = std::make_shared<SearchEntity>(this, make_shared<SearchInstance>(), id, aExpirationTick);

		addSubModule(id, module);
		return module;
	}

	api_return SearchApi::handleCreateInstance(ApiRequest& aRequest) {
		auto expirationMinutes = JsonUtil::getOptionalFieldDefault<int>("expiration", aRequest.getRequestBody(), 5);

		auto instance = createInstance(GET_TICK() + expirationMinutes * 60 * 1000);

		aRequest.setResponseBody(serializeSearchInstance(*instance.get()));
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handleDeleteInstance(ApiRequest& aRequest) {
		auto instance = getSubModule(aRequest);
		removeSubModule(instance->getId());

		return websocketpp::http::status_code::no_content;
	}

	api_return SearchApi::handleGetResults(ApiRequest& aRequest) {
		// Serialize the most relevant results first
		GroupedSearchResult::Set resultSet;

		{
			RLock l(cs);
			boost::range::copy(results | map_values, inserter(resultSet, resultSet.begin()));
		}

		auto j = Serializer::serializeItemList(aRequest.getRangeParam(0), aRequest.getRangeParam(1), SearchUtils::propertyHandler, resultSet);

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handleGetChildren(ApiRequest& aRequest) {
		auto result = getResult(Deserializer::parseTTH(aRequest.getStringParam(0)));
		if (!result) {
			aRequest.setResponseErrorStr("Result not found");
			return websocketpp::http::status_code::not_found;
		}

		SearchResultList results;
		{
			RLock l(cs);
			results = result->getChildren();
		}

		auto j = json::array();
		for (const auto& sr : results) {
			j.push_back(serializeSearchResult(sr));
		}

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	json SearchApi::serializeSearchResult(const SearchResultPtr& aSR) noexcept {
		return {
			{ "path", Util::toAdcFile(aSR->getPath()) },
			{ "ip", Serializer::serializeIp(aSR->getIP()) },
			{ "user", Serializer::serializeHintedUser(aSR->getUser()) },
			{ "connection", aSR->getConnectionInt() },
			{ "time", aSR->getDate() },
			{ "slots", Serializer::serializeSlots(aSR->getFreeSlots(), aSR->getTotalSlots()) }
		};
	}

	GroupedSearchResultPtr SearchApi::getResult(GroupedResultToken aToken) {
		RLock l(cs);
		auto i = find_if(results | map_values, [&](const GroupedSearchResultPtr& aSI) { return aSI->getToken() == aToken.toBase32(); });
		if (i.base() == results.end()) {
			return nullptr;
		}

		return *i;
	}

	GroupedSearchResultList SearchApi::getResultList() {
		GroupedSearchResultList ret;

		RLock l(cs);
		boost::range::copy(results | map_values, back_inserter(ret));
		return ret;
	}

	api_return SearchApi::handleDownload(ApiRequest& aRequest) {
		auto result = getResult(Deserializer::parseTTH(aRequest.getStringParam(0)));
		if (!result) {
			aRequest.setResponseErrorStr("Result not found");
			return websocketpp::http::status_code::not_found;
		}

		string targetDirectory, targetName = result->getFileName();
		Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), aRequest.getSession(), targetDirectory, targetName, prio);

		try {
			if (result->isDirectory()) {
				auto directoryDownloadIds = result->downloadDirectory(targetDirectory, targetName, prio);
				aRequest.setResponseBody({
					{ "directory_downloads", Serializer::serializeList(directoryDownloadIds, Serializer::serializeDirectoryDownload) }
				});
			} else {
				auto bundleAddInfo = result->downloadFile(targetDirectory, targetName, prio);
				aRequest.setResponseBody({
					{ "bundle_info", Serializer::serializeBundleAddInfo(bundleAddInfo) }
				});
			}
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.what());
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
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

	api_return SearchApi::handlePostHubSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		// Parse request
		currentSearchToken = Util::toString(Util::rand());
		auto s = FileSearchParser::parseSearch(reqJson, false, currentSearchToken);

		auto hubs = Deserializer::deserializeHubUrls(reqJson);

		// Result matching
		curSearch = shared_ptr<SearchQuery>(SearchQuery::getSearch(s));

		// Reset old data
		{
			WLock l(cs);
			results.clear();
		}
		searchView.resetItems();

		// Send
		auto result = SearchManager::getInstance()->search(hubs, s);
		aRequest.setResponseBody({
			{ "queue_time", result.queueTime },
			{ "search_id", currentSearchToken },
			{ "sent", result.succeed },
		});

		return websocketpp::http::status_code::ok;
	}

	api_return SearchApi::handlePostShareSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		// Parse share profile and query
		auto profile = Deserializer::deserializeOptionalShareProfile(reqJson);
		auto s = FileSearchParser::parseSearch(reqJson, true, Util::toString(Util::rand()));

		// Search
		unique_ptr<SearchQuery> matcher(SearchQuery::getSearch(s));
		SearchResultList results;

		try {
			ShareManager::getInstance()->adcSearch(results, *matcher, profile, ClientManager::getInstance()->getMyCID(), s->path);
		} catch (...) {}

		// Serialize results
		aRequest.setResponseBody(serializeDirectSearchResults(results, *matcher.get()));
		return websocketpp::http::status_code::ok;
	}

	json SearchApi::serializeDirectSearchResults(const SearchResultList& aResults, SearchQuery& aQuery) noexcept {
		// Construct SearchResultInfos
		GroupedSearchResult::Set resultSet;
		for (const auto& sr : aResults) {
			SearchResult::RelevanceInfo relevanceInfo;
			if (sr->getRelevance(aQuery, relevanceInfo)) {
				resultSet.emplace(std::make_shared<GroupedSearchResult>(sr, move(relevanceInfo)));
			} else {
				dcassert(0);
			}
		}

		// Serialize results
		return Serializer::serializeItemList(SearchUtils::propertyHandler, resultSet);
	}

	api_return SearchApi::handlePostUserSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		// Parse user and query
		auto user = Deserializer::deserializeHintedUser(reqJson, false);
		auto s = FileSearchParser::parseSearch(reqJson, true, Util::toString(Util::rand()));

		// Search
		auto ds = DirectSearch(user, s);

		// Wait for the search to finish
		// TODO: use timer to avoid large number of searches to consume all threads
		while (true) {
			Thread::sleep(50);
			if (ds.finished()) {
				break;
			}
		}

		if (ds.hasTimedOut()) {
			aRequest.setResponseErrorStr("Search timed out");
			return websocketpp::http::status_code::service_unavailable;
		}

		// Serialize results
		unique_ptr<SearchQuery> matcher(SearchQuery::getSearch(s));
		aRequest.setResponseBody(serializeDirectSearchResults(ds.getResults(), *matcher.get()));
		return websocketpp::http::status_code::ok;
	}

	void SearchApi::on(SearchManagerListener::SR, const SearchResultPtr& aResult) noexcept {
		auto search = curSearch; // Increase the refs
		if (!search) {
			return;
		}

		SearchResult::RelevanceInfo relevanceInfo;
		{
			WLock l(cs);
			if (!aResult->getRelevance(*search.get(), relevanceInfo, currentSearchToken)) {
				return;
			}
		}

		GroupedSearchResultPtr parent = nullptr;
		bool created = false;

		{
			WLock l(cs);
			auto i = results.find(aResult->getTTH());
			if (i == results.end()) {
				parent = std::make_shared<GroupedSearchResult>(aResult, move(relevanceInfo));
				results.emplace(aResult->getTTH(), parent);
				created = true;
			} else {
				parent = i->second;
			}
		}

		if (created) {
			// New parent
			searchView.onItemAdded(parent);
		} else {
			// Existing parent from now on
			if (!parent->addChildResult(aResult)) {
				return;
			}

			searchView.onItemUpdated(parent, { 
				SearchUtils::PROP_RELEVANCE, SearchUtils::PROP_CONNECTION, 
				SearchUtils::PROP_HITS, SearchUtils::PROP_SLOTS, 
				SearchUtils::PROP_USERS 
			});
		}
	}
}