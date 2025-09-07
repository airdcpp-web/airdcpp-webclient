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

#include <api/SearchEntity.h>

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>

#include <airdcpp/queue/QueueAddInfo.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/search/SearchManager.h>
#include <airdcpp/search/SearchInstance.h>


namespace webserver {
	const StringList SearchEntity::subscriptionList = {
		"search_user_result",
		"search_result_added",
		"search_result_updated",
		"search_hub_searches_queued",
		"search_hub_searches_sent",
	};

	SearchEntity::SearchEntity(ParentType* aParentModule, const SearchInstancePtr& aSearch) :
		SubApiModule(aParentModule, aSearch->getToken()), search(aSearch),
		searchView("search_view", this, SearchUtils::propertyHandler, std::bind(&SearchEntity::getResultList, this)) {

		createSubscriptions(subscriptionList);

		METHOD_HANDLER(Access::SEARCH,		METHOD_POST,	(EXACT_PARAM("hub_search")),									SearchEntity::handlePostHubSearch);
		METHOD_HANDLER(Access::SEARCH,		METHOD_POST,	(EXACT_PARAM("user_search")),									SearchEntity::handlePostUserSearch);

		METHOD_HANDLER(Access::SEARCH,		METHOD_GET,		(EXACT_PARAM("results"), RANGE_START_PARAM, RANGE_MAX_PARAM),	SearchEntity::handleGetResults);
		METHOD_HANDLER(Access::SEARCH,		METHOD_GET,		(EXACT_PARAM("results"), TTH_PARAM),							SearchEntity::handleGetResult);
		METHOD_HANDLER(Access::DOWNLOAD,	METHOD_POST,	(EXACT_PARAM("results"), TTH_PARAM, EXACT_PARAM("download")),	SearchEntity::handleDownload);
		METHOD_HANDLER(Access::SEARCH,		METHOD_GET,		(EXACT_PARAM("results"), TTH_PARAM, EXACT_PARAM("children")),	SearchEntity::handleGetChildren);
	}

	SearchEntity::~SearchEntity() {
		search->removeListener(this);
	}

	SearchInstanceToken SearchEntity::getId() const noexcept {
		return search->getToken();
	}

	void SearchEntity::init() noexcept {
		search->addListener(this);
	}

	GroupedSearchResultList SearchEntity::getResultList() noexcept {
		return search->getResultList();
	}

	api_return SearchEntity::handleGetResults(ApiRequest& aRequest) {
		// Serialize the most relevant results first
		auto j = Serializer::serializeItemList(aRequest.getRangeParam(START_POS), aRequest.getRangeParam(MAX_COUNT), SearchUtils::propertyHandler, search->getResultSet());

		aRequest.setResponseBody(j);
		return http_status::ok;
	}

	api_return SearchEntity::handleGetChildren(ApiRequest& aRequest) {
		auto result = parseResultParam(aRequest);

		aRequest.setResponseBody(Serializer::serializeList(result->getChildren(), serializeSearchResult));
		return http_status::ok;
	}

	api_return SearchEntity::handleGetResult(ApiRequest& aRequest) {
		auto result = parseResultParam(aRequest);

		auto j = Serializer::serializeItem(result, SearchUtils::propertyHandler);
		aRequest.setResponseBody(j);
		return http_status::ok;
	}

	json SearchEntity::serializeSearchQuery(const SearchPtr& aQuery) noexcept {
		if (!aQuery) {
			return nullptr;
		}

		return {
			{ "pattern", aQuery->query },
			{ "min_size", aQuery->minSize ? json(*aQuery->minSize) : json() },
			{ "max_size", aQuery->maxSize ? json(*aQuery->maxSize) : json() },
			{ "file_type", FileSearchParser::serializeSearchType(Util::toString(aQuery->fileType)) }, // TODO: custom types
			{ "extensions", aQuery->exts },
			{ "excluded", aQuery->excluded },
		};
	}

	json SearchEntity::serializeSearchResult(const SearchResultPtr& aSR) noexcept {
		auto isDirectory = aSR->getType() == SearchResult::Type::DIRECTORY;
		return { 
			{ "id", aSR->getId() },
			{ "name", aSR->getFileName() },
			{ "dupe", isDirectory ? Serializer::serializeDirectoryDupe(aSR->getDupe(), aSR->getAdcPath()) : Serializer::serializeFileDupe(aSR->getDupe(), aSR->getTTH()) },
			{ "type", isDirectory ? Serializer::serializeFolderType(aSR->getContentInfo()) : Serializer::serializeFileType(aSR->getAdcPath()) },
			{ "path", aSR->getAdcPath() },
			{ "tth", isDirectory ? Util::emptyString : aSR->getTTH().toBase32() },
			{ "size", aSR->getSize() },
			{ "ip", Serializer::serializeIp(aSR->getIP()) },
			{ "user", Serializer::serializeHintedUser(aSR->getUser()) },
			{ "connection", aSR->getConnectionInt() },
			{ "time", aSR->getDate() },
			{ "slots", Serializer::serializeSlots(aSR->getFreeSlots(), aSR->getTotalSlots()) },
		};
	}

