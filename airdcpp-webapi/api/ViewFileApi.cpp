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

#include <web-server/stdinc.h>
#include <web-server/JsonUtil.h>

#include <api/ViewFileApi.h>

#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>

#include <airdcpp/QueueManager.h>
#include <airdcpp/ViewFileManager.h>

#include <boost/range/algorithm/copy.hpp>

namespace webserver {
	ViewFileApi::ViewFileApi(Session* aSession) : ApiModule(aSession, Access::VIEW_FILES_VIEW) {

		ViewFileManager::getInstance()->addListener(this);

		createSubscription("view_file_added");
		createSubscription("view_file_removed");
		createSubscription("view_file_updated");
		createSubscription("view_file_finished");

		METHOD_HANDLER("sessions", Access::VIEW_FILES_VIEW, ApiRequest::METHOD_GET, (), false, ViewFileApi::handleGetFiles);
		METHOD_HANDLER("session", Access::VIEW_FILES_EDIT, ApiRequest::METHOD_POST, (), true, ViewFileApi::handleAddFile);
		METHOD_HANDLER("session", Access::VIEW_FILES_EDIT, ApiRequest::METHOD_DELETE, (TTH_PARAM), false, ViewFileApi::handleRemoveFile);

		METHOD_HANDLER("session", Access::VIEW_FILES_VIEW, ApiRequest::METHOD_GET, (TTH_PARAM, EXACT_PARAM("text")), false, ViewFileApi::handleGetText);
	}

	ViewFileApi::~ViewFileApi() {
		ViewFileManager::getInstance()->removeListener(this);
	}

	json ViewFileApi::serializeFile(const ViewFilePtr& aFile) noexcept {
		return{
			{ "id", aFile->getTTH().toBase32() },
			{ "tth", aFile->getTTH().toBase32() },
			//{ "path", aFile->getPath() },
			{ "text", aFile->isText() },
			{ "name", aFile->getDisplayName() },
			{ "state", Serializer::serializeDownloadState(aFile->getDownloadState()) },
			{ "type", Serializer::serializeFileType(aFile->getPath()) },
			{ "time_finished", aFile->getTimeFinished() },
		};
	}

	api_return ViewFileApi::handleGetFiles(ApiRequest& aRequest) {
		auto ret = json::array();

		auto files = ViewFileManager::getInstance()->getFiles();
		for (const auto& file : files | map_values) {
			ret.push_back(serializeFile(file));
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	api_return ViewFileApi::handleAddFile(ApiRequest& aRequest) {
		const auto& j = aRequest.getRequestBody();
		auto tth = Deserializer::deserializeTTH(j);

		if (ViewFileManager::getInstance()->getFile(tth)) {
			aRequest.setResponseErrorStr("File with the same TTH is open already");
			return websocketpp::http::status_code::bad_request;
		}

		auto name = JsonUtil::getField<string>("name", j, false);
		auto size = JsonUtil::getField<int64_t>("size", j);
		auto user = Deserializer::deserializeHintedUser(j);
		auto isText = JsonUtil::getOptionalFieldDefault<bool>("text", j, false);

		try {
			QueueManager::getInstance()->addOpenedItem(name, size, tth, user, true, isText);
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::internal_server_error;
		}

		return websocketpp::http::status_code::ok;
	}

	api_return ViewFileApi::handleGetText(ApiRequest& aRequest) {
		auto file = ViewFileManager::getInstance()->getFile(Deserializer::parseTTH(aRequest.getStringParam(0)));
		if (!file) {
			aRequest.setResponseErrorStr("File not found");
			return websocketpp::http::status_code::not_found;
		}

		if (!file->isText()) {
			aRequest.setResponseErrorStr("This method can't be used for non-text files");
			return websocketpp::http::status_code::bad_request;
		}

		string content;
		try {
			File f(file->getPath(), File::READ, File::OPEN);

			content = f.read();
			if (Util::getFileExt(file->getPath()) == ".nfo") {
				content = Text::toUtf8(content, "cp437");
			}
		} catch (const FileException& e) {
			aRequest.setResponseErrorStr("Failed to open the file: " + e.getError() + "(" + file->getPath() + ")");
			return websocketpp::http::status_code::internal_server_error;
		}

		aRequest.setResponseBody({
			{ "text", content },
		});
		return websocketpp::http::status_code::ok;
	}

	api_return ViewFileApi::handleRemoveFile(ApiRequest& aRequest) {
		auto success = ViewFileManager::getInstance()->removeFile(Deserializer::parseTTH(aRequest.getStringParam(0)));
		if (!success) {
			aRequest.setResponseErrorStr("File not found");
			return websocketpp::http::status_code::not_found;
		}

		return websocketpp::http::status_code::ok;
	}

	void ViewFileApi::on(ViewFileManagerListener::FileAdded, const ViewFilePtr& aFile) noexcept {
		maybeSend("view_file_added", [&] { return serializeFile(aFile); });
	}

	void ViewFileApi::on(ViewFileManagerListener::FileClosed, const ViewFilePtr& aFile) noexcept {
		maybeSend("view_file_removed", [&] { 
			return json({ "id", aFile->getTTH().toBase32() });
		});
	}

	void ViewFileApi::on(ViewFileManagerListener::FileUpdated, const ViewFilePtr& aFile) noexcept {
		onViewFileUpdated(aFile);
	}

	void ViewFileApi::on(ViewFileManagerListener::FileFinished, const ViewFilePtr& aFile) noexcept {
		onViewFileUpdated(aFile);

		maybeSend("view_file_finished", [&] { return serializeFile(aFile); });
	}

	void ViewFileApi::onViewFileUpdated(const ViewFilePtr& aFile) noexcept {
		maybeSend("view_file_updated", [&] { return serializeFile(aFile); });
	}
}