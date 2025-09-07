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

#include <api/ExtensionApi.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>
#include <web-server/Extension.h>
#include <web-server/ExtensionManager.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/core/io/File.h>


#define EXTENSION_PARAM_ID "extension"
#define EXTENSION_PARAM (ApiModule::RequestHandler::Param(EXTENSION_PARAM_ID, regex(R"(^airdcpp-.+$)")))
namespace webserver {
	StringList ExtensionApi::subscriptionList = {
		"extension_added",
		"extension_removed",
		"extension_installation_started",
		"extension_installation_succeeded",
		"extension_installation_failed",
	};

	ExtensionApi::ExtensionApi(Session* aSession) : 
		ParentApiModule(EXTENSION_PARAM, Access::SETTINGS_VIEW, aSession,
			[](const string& aId) { return aId; },
			[](const ExtensionInfo& aInfo) { return ExtensionInfo::serializeExtension(aInfo.getExtension()); }
		),
		em(aSession->getServer()->getExtensionManager())
	{
		em.addListener(this);

		createSubscriptions(subscriptionList, ExtensionInfo::subscriptionList);

		METHOD_HANDLER(Access::ADMIN, METHOD_POST, (), ExtensionApi::handlePostExtension);
		METHOD_HANDLER(Access::ADMIN, METHOD_POST, (EXACT_PARAM("download")), ExtensionApi::handleDownloadExtension);

		METHOD_HANDLER(Access::SETTINGS_VIEW, METHOD_GET, (EXACT_PARAM("engines"), EXACT_PARAM("status")), ExtensionApi::handleGetEngineStatuses);
		// TODO
		//METHOD_HANDLER(Access::ADMIN, METHOD_PUT, (EXACT_PARAM("engines")), ExtensionApi::handlePutEngines);
		//METHOD_HANDLER(Access::SETTINGS_VIEW, METHOD_GET, (EXACT_PARAM("engines")), ExtensionApi::handleGetEngines);

		for (const auto& ext: em.getExtensions()) {
			addExtension(ext);
		}
	}

	ExtensionApi::~ExtensionApi() {
		em.removeListener(this);
	}

	void ExtensionApi::addExtension(const ExtensionPtr& aExtension) noexcept {
		addSubModule(aExtension->getName(), std::make_shared<ExtensionInfo>(this, aExtension));
	}

	api_return ExtensionApi::handlePostExtension(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		try {
			auto ext = em.registerRemoteExtensionThrow(aRequest.getSession(), reqJson);
			aRequest.setResponseBody(ExtensionInfo::serializeExtension(ext));
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return http_status::bad_request;
		}

		return http_status::ok;
	}

	api_return ExtensionApi::handleDownloadExtension(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto installId = JsonUtil::getField<string>("install_id", reqJson, false);
		auto url = JsonUtil::getField<string>("url", reqJson, false);
		auto sha = JsonUtil::getOptionalFieldDefault<string>("shasum", reqJson, Util::emptyString);

		if (Util::findSubString(url, "http://") != 0 && Util::findSubString(url, "https://") != 0) {
			JsonUtil::throwError("url", JsonException::ERROR_INVALID, "Invalid URL");
		}

		if (!em.downloadExtension(installId, url, sha)) {
			aRequest.setResponseErrorStr("Extension is being download already");
			return http_status::conflict;
		}

		return http_status::no_content;
	}

	api_return ExtensionApi::handleDeleteSubmodule(ApiRequest& aRequest) {
		auto extensionInfo = getSubModule(aRequest);
		try {
			if (extensionInfo->getExtension()->isManaged()) {
				em.uninstallLocalExtensionThrow(extensionInfo->getExtension());
			} else {
				em.unregisterRemoteExtension(extensionInfo->getExtension());
			}
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return http_status::internal_server_error;
		}

		return http_status::no_content;
	}

	api_return ExtensionApi::handleGetEngineStatuses(ApiRequest& aRequest) {
		auto ret = json::object();
		for (const auto& e: em.getEngines()) {
			auto command = ExtensionManager::selectEngineCommand(e.name);
			if (!command.empty()) {
				ret[e.name] = command;
			} else {
				ret[e.name] = nullptr;
			}
		}

		aRequest.setResponseBody(ret);
		return http_status::ok;
	}

	void ExtensionApi::on(ExtensionManagerListener::ExtensionAdded, const ExtensionPtr& aExtension) noexcept {
		addExtension(aExtension);

		maybeSend("extension_added", [&] {
			return ExtensionInfo::serializeExtension(aExtension);
		});
	}

	void ExtensionApi::on(ExtensionManagerListener::ExtensionRemoved, const ExtensionPtr& aExtension) noexcept {
		removeSubModule(aExtension->getName());
		maybeSend("extension_removed", [&] {
			return ExtensionInfo::serializeExtension(aExtension);
		});
	}

	void ExtensionApi::on(ExtensionManagerListener::InstallationStarted, const string& aInstallId) noexcept {
		maybeSend("extension_installation_started", [&] {
			return json({
				{ "install_id", aInstallId },
			});
		});
	}

	void ExtensionApi::on(ExtensionManagerListener::InstallationSucceeded, const string& aInstallId, const ExtensionPtr&, bool) noexcept {
		maybeSend("extension_installation_succeeded", [&] {
			return json({
				{ "install_id", aInstallId },
			});
		});
	}

	void ExtensionApi::on(ExtensionManagerListener::InstallationFailed, const string& aInstallId, const string& aError) noexcept {
		maybeSend("extension_installation_failed", [&] {
			return json({
				{ "install_id", aInstallId },
				{ "error", aError },
			});
		});
	}
}