	GroupedSearchResultPtr SearchEntity::parseResultParam(ApiRequest& aRequest) {
		auto resultId = aRequest.getTTHParam();
		auto result = search->getResult(resultId);
		if (!result) {
			throw RequestException(http_status::not_found, "Result " + resultId.toBase32() + " was not found");
		}

		return result;
	}

	api_return SearchEntity::handleDownload(ApiRequest& aRequest) {
		auto result = parseResultParam(aRequest);

		string targetDirectory, targetName = result->getFileName();
		Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), aRequest.getSession().get(), targetDirectory, targetName, prio);
		addAsyncTask([
			result,
			targetName,
			targetDirectory,
			prio,
			complete = aRequest.defer(),
			caller = aRequest.getOwnerPtr()
		] {
			try {
				json responseData;
				if (result->isDirectory()) {
					auto directoryDownloads = result->downloadDirectoryHooked(targetDirectory, targetName, prio, caller);
					responseData = {
						{ "directory_download_ids", Serializer::serializeList(directoryDownloads, Serializer::serializeDirectoryDownload) }
					};
				} else {
					auto bundleAddInfo = result->downloadFileHooked(targetDirectory, targetName, prio, caller);
					responseData = {
						{ "bundle_info", Serializer::serializeBundleAddInfo(bundleAddInfo) },
					};
				}

				complete(http_status::ok, responseData, nullptr);
				return;
			} catch (const Exception& e) {
				complete(http_status::bad_request, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
				return;
			}
		});

		return CODE_DEFERRED;
	}

	api_return SearchEntity::handlePostHubSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		// Parse request
		auto s = FileSearchParser::parseSearch(reqJson, false);
		auto hubs = Deserializer::deserializeHubUrls(reqJson);

		if (s->priority <= Priority::NORMAL && ClientManager::getInstance()->hasSearchQueueOverflow()) {
			aRequest.setResponseErrorStr("Search queue overflow");
			return http_status::service_unavailable;
		}

		auto queueResult = search->hubSearch(hubs, s);
		if (queueResult.queuedHubUrls.empty() && !queueResult.error.empty()) {
			aRequest.setResponseErrorStr(queueResult.error);
			return http_status::bad_request;
		}

		aRequest.setResponseBody(serializeSearchQueueInfo(queueResult.queueTime, queueResult.queuedHubUrls.size()));
		return http_status::ok;
	}

	json SearchEntity::serializeSearchQueueInfo(uint64_t aQueueItem, size_t aQueueCount) noexcept {
		return {
			{ "queue_time", aQueueItem },
			{ "search_id", search->getCurrentSearchToken() },
			{ "queued_count", aQueueCount },
			{ "query", serializeSearchQuery(search->getCurrentParams()) },
		};
	}

	api_return SearchEntity::handlePostUserSearch(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		// Parse user and query
		auto user = Deserializer::deserializeHintedUser(reqJson);
		auto s = FileSearchParser::parseSearch(reqJson, true);

		addAsyncTask([
			this,
			user,
			s,
			complete = aRequest.defer(),
			callerPtr = aRequest.getOwnerPtr()
		] {
			string error;
			if (!search->userSearchHooked(user, s, error)) {
				complete(http_status::bad_request, nullptr, ApiRequest::toResponseErrorStr(error));
			} else {
				complete(http_status::no_content, nullptr, nullptr);
			}
		});

		return CODE_DEFERRED;
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

	void SearchEntity::on(SearchInstanceListener::ChildResultAdded, const GroupedSearchResultPtr& aResult, const SearchResultPtr&) noexcept {
		searchView.onItemUpdated(aResult, {
			SearchUtils::PROP_RELEVANCE, SearchUtils::PROP_CONNECTION,
			SearchUtils::PROP_HITS, SearchUtils::PROP_SLOTS,
			SearchUtils::PROP_USERS, SearchUtils::PROP_DATE
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


	void SearchEntity::on(SearchInstanceListener::HubSearchQueued, const string& aSearchToken, uint64_t aQueueTime, size_t aQueuedCount) noexcept {
		if (subscriptionActive("search_hub_searches_queued")) {
			send("search_hub_searches_queued", serializeSearchQueueInfo(aQueueTime, aQueuedCount));
		}
	}

	void SearchEntity::on(SearchInstanceListener::HubSearchSent, const string& aSearchToken, int aSent) noexcept {
		if (subscriptionActive("search_hub_searches_sent")) {
			send("search_hub_searches_sent", {
				{ "search_id", aSearchToken },
				{ "query", serializeSearchQuery(search->getCurrentParams()) },
				{ "sent", aSent }
			});
		}
	}
}