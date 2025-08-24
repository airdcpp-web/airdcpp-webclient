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

#include <api/ExtensionInfo.h>
#include <api/common/Serializer.h>
#include <api/common/SettingUtils.h>

#include <web-server/JsonUtil.h>
#include <web-server/Extension.h>
#include <web-server/ExtensionManager.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/core/io/File.h>


namespace webserver {
	const StringList ExtensionInfo::subscriptionList = {
		"extension_started",
		"extension_stopped",
		"extension_updated",
		"extension_settings_updated",
		"extension_package_updated",
	};

	ExtensionInfo::ExtensionInfo(ParentType* aParentModule, const ExtensionPtr& aExtension) : 
		SubApiModule(aParentModule, aExtension->getName()),
		extension(aExtension) 
	{
		createSubscriptions(subscriptionList);

		METHOD_HANDLER(Access::ADMIN, METHOD_PATCH, (), ExtensionInfo::handleUpdateProperties);
		METHOD_HANDLER(Access::ADMIN, METHOD_POST, (EXACT_PARAM("start")), ExtensionInfo::handleStartExtension);
		METHOD_HANDLER(Access::ADMIN, METHOD_POST, (EXACT_PARAM("stop")), ExtensionInfo::handleStopExtension);
		METHOD_HANDLER(Access::ANY, METHOD_POST, (EXACT_PARAM("ready")), ExtensionInfo::handleReady);

		METHOD_HANDLER(Access::SETTINGS_VIEW, METHOD_GET, (EXACT_PARAM("settings"), EXACT_PARAM("definitions")), ExtensionInfo::handleGetSettingDefinitions);
		METHOD_HANDLER(Access::SETTINGS_EDIT, METHOD_POST, (EXACT_PARAM("settings"), EXACT_PARAM("definitions")), ExtensionInfo::handlePostSettingDefinitions);

		METHOD_HANDLER(Access::SETTINGS_VIEW, METHOD_GET, (EXACT_PARAM("settings")), ExtensionInfo::handleGetSettings);
		METHOD_HANDLER(Access::SETTINGS_EDIT, METHOD_PATCH, (EXACT_PARAM("settings")), ExtensionInfo::handlePostSettings);
	}

	void ExtensionInfo::init() noexcept {
		extension->addListener(this);
	}

	string ExtensionInfo::getId() const noexcept {
		return extension->getName();
	}

	ExtensionInfo::~ExtensionInfo() {
		extension->removeListener(this);
	}

	api_return ExtensionInfo::handleUpdateProperties(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto disabled = JsonUtil::getOptionalField<bool>("disabled", reqJson, false);
		if (disabled) {
			extension->setDisabled(*disabled);
		}

		return http_status::no_content;
	}

	api_return ExtensionInfo::handleStartExtension(ApiRequest& aRequest) {
		try {
			auto server = aRequest.getSession()->getServer();
			auto installedEngines = server->getExtensionManager().getEngines();
			auto launchInfo = server->getExtensionManager().getStartCommandThrow(extension->getEngines(), installedEngines);
			extension->startThrow(launchInfo.command, server, launchInfo.arguments);
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.what());
			return http_status::internal_server_error;
		}

