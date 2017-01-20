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

#include <api/HistoryApi.h>

#include <web-server/JsonUtil.h>
#include <api/common/Serializer.h>

#include <airdcpp/RecentManager.h>


namespace webserver {
#define HISTORY_TYPE "history_type"
	HistoryApi::HistoryApi(Session* aSession) : ApiModule(aSession) {
		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("strings"), STR_PARAM(HISTORY_TYPE)),	HistoryApi::handleGetStrings);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(EXACT_PARAM("strings"), STR_PARAM(HISTORY_TYPE)),	HistoryApi::handleDeleteStrings);
		METHOD_HANDLER(Access::ANY,				METHOD_POST,	(EXACT_PARAM("strings"), STR_PARAM(HISTORY_TYPE)),	HistoryApi::handlePostString);

		METHOD_HANDLER(Access::HUBS_VIEW,		METHOD_GET,		(EXACT_PARAM("hubs"), RANGE_MAX_PARAM),				HistoryApi::handleGetHubs);
		METHOD_HANDLER(Access::HUBS_VIEW,		METHOD_POST,	(EXACT_PARAM("hubs"), EXACT_PARAM("search")),		HistoryApi::handleSearchHubs);
	}

	HistoryApi::~HistoryApi() {
	}

	api_return HistoryApi::handleGetStrings(ApiRequest& aRequest) {
		auto type = toHistoryType(aRequest.getStringParam(HISTORY_TYPE));
		auto history = SettingsManager::getInstance()->getHistory(type);
		aRequest.setResponseBody(history);
		return websocketpp::http::status_code::ok;
	}

	api_return HistoryApi::handlePostString(ApiRequest& aRequest) {
		auto type = toHistoryType(aRequest.getStringParam(HISTORY_TYPE));
		auto item = JsonUtil::getField<string>("string", aRequest.getRequestBody(), false);

		SettingsManager::getInstance()->addToHistory(item, type);
		return websocketpp::http::status_code::no_content;
	}

	api_return HistoryApi::handleDeleteStrings(ApiRequest& aRequest) {
		auto type = toHistoryType(aRequest.getStringParam(HISTORY_TYPE));
		SettingsManager::getInstance()->clearHistory(type);
		return websocketpp::http::status_code::no_content;
	}

	SettingsManager::HistoryType HistoryApi::toHistoryType(const string& aName) {
		if (aName == "search_pattern") {
			return SettingsManager::HISTORY_SEARCH;
		} else if (aName == "search_excluded") {
			return SettingsManager::HISTORY_EXCLUDE;
		} else if (aName == "download_target") {
			return SettingsManager::HISTORY_DOWNLOAD_DIR;
		}

		dcassert(0);
		throw RequestException(websocketpp::http::status_code::bad_request, "Invalid string history type");
	}

	json HistoryApi::serializeHub(const RecentEntryPtr& aHub) noexcept {
		return {
			{ "name", aHub->getName() },
			{ "description", aHub->getDescription() },
			{ "hub_url", aHub->getUrl() }
		};
	}

	api_return HistoryApi::handleSearchHubs(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto pattern = JsonUtil::getField<string>("pattern", reqJson);
		auto maxResults = JsonUtil::getField<size_t>("max_results", reqJson);

		auto hubs = RecentManager::getInstance()->searchRecents(pattern, maxResults);
		aRequest.setResponseBody(Serializer::serializeList(hubs, serializeHub));
		return websocketpp::http::status_code::ok;
	}

	api_return HistoryApi::handleGetHubs(ApiRequest& aRequest) {
		auto hubs = RecentManager::getInstance()->getRecents();

		auto retJson = Serializer::serializeFromEnd(aRequest.getRangeParam(MAX_COUNT), hubs, serializeHub);
		aRequest.setResponseBody(retJson);

		return websocketpp::http::status_code::ok;
	}
}
