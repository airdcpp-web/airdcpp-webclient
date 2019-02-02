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

#include <api/FilesystemApi.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/File.h>

#ifdef WIN32
#include <api/platform/windows/Filesystem.h>
#endif

namespace webserver {
	FilesystemApi::FilesystemApi(Session* aSession) : ApiModule(aSession) {
		METHOD_HANDLER(Access::ANY,				METHOD_POST, (EXACT_PARAM("disk_info")),	FilesystemApi::handleGetDiskInfo);
		METHOD_HANDLER(Access::FILESYSTEM_VIEW, METHOD_POST, (EXACT_PARAM("list_items")),	FilesystemApi::handleListItems);
		METHOD_HANDLER(Access::FILESYSTEM_EDIT, METHOD_POST, (EXACT_PARAM("directory")),	FilesystemApi::handlePostDirectory);
	}

	FilesystemApi::~FilesystemApi() {
	}

	api_return FilesystemApi::handleListItems(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = JsonUtil::getField<string>("path", reqJson, 
#ifdef WIN32
			true
#else
			false
#endif
		);

		auto dirsOnly = JsonUtil::getOptionalFieldDefault<bool>("directories_only", reqJson, false);

		auto retJson = json::array();
		if (path.empty()) {
#ifdef WIN32
			retJson = Filesystem::getDriveListing(false);
#endif
		} else {
			if (!Util::fileExists(path)) {
				aRequest.setResponseErrorStr("The path doesn't exist on disk");
				return websocketpp::http::status_code::bad_request;
			}

			try {
				retJson = serializeDirectoryContent(path, dirsOnly);
			} catch (const FileException& e) {
				aRequest.setResponseErrorStr("Failed to get directory content: " + e.getError());
				return websocketpp::http::status_code::internal_server_error;
			}
		}

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
	}

	json FilesystemApi::serializeDirectoryContent(const string& aPath, bool aDirectoriesOnly) {
		auto retJson = json::array();

		File::forEachFile(aPath, "*", [&](const FilesystemItem& aInfo) {
			if (aDirectoriesOnly && !aInfo.isDirectory) {
				return;
			}

			retJson.push_back(Serializer::serializeFilesystemItem(aInfo));
		});

		return retJson;
	}

	api_return FilesystemApi::handlePostDirectory(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = JsonUtil::getField<string>("path", reqJson, false);
		try {
			if (!File::createDirectory(path)) {
				aRequest.setResponseErrorStr("Directory exists");
				return websocketpp::http::status_code::bad_request;
			}
		} catch (const FileException& e) {
			aRequest.setResponseErrorStr("Failed to create directory: " + e.getError());
			return websocketpp::http::status_code::internal_server_error;
		}

		return websocketpp::http::status_code::no_content;
	}

	api_return FilesystemApi::handleGetDiskInfo(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		auto paths = JsonUtil::getField<StringList>("paths", reqJson, false);

		auto volumes = File::getVolumes();

		json retJson;
		for (const auto& path : paths) {
			auto targetInfo = File::getDiskInfo(path, volumes, false);

			retJson.push_back({
				{ "path", path },
				{ "free_space", targetInfo.freeSpace },
				{ "total_space", targetInfo.totalSpace },
			});
		}

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
	}
}
