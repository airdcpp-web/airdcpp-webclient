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

#include <api/HistoryApi.h>

#include <web-server/JsonUtil.h>
#include <api/common/Serializer.h>

#include <airdcpp/recents/RecentManager.h>


namespace webserver {
#define HISTORY_TYPE "history_type"
	HistoryApi::HistoryApi(Session* aSession) : ApiModule(aSession) {
		METHOD_HANDLER(Access::ANY,					METHOD_GET,		(EXACT_PARAM("strings"), STR_PARAM(HISTORY_TYPE)),	HistoryApi::handleGetStrings);
		METHOD_HANDLER(Access::SETTINGS_EDIT,		METHOD_DELETE,	(EXACT_PARAM("strings"), STR_PARAM(HISTORY_TYPE)),	HistoryApi::handleDeleteStrings);
		METHOD_HANDLER(Access::ANY,					METHOD_POST,	(EXACT_PARAM("strings"), STR_PARAM(HISTORY_TYPE)),	HistoryApi::handlePostString);

		METHOD_HANDLER(Access::ANY,					METHOD_GET,		(EXACT_PARAM("sessions"), STR_PARAM(HISTORY_TYPE), RANGE_MAX_PARAM),		HistoryApi::handleGetRecents);
		METHOD_HANDLER(Access::ANY,					METHOD_POST,	(EXACT_PARAM("sessions"), STR_PARAM(HISTORY_TYPE), EXACT_PARAM("search")),	HistoryApi::handleSearchRecents);
		METHOD_HANDLER(Access::SETTINGS_EDIT,		METHOD_DELETE,	(EXACT_PARAM("sessions"), STR_PARAM(HISTORY_TYPE)),							HistoryApi::handleClearRecents);
	}

	HistoryApi::~HistoryApi() {
	}

	api_return HistoryApi::handleGetStrings(ApiRequest& aRequest) {
		auto type = toHistoryType(aRequest);
		auto history = SettingsManager::getInstance()->getHistory(type);
		aRequest.setResponseBody(history);
		return http_status::ok;
	}

	api_return HistoryApi::handlePostString(ApiRequest& aRequest) {
		auto type = toHistoryType(aRequest);
		auto item = JsonUtil::getField<string>("string", aRequest.getRequestBody(), false);

		SettingsManager::getInstance()->addToHistory(item, type);
		return http_status::no_content;
	}

	api_return HistoryApi::handleDeleteStrings(ApiRequest& aRequest) {
		auto type = toHistoryType(aRequest);
		SettingsManager::getInstance()->clearHistory(type);
		return http_status::no_content;
	}

	RecentEntry::Type HistoryApi::toRecentType(ApiRequest& aRequest) {
		auto name = aRequest.getStringParam(HISTORY_TYPE);
		if (name == "hub") {
			return RecentEntry::TYPE_HUB;
		} else if (name == "private_chat") {
			return RecentEntry::TYPE_PRIVATE_CHAT;
		} else if (name == "filelist") {
			return RecentEntry::TYPE_FILELIST;
		}

		dcassert(0);
		throw RequestException(http_status::bad_request, "Invalid entry history type " + name);
	}

	SettingsManager::HistoryType HistoryApi::toHistoryType(ApiRequest& aRequest) {
		auto name = aRequest.getStringParam(HISTORY_TYPE);
		if (name == "search_pattern") {
			return SettingsManager::HISTORY_SEARCH;
		} else if (name == "search_excluded") {
			return SettingsManager::HISTORY_EXCLUDE;
		} else if (name == "download_target") {
			return SettingsManager::HISTORY_DOWNLOAD_DIR;
		}

		dcassert(0);
		throw RequestException(http_status::bad_request, "Invalid string history type " + name);
	}

	json HistoryApi::serializeRecentEntry(const RecentEntryPtr& aEntry) noexcept {
		return {
			{ "name", aEntry->getName() },
			{ "description", aEntry->getDescription() },
			{ "hub_url", aEntry->getUrl() },
			{ "last_opened", aEntry->getLastOpened() },
			{ "user", aEntry->getUser() ? Serializer::serializeHintedUser(HintedUser(aEntry->getUser(), aEntry->getUrl())) : json() },
		};
	}

	api_return HistoryApi::handleSearchRecents(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto pattern = JsonUtil::getField<string>("pattern", reqJson);
		auto maxResults = JsonUtil::getField<size_t>("max_results", reqJson);

		auto hubs = RecentManager::getInstance()->searchRecents(toRecentType(aRequest), pattern, maxResults);
		aRequest.setResponseBody(Serializer::serializeList(hubs, serializeRecentEntry));
		return http_status::ok;
	}

	api_return HistoryApi::handleGetRecents(ApiRequest& aRequest) {
		auto entries = RecentManager::getInstance()->getRecents(toRecentType(aRequest));
		sort(entries.begin(), entries.end(), RecentEntry::Sort());

		auto retJson = Serializer::serializeFromBegin(aRequest.getRangeParam(MAX_COUNT), entries, serializeRecentEntry);
		aRequest.setResponseBody(retJson);

		return http_status::ok;
	}

	api_return HistoryApi::handleClearRecents(ApiRequest& aRequest) {
		RecentManager::getInstance()->clearRecents(toRecentType(aRequest));
		return http_status::no_content;
	}
}
