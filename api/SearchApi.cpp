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

#include <airdcpp/ScopedFunctor.h>

const unsigned int MIN_SEARCH = 2;

namespace webserver {
	SearchApi::SearchApi(Session* aSession) : ApiModule(aSession, Access::SEARCH), itemHandler(properties,
		SearchUtils::getStringInfo, SearchUtils::getNumericInfo, SearchUtils::compareResults, SearchUtils::serializeResult), 
		searchView("search_view", this, itemHandler, std::bind(&SearchApi::getResultList, this)) {

		SearchManager::getInstance()->addListener(this);

		METHOD_HANDLER("query", Access::SEARCH, ApiRequest::METHOD_POST, (), true, SearchApi::handlePostSearch);
		METHOD_HANDLER("types", Access::ANY, ApiRequest::METHOD_GET, (), false, SearchApi::handleGetTypes);

		METHOD_HANDLER("result", Access::DOWNLOAD, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("download")), false, SearchApi::handleDownload);
	}

	SearchApi::~SearchApi() {
		SearchManager::getInstance()->removeListener(this);
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

		string targetDirectory, targetName;
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

	api_return SearchApi::handlePostSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		std::string str = reqJson["pattern"];

		if (str.length() < MIN_SEARCH) {
			aRequest.setResponseErrorStr("Search string too short");
			return websocketpp::http::status_code::bad_request;
		}

		auto type = str.size() == 39 && Encoder::isBase32(str.c_str()) ? SearchManager::TYPE_TTH : SearchManager::TYPE_ANY;

		// new search
		auto newSearch = SearchQuery::getSearch(str, Util::emptyString, 0, type, SearchManager::SIZE_DONTCARE, StringList(), SearchQuery::MATCH_FULL_PATH, false);

		{
			WLock l(cs);
			results.clear();
		}

		searchView.resetItems();

		curSearch = shared_ptr<SearchQuery>(newSearch);

		SettingsManager::getInstance()->addToHistory(str, SettingsManager::HISTORY_SEARCH);
		currentSearchToken = Util::toString(Util::rand());

		auto queueTime = SearchManager::getInstance()->search(str, 0, type, SearchManager::SIZE_DONTCARE, currentSearchToken, Search::MANUAL);

		aRequest.setResponseBody({
			{ "queue_time", queueTime },
			{ "search_token", currentSearchToken },
			{ "type", type }
		});
		return websocketpp::http::status_code::ok;
	}

	optional<RelevancyInfo> SearchApi::matches(const SearchResultPtr& aResult) const noexcept {
		auto search = curSearch;
		if (!search)
			return boost::none;

		if (!aResult->getToken().empty()) {
			// ADC
			if (currentSearchToken != aResult->getToken()) {
				return boost::none;
			}
		} else {
			// NMDC results must be matched manually

			// Exludes
			if (curSearch->isExcluded(aResult->getPath())) {
				return boost::none;
			}

			if (search->root && *search->root != aResult->getTTH()) {
				return boost::none;
			}
		}

		// All clients can't handle this correctly
		if (search->itemType == SearchQuery::TYPE_FILE && aResult->getType() != SearchResult::TYPE_FILE) {
			return boost::none;
		}


		WLock l(cs);

		// Path match (always required to get the relevancy)
		// Must be locked because the match positions are saved in SearchQuery
		SearchQuery::Recursion recursion;
		ScopedFunctor([&] { search->recursion = nullptr; });
		if (!search->root && !search->matchesNmdcPath(aResult->getPath(), recursion)) {
			return boost::none;
		}

		// Don't count the levels because they can't be compared with each others
		auto matchRelevancy = SearchQuery::getRelevancyScores(*search.get(), 0, aResult->getType() == SearchResult::TYPE_DIRECTORY, aResult->getFileName());
		double sourceScoreFactor = 0.01;
		if (search->recursion && search->recursion->isComplete()) {
			// There are subdirectories/files that have more matches than the main directory
			// Don't give too much weight for those even if there are lots of sources
			sourceScoreFactor = 0.001;

			// We don't get the level scores so balance those here
			matchRelevancy = max(0.0, matchRelevancy - (0.05 * search->recursion->recursionLevel));
		}

		return RelevancyInfo({ matchRelevancy, sourceScoreFactor });
	}

	void SearchApi::on(SearchManagerListener::SR, const SearchResultPtr& aResult) noexcept {
		auto relevancyInfo = matches(aResult);
		if (!relevancyInfo) {
			return;
		}

		SearchResultInfoPtr parent = nullptr;
		auto result = std::make_shared<SearchResultInfo>(aResult, move(*relevancyInfo));

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
		searchView.onItemUpdated(parent, { PROP_RELEVANCY, PROP_CONNECTION, PROP_HITS, PROP_SLOTS, PROP_USERS });
	}
}