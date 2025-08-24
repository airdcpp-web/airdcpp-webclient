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

#include <api/FilesystemApi.h>
#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/util/PathUtil.h>

#ifdef _WIN32
#include <api/platform/windows/Filesystem.h>
#define ALLOW_LIST_EMPTY_PATH true
#else
#define ALLOW_LIST_EMPTY_PATH false
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

		// Iterating over the directory content may take a while...
		addAsyncTask([
			this,
			path = JsonUtil::getField<string>("path", reqJson, ALLOW_LIST_EMPTY_PATH),
			dirsOnly = JsonUtil::getOptionalFieldDefault<bool>("directories_only", reqJson, false),
			complete = aRequest.defer()
		]{
			auto retJson = json::array();
			if (path.empty()) {
#ifdef _WIN32
				auto content = Filesystem::getDriveListing(false);
				complete(http_status::ok, content, nullptr);
				return;
#endif
			} else {
				// Validate path
				if (!File::isDirectory(path)) {
					complete(http_status::bad_request, nullptr, ApiRequest::toResponseErrorStr("Directory " + path + " doesn't exist"));
					return;
				}

				// Return listing
				try {
					auto content = serializeDirectoryContent(path, dirsOnly);
					complete(http_status::ok, content, nullptr);
					return;
				} catch (const FileException& e) {
					complete(http_status::internal_server_error, nullptr, ApiRequest::toResponseErrorStr("Failed to get directory content: " + e.getError()));
					return;
				}
			}
		});

		return CODE_DEFERRED;
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

		auto path = PathUtil::validateDirectoryPath(JsonUtil::getField<string>("path", reqJson, false));
		try {
			if (!File::createDirectory(path)) {
				aRequest.setResponseErrorStr("Directory exists");
				return http_status::bad_request;
			}
		} catch (const FileException& e) {
			aRequest.setResponseErrorStr("Failed to create directory: " + e.getError());
			return http_status::internal_server_error;
		}

		return http_status::no_content;
	}

	api_return FilesystemApi::handleGetDiskInfo(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();
		auto paths = Deserializer::deserializeList<string>("paths", reqJson, Deserializer::directoryPathArrayValueParser, false);

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
		return http_status::ok;
	}
}