		return http_status::no_content;
	}

	api_return ExtensionInfo::handleStopExtension(ApiRequest& aRequest) {
		try {
			extension->stopThrow();
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.what());
			return http_status::internal_server_error;
		}

		return http_status::no_content;
	}

	api_return ExtensionInfo::handleReady(ApiRequest& aRequest) {
		extension->setReady(true);
		return http_status::no_content;
	}

	api_return ExtensionInfo::handleGetSettings(ApiRequest& aRequest) {
		aRequest.setResponseBody(extension->getSettingValues());
		return http_status::ok;
	}

	api_return ExtensionInfo::handleGetSettingDefinitions(ApiRequest& aRequest) {
		aRequest.setResponseBody(Serializer::serializeList(extension->getSettings(), SettingUtils::serializeDefinition));
		return http_status::ok;
	}

	api_return ExtensionInfo::handlePostSettingDefinitions(ApiRequest& aRequest) {
		if (extension->hasSettings()) {
			aRequest.setResponseErrorStr("Setting definitions exist for this extensions already");
			return http_status::conflict;
		}

		if (extension->getSession() != aRequest.getSession()) {
			aRequest.setResponseErrorStr("Setting definitions may only be posted by the owning session");
			return http_status::conflict;
		}

		auto defs = SettingUtils::deserializeDefinitions(aRequest.getRequestBody());
		extension->swapSettingDefinitions(defs);
		return http_status::no_content;
	}

	api_return ExtensionInfo::handlePostSettings(ApiRequest& aRequest) {
		SettingValueMap settings;
		SettingReferenceList userReferences;

		// Validate values
		for (const auto& elem : aRequest.getRequestBody().items()) {
			auto setting = extension->getSetting(elem.key());
			if (!setting) {
				JsonUtil::throwError(elem.key(), JsonException::ERROR_INVALID, "Setting not found");
			}

			settings[elem.key()] = SettingUtils::validateValue(elem.value(), *setting, &userReferences);
		}

		// Update
		extension->setValidatedSettingValues(settings, userReferences);
		return http_status::no_content;
	}

	json ExtensionInfo::serializeExtension(const ExtensionPtr& aExtension) noexcept {
		return {
			{ "id", aExtension->getName() },
			{ "name", aExtension->getName() },
			{ "description", aExtension->getDescription() },
			{ "version", aExtension->getVersion() },
			{ "homepage", aExtension->getHomepage() },
			{ "author", aExtension->getAuthor() },
			{ "disabled", aExtension->isDisabled() },
			{ "running", aExtension->isRunning() },
			{ "private", aExtension->isPrivate() },
			{ "logs", ExtensionInfo::serializeLogs(aExtension) },
			{ "engines", aExtension->getEngines() },
			{ "managed", aExtension->isManaged() },
			{ "has_settings", aExtension->hasSettings() },
		};
	}

	void ExtensionInfo::on(ExtensionListener::SettingValuesUpdated, const Extension*, const SettingValueMap& aUpdatedSettings) noexcept {
		maybeSend("extension_settings_updated", [&aUpdatedSettings] {
			return aUpdatedSettings;
		});
	}

	void ExtensionInfo::on(ExtensionListener::SettingDefinitionsUpdated, const Extension*) noexcept {
		onUpdated([&] {
			return json({
				{ "has_settings", extension->hasSettings() }
			});
		});
	}

	json ExtensionInfo::serializeLogs(const ExtensionPtr& aExtension) noexcept {
		return Serializer::serializeList(aExtension->getLogs(), Serializer::serializeFilesystemItem);
	}

	void ExtensionInfo::on(ExtensionListener::StateUpdated, const Extension*) noexcept {
		onUpdated([&] {
			return json({
				{ "disabled", extension->isDisabled() }
			});
		});
	}

	void ExtensionInfo::on(ExtensionListener::ExtensionStarted, const Extension*) noexcept {
		onUpdated([&] {
			return json({
				{ "running", extension->isRunning() }
			});
		});

		maybeSend("extension_started", [&] {
			return serializeExtension(extension);
		});
	}

	void ExtensionInfo::on(ExtensionListener::ExtensionStopped, const Extension*, bool) noexcept {
		onUpdated([&] {
			return json({
				{ "running", extension->isRunning() }
			});
		});

		maybeSend("extension_stopped", [&] {
			return serializeExtension(extension);
		});
	}

	void ExtensionInfo::on(ExtensionListener::PackageUpdated, const Extension*) noexcept {
		onUpdated([&] {
			return serializeExtension(extension);
		});

		maybeSend("extension_package_updated", [&] {
			return serializeExtension(extension);
		});
	}

	void ExtensionInfo::onUpdated(const JsonCallback& aDataCallback) noexcept {
		maybeSend("extension_updated", aDataCallback);
	}
}