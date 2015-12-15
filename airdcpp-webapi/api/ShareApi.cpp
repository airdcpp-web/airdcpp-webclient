/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#include <api/ShareApi.h>
#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>
#include <api/ShareUtils.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ShareManager.h>
#include <airdcpp/HubEntry.h>

namespace webserver {
	ShareApi::ShareApi(Session* aSession) : ApiModule(aSession) {

		METHOD_HANDLER("grouped_root_paths", Access::ANY, ApiRequest::METHOD_GET, (), false, ShareApi::handleGetGroupedRootPaths);
		METHOD_HANDLER("stats", Access::ANY, ApiRequest::METHOD_GET, (), false, ShareApi::handleGetStats);
		METHOD_HANDLER("find_dupe_paths", Access::ANY, ApiRequest::METHOD_POST, (), true, ShareApi::handleFindDupePaths);

		METHOD_HANDLER("refresh", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (), false, ShareApi::handleRefreshShare);
		METHOD_HANDLER("refresh", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("paths")), true, ShareApi::handleRefreshPaths);
	}

	ShareApi::~ShareApi() {
	}

	api_return ShareApi::handleRefreshShare(ApiRequest& aRequest) {
		auto incomingOptional = JsonUtil::getOptionalField<bool>("incoming", aRequest.getRequestBody());
		auto ret = ShareManager::getInstance()->refresh(incomingOptional ? *incomingOptional : false);

		//aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleRefreshPaths(ApiRequest& aRequest) {
		auto paths = JsonUtil::getField<StringList>("paths", aRequest.getRequestBody(), false);
		auto ret = ShareManager::getInstance()->refreshPaths(paths);

		//aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleGetStats(ApiRequest& aRequest) {
		json j;

		auto optionalStats = ShareManager::getInstance()->getShareStats();
		if (!optionalStats) {
			return websocketpp::http::status_code::no_content;
		}

		auto stats = *optionalStats;
		j["total_file_count"] = stats.totalFileCount;
		j["total_directory_count"] = stats.totalDirectoryCount;
		j["files_per_directory"] = stats.filesPerDirectory;
		j["total_size"] = stats.totalSize;
		j["unique_file_percentage"] = stats.uniqueFilePercentage;
		j["unique_files"] = stats.uniqueFileCount;
		j["average_file_age"] = stats.averageFileAge;
		j["profile_count"] = stats.profileCount;
		j["profile_root_count"] = stats.profileDirectoryCount;

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleGetGroupedRootPaths(ApiRequest& aRequest) {
		json ret;

		auto roots = ShareManager::getInstance()->getGroupedDirectories();
		if (!roots.empty()) {
			for (const auto& vPath : roots) {
				json parentJson;
				parentJson["name"] = vPath.first;
				for (const auto& realPath : vPath.second) {
					parentJson["paths"].push_back(realPath);
				}

				ret.push_back(parentJson);
			}
		} else {
			ret = json::array();
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareApi::handleFindDupePaths(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		json ret;

		StringList paths;
		auto path = JsonUtil::getOptionalField<string>("path", reqJson, false, false);
		if (path) {
			paths = ShareManager::getInstance()->getDirPaths(Util::toNmdcFile(*path));
		} else {
			auto tth = Deserializer::deserializeTTH(reqJson);
			paths = ShareManager::getInstance()->getRealPaths(tth);
		}

		if (!paths.empty()) {
			for (const auto& p : paths) {
				ret.push_back(p);
			}
		} else {
			ret = json::array();
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}
}