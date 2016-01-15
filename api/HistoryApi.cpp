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
#include <api/common/Serializer.h>

namespace webserver {
	HistoryApi::HistoryApi(Session* aSession) : ApiModule(aSession) {
		METHOD_HANDLER("items", Access::ANY, ApiRequest::METHOD_GET, (NUM_PARAM), false, HistoryApi::handleGetHistory);
		METHOD_HANDLER("item", Access::ANY, ApiRequest::METHOD_POST, (NUM_PARAM), true, HistoryApi::handlePostHistory);
	}

	HistoryApi::~HistoryApi() {
	}

	api_return HistoryApi::handleGetHistory(ApiRequest& aRequest) {
		json j;

		auto history = SettingsManager::getInstance()->getHistory(toHistoryType(aRequest.getStringParam(0)));
		if (!history.empty()) {
			for (const auto& s : history) {
				j.push_back(s);
			}
		} else {
			j = json::array();
		}

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return HistoryApi::handlePostHistory(ApiRequest& aRequest) {
		auto type = toHistoryType(aRequest.getStringParam(0));
		const string item = aRequest.getRequestBody()["item"];

		SettingsManager::getInstance()->addToHistory(item, type);
		return websocketpp::http::status_code::no_content;
	}

	static SettingsManager::HistoryType toHistoryType(const string& aName) {
		auto type = Util::toInt(aName);
		if (type < 0 || type >= SettingsManager::HistoryType::HISTORY_LAST) {
			throw std::invalid_argument("Invalid history type");
		}

		return static_cast<SettingsManager::HistoryType>(type);
	}
}
