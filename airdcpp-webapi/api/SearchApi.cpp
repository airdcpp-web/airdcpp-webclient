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
#include <api/SearchUtils.h>

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>

#include <airdcpp/DirectSearch.h>
#include <airdcpp/ScopedFunctor.h>
#include <airdcpp/ShareManager.h>


namespace webserver {
	const PropertyList SearchApi::properties = {
		{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
		{ PROP_RELEVANCE, "relevance", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_HITS, "hits", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_USERS, "users", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_DATE, "time", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_PATH, "path", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_CONNECTION, "connection", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_SLOTS, "slots", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_TTH, "tth", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_DUPE, "dupe", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
	};

	const PropertyItemHandler<SearchResultInfoPtr> SearchApi::itemHandler = {
		properties,
		SearchUtils::getStringInfo, SearchUtils::getNumericInfo, SearchUtils::compareResults, SearchUtils::serializeResult
	};

	SearchApi::SearchApi(Session* aSession) : ApiModule(aSession, Access::SEARCH), 
		searchView("search_view", this, itemHandler, std::bind(&SearchApi::getResultList, this)) {

		SearchManager::getInstance()->addListener(this);

		METHOD_HANDLER("query", Access::SEARCH, ApiRequest::METHOD_POST, (), true, SearchApi::handlePostHubSearch);
		METHOD_HANDLER("query", Access::SEARCH, ApiRequest::METHOD_POST, (EXACT_PARAM("user")), true, SearchApi::handlePostUserSearch);
		METHOD_HANDLER("query", Access::SEARCH, ApiRequest::METHOD_POST, (EXACT_PARAM("share")), true, SearchApi::handlePostShareSearch);

		METHOD_HANDLER("types", Access::ANY, ApiRequest::METHOD_GET, (), false, SearchApi::handleGetTypes);

		METHOD_HANDLER("results", Access::SEARCH, ApiRequest::METHOD_GET, (NUM_PARAM, NUM_PARAM), false, SearchApi::handleGetResults);
		METHOD_HANDLER("result", Access::DOWNLOAD, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("download")), false, SearchApi::handleDownload);

		createSubscription("search_result");
	}

	SearchApi::~SearchApi() {
		SearchManager::getInstance()->removeListener(this);
	}

	api_return SearchApi::handleGetResults(ApiRequest& aRequest) {
		// Serialize the most relevant results first
		SearchResultInfo::Set resultSet;

		{
			RLock l(cs);
			boost::range::copy(results | map_values, inserter(resultSet, resultSet.begin()));
		}

		auto j = Serializer::serializeItemList(aRequest.getRangeParam(0), aRequest.getRangeParam(1), itemHandler, resultSet);

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	SearchResultInfo::List SearchApi::getResultList() {
		SearchResultInfo::List ret;

		RLock l(cs);
		boost::range::copy(results | map_values, back_inserter(ret));
		return ret;
	}

	api_return SearchApi::handleDownload(ApiRequest& aRequest) {
		SearchResultInfoPtr result = nullptr;

		{
			RLock l(cs);
			auto i = find_if(results | map_values, [&](const SearchResultInfoPtr& aSI) { return aSI->getToken() == aRequest.getTokenParam(0); });
			if (i.base() == results.end()) {
				aRequest.setResponseErrorStr("Result not found");
				return websocketpp::http::status_code::not_found;
			}

			result = *i;
		}

		string targetDirectory, targetName = result->sr->getFileName();
		TargetUtil::TargetType targetType;
		QueueItemBase::Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), targetDirectory, targetName, targetType, prio);

		return result->download(targetDirectory, targetName, targetType, prio);
	}

	api_return SearchApi::handleGetTypes(ApiRequest& aRequest) {
		auto getName = [](const string& aId) -> string {
			if (aId.size() == 1 && aId[0] >= '1' && aId[0] <= '6') {
				return string(SearchManager::getTypeStr(aId[0] - '0'));
			}

			return aId;
		};

		auto types = SearchManager::getInstance()->getSearchTypes();

		json retJ;
		for (const auto& s : types) {
			retJ.push_back({
				{ "id", s.first },
				{ "str", getName(s.first) }
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
		auto profile = Deserializer::deserializeShareProfile(reqJson);
		auto s = FileSearchParser::parseSearch(reqJson, true, Util::toString(Util::rand()));

		// Search
		unique_ptr<SearchQuery> matcher(SearchQuery::getSearch(s));
		SearchResultList results;

		try {
			ShareManager::getInstance()->search(results, *matcher, profile, CID(), s->path);
		} catch (...) {}

		// Serialize results
		aRequest.setResponseBody(serializeDirectSearchResults(results, *matcher.get()));
		return websocketpp::http::status_code::ok;
	}

	json SearchApi::serializeDirectSearchResults(const SearchResultList& aResults, SearchQuery& aQuery) noexcept {
		// Construct SearchResultInfos
		SearchResultInfo::Set resultSet;
		for (const auto& sr : aResults) {
			SearchResult::RelevanceInfo relevanceInfo;
			if (sr->getRelevance(aQuery, relevanceInfo)) {
				resultSet.emplace(std::make_shared<SearchResultInfo>(sr, move(relevanceInfo)));
			}
		}

		// Serialize results
		return Serializer::serializeItemList(itemHandler, resultSet);
	}

	api_return SearchApi::handlePostUserSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		// Parse user and query
		auto user = Deserializer::deserializeHintedUser(reqJson, false);
		auto s = FileSearchParser::parseSearch(reqJson, true, Util::toString(Util::rand()));

		// Search
		auto ds = DirectSearch(user, s);

		// Wait for the search to finish
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

		SearchResultInfoPtr parent = nullptr;
		auto result = std::make_shared<SearchResultInfo>(aResult, move(relevanceInfo));

		{
			WLock l(cs);
			auto i = results.emplace(aResult->getTTH(), result);
			if (!i.second) {
				parent = i.first->second;
			}
		}

		if (!parent) {
			searchView.onItemAdded(result);
			return;
		}

		// No duplicate results for the same user that are received via different hubs
		if (parent->hasUser(aResult->getUser())) {
			return;
		}

		// Add as child
		parent->addChildResult(result);
		searchView.onItemUpdated(parent, { PROP_RELEVANCE, PROP_CONNECTION, PROP_HITS, PROP_SLOTS, PROP_USERS });

		if (subscriptionActive("search_result")) {
			send("search_result", {
				{ "search_id", currentSearchToken },
				{ "result", Serializer::serializeItem(result, itemHandler) }
			});
		}
	}
}