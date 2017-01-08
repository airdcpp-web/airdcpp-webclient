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

#include <api/SearchEntity.h>

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>

#include <airdcpp/BundleInfo.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/SearchInstance.h>


namespace webserver {
	const StringList SearchEntity::subscriptionList = {
		"search_user_result",
		"search_result_added",
		"search_result_updated",
	};

	SearchEntity::SearchEntity(ParentType* aParentModule, const SearchInstancePtr& aSearch, SearchInstanceToken aId, uint64_t aExpirationTick) :
		expirationTick(aExpirationTick), id(aId),
		SubApiModule(aParentModule, aId, subscriptionList), search(aSearch),
		searchView("search_view", this, SearchUtils::propertyHandler, std::bind(&SearchEntity::getResultList, this)) {

		METHOD_HANDLER("hub_search", Access::SEARCH, ApiRequest::METHOD_POST, (), true, SearchEntity::handlePostHubSearch);
		METHOD_HANDLER("user_search", Access::SEARCH, ApiRequest::METHOD_POST, (), true, SearchEntity::handlePostUserSearch);

		METHOD_HANDLER("results", Access::SEARCH, ApiRequest::METHOD_GET, (NUM_PARAM, NUM_PARAM), false, SearchEntity::handleGetResults);
		METHOD_HANDLER("result", Access::DOWNLOAD, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("download")), false, SearchEntity::handleDownload);
		METHOD_HANDLER("result", Access::SEARCH, ApiRequest::METHOD_GET, (TOKEN_PARAM, EXACT_PARAM("children")), false, SearchEntity::handleGetChildren);
	}

	SearchEntity::~SearchEntity() {
		search->removeListener(this);
	}

	void SearchEntity::init() noexcept {
		search->addListener(this);
	}

	GroupedSearchResultList SearchEntity::getResultList() noexcept {
		return search->getResultList();
	}

	api_return SearchEntity::handleGetResults(ApiRequest& aRequest) {
		// Serialize the most relevant results first
		auto j = Serializer::serializeItemList(aRequest.getRangeParam(0), aRequest.getRangeParam(1), SearchUtils::propertyHandler, search->getResultSet());

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return SearchEntity::handleGetChildren(ApiRequest& aRequest) {
		//auto result = getResult(aRequest.getTokenParam(0));
		auto result = search->getResult(Deserializer::parseTTH(aRequest.getStringParam(0)));
		if (!result) {
			aRequest.setResponseErrorStr("Result not found");
			return websocketpp::http::status_code::not_found;
		}

		auto j = json::array();
		for (const auto& sr : result->getChildren()) {
			j.push_back(serializeSearchResult(sr));
		}

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	json SearchEntity::serializeSearchResult(const SearchResultPtr& aSR) noexcept {
		return {
			{ "id", aSR->getId() },
			{ "path", Util::toAdcFile(aSR->getPath()) },
			{ "ip", Serializer::serializeIp(aSR->getIP()) },
			{ "user", Serializer::serializeHintedUser(aSR->getUser()) },
			{ "connection", aSR->getConnectionInt() },
			{ "time", aSR->getDate() },
			{ "slots", Serializer::serializeSlots(aSR->getFreeSlots(), aSR->getTotalSlots()) },
		};
	}

	api_return SearchEntity::handleDownload(ApiRequest& aRequest) {
		//auto result = search->getResult(aRequest.getTokenParam(0));
		auto result = search->getResult(Deserializer::parseTTH(aRequest.getStringParam(0)));
		if (!result) {
			aRequest.setResponseErrorStr("Result not found");
			return websocketpp::http::status_code::not_found;
		}

		string targetDirectory, targetName = result->getFileName();
		Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), aRequest.getSession(), targetDirectory, targetName, prio);

		try {
			if (result->isDirectory()) {
				auto directoryDownloads = result->downloadDirectory(targetDirectory, targetName, prio);
				aRequest.setResponseBody({
					{ "directory_download_ids", Serializer::serializeList(directoryDownloads, Serializer::serializeDirectoryDownload) }
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

	api_return SearchEntity::handlePostHubSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		// Parse request
		auto s = FileSearchParser::parseSearch(reqJson, false, Util::toString(Util::rand()));
		auto hubs = Deserializer::deserializeHubUrls(reqJson);

		auto queueResult = search->hubSearch(hubs, s);
		aRequest.setResponseBody({
			{ "queue_time", queueResult.queueTime },
			{ "search_id", search->getCurrentSearchToken() },
			{ "sent", queueResult.succeed },
		});

		return websocketpp::http::status_code::ok;
	}

	api_return SearchEntity::handlePostUserSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		// Parse user and query
		auto user = Deserializer::deserializeHintedUser(reqJson, false);
		auto s = FileSearchParser::parseSearch(reqJson, true, Util::toString(Util::rand()));

		search->userSearch(user, s);
		return websocketpp::http::status_code::ok;
	}

	void SearchEntity::on(SearchInstanceListener::GroupedResultAdded, const GroupedSearchResultPtr& aResult) noexcept {
		searchView.onItemAdded(aResult);

		if (subscriptionActive("search_result_added")) {
			send("search_result_added", {
				{ "search_id", search->getCurrentSearchToken() },
				{ "result", Serializer::serializeItem(aResult, SearchUtils::propertyHandler) }
			});
		}
	}

	void SearchEntity::on(SearchInstanceListener::GroupedResultUpdated, const GroupedSearchResultPtr& aResult) noexcept {
		searchView.onItemUpdated(aResult, {
			SearchUtils::PROP_RELEVANCE, SearchUtils::PROP_CONNECTION,
			SearchUtils::PROP_HITS, SearchUtils::PROP_SLOTS,
			SearchUtils::PROP_USERS
		});
		
		if (subscriptionActive("search_result_updated")) {
			send("search_result_updated", {
				{ "search_id", search->getCurrentSearchToken() },
				{ "result", Serializer::serializeItem(aResult, SearchUtils::propertyHandler) }
			});
		}
	}

	void SearchEntity::on(SearchInstanceListener::UserResult, const SearchResultPtr& aResult, const GroupedSearchResultPtr& aParent) noexcept {
		if (subscriptionActive("search_user_result")) {
			send("search_user_result", {
				{ "search_id", search->getCurrentSearchToken() },
				{ "parent_id", aParent->getToken() },
				{ "result", serializeSearchResult(aResult) }
			});
		}
	}

	void SearchEntity::on(SearchInstanceListener::Reset) noexcept {
		searchView.resetItems();
	}
}