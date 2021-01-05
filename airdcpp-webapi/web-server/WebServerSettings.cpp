/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#include <web-server/WebServerSettings.h>
#include <api/common/SettingUtils.h>

#include <airdcpp/File.h>
#include <airdcpp/TimerManager.h>

namespace webserver {

#ifdef _WIN32
	const string WebServerSettings::localNodeDirectoryName = "Node.js";
#endif
	json WebServerSettings::getDefaultExtensionEngines() noexcept {
		return {
			{
				{ "name", "node" },
#ifdef _WIN32
				{ "command", "./" + localNodeDirectoryName + "/node.exe;node" },
#else
				{ "command", "nodejs;node" },
#endif
				{ "arguments", json::array() },
			},
			{
				{ "name", "python3" },
				{ "command", "python3;python" },
				{ "arguments", json::array() },
			}
		};
	}

	WebServerSettings::WebServerSettings(): 
		settings({
			{ "web_plain_port",			ResourceManager::WEB_CFG_PORT,			5600,	ApiSettingItem::TYPE_NUMBER, false, { 0, 65535 } },
			{ "web_plain_bind_address", ResourceManager::WEB_CFG_BIND_ADDRESS,	"",		ApiSettingItem::TYPE_STRING, true },

			{ "web_tls_port",			ResourceManager::WEB_CFG_PORT,			5601,	ApiSettingItem::TYPE_NUMBER, false, { 0, 65535 } },
			{ "web_tls_bind_address",	ResourceManager::WEB_CFG_BIND_ADDRESS,	"",		ApiSettingItem::TYPE_STRING, true },

			{ "web_tls_certificate_path",		ResourceManager::WEB_CFG_CERT_PATH,		"", ApiSettingItem::TYPE_EXISTING_FILE_PATH, true },
			{ "web_tls_certificate_key_path",	ResourceManager::WEB_CFG_CERT_KEY_PATH, "", ApiSettingItem::TYPE_EXISTING_FILE_PATH, true },

			{ "web_server_threads",			ResourceManager::WEB_CFG_SERVER_THREADS,	4,								ApiSettingItem::TYPE_NUMBER,	false, { 1, 100 } },
			{ "extension_engines",			ResourceManager::WEB_CFG_EXTENSION_ENGINES,	getDefaultExtensionEngines(),	ApiSettingItem::TYPE_LIST,		false, {}, ResourceManager::LAST, ApiSettingItem::TYPE_STRUCT, extensionEngines },

			{ "default_idle_timeout",		ResourceManager::WEB_CFG_IDLE_TIMEOUT,				20,		ApiSettingItem::TYPE_NUMBER,	false, { 0, MAX_INT_VALUE, ResourceManager::MINUTES_LOWER }, },
			{ "ping_interval",				ResourceManager::WEB_CFG_PING_INTERVAL,				30,		ApiSettingItem::TYPE_NUMBER,	false, { 1, 10000, ResourceManager::SECONDS_LOWER },		 },
			{ "ping_timeout",				ResourceManager::WEB_CFG_PING_TIMEOUT,				10,		ApiSettingItem::TYPE_NUMBER,	false, { 1, 10000, ResourceManager::SECONDS_LOWER },		 },

			{ "extensions_debug_mode",		ResourceManager::WEB_CFG_EXTENSIONS_DEBUG_MODE,		false,	ApiSettingItem::TYPE_BOOLEAN,	false },
			{ "extensions_init_timeout",	ResourceManager::WEB_CFG_EXTENSIONS_INIT_TIMEOUT,	5,		ApiSettingItem::TYPE_NUMBER,	false, { 1, 60, ResourceManager::SECONDS_LOWER } },
			{ "extensions_auto_update",		ResourceManager::WEB_CFG_EXTENSIONS_AUTO_UPDATE,	true,	ApiSettingItem::TYPE_BOOLEAN,	false },

			{ "share_file_validation_hook_timeout",			ResourceManager::WEB_CFG_SHARE_FILE_VALIDATION_HOOK_TIMEOUT,			30, ApiSettingItem::TYPE_NUMBER, false, { 1, 300,	ResourceManager::SECONDS_LOWER } },
			{ "share_directory_validation_hook_timeout",	ResourceManager::WEB_CFG_SHARE_DIRECTORY_VALIDATION_HOOK_TIMEOUT,		30, ApiSettingItem::TYPE_NUMBER, false, { 1, 300,	ResourceManager::SECONDS_LOWER } },
			{ "new_share_file_validation_hook_timeout",		ResourceManager::WEB_CFG_NEW_SHARE_FILE_VALIDATION_HOOK_TIMEOUT,		60, ApiSettingItem::TYPE_NUMBER, false, { 1, 3600,	ResourceManager::SECONDS_LOWER } },
			{ "new_share_directory_validation_hook_timeout", ResourceManager::WEB_CFG_NEW_SHARE_DIRECTORY_VALIDATION_HOOK_TIMEOUT,	60, ApiSettingItem::TYPE_NUMBER, false, { 1, 3600,	ResourceManager::SECONDS_LOWER } },

			{ "outgoing_chat_message_hook_timeout",			ResourceManager::WEB_CFG_OUTGOING_CHAT_MESSAGE_HOOK_TIMEOUT,			2, ApiSettingItem::TYPE_NUMBER, false,	{ 1, 60,	ResourceManager::SECONDS_LOWER } },
			{ "incoming_chat_message_hook_timeout",			ResourceManager::WEB_CFG_INCOMING_CHAT_MESSAGE_HOOK_TIMEOUT,			2, ApiSettingItem::TYPE_NUMBER, false,	{ 1, 60,	ResourceManager::SECONDS_LOWER } },

			{ "queue_add_bundle_file_hook_timeout",			ResourceManager::WEB_CFG_QUEUE_ADD_BUNDLE_FILE_HOOK_TIMEOUT,			5,	ApiSettingItem::TYPE_NUMBER, false, { 1, 300,	ResourceManager::SECONDS_LOWER } },
			{ "queue_add_bundle_hook_timeout",				ResourceManager::WEB_CFG_QUEUE_ADD_BUNDLE_HOOK_TIMEOUT,					10, ApiSettingItem::TYPE_NUMBER, false, { 1, 600,	ResourceManager::SECONDS_LOWER } },
			{ "queue_add_source_hook_timeout",				ResourceManager::WEB_CFG_QUEUE_ADD_SOURCE_HOOK_TIMEOUT,					5,	ApiSettingItem::TYPE_NUMBER, false, { 1, 60,	ResourceManager::SECONDS_LOWER } },
			{ "queue_file_finished_hook_timeout",			ResourceManager::WEB_CFG_QUEUE_FILE_FINISHED_HOOK_TIMEOUT,				60, ApiSettingItem::TYPE_NUMBER, false, { 1, 3600,	ResourceManager::SECONDS_LOWER } },
			{ "queue_bundle_finished_hook_timeout",			ResourceManager::WEB_CFG_QUEUE_BUNDLE_FINISHED_HOOK_TIMEOUT,			120,ApiSettingItem::TYPE_NUMBER, false, { 1, 3600,	ResourceManager::SECONDS_LOWER } },

			{ "list_menuitems_hook_timeout",				ResourceManager::WEB_CFG_LIST_MENUITEMS_HOOK_TIMEOUT,					1,	ApiSettingItem::TYPE_NUMBER, false, { 1, 60,	ResourceManager::SECONDS_LOWER } },
		}),
		extensionEngines({
			{ "name",					ResourceManager::NAME,				"",				ApiSettingItem::TYPE_STRING,	false },
			{ "command",				ResourceManager::COMMAND,			"",				ApiSettingItem::TYPE_STRING,	false,	{}, ResourceManager::WEB_CFG_EXTENSION_ENGINES_COMMAND_HELP },
			{ "arguments",				ResourceManager::SETTINGS_ARGUMENT,	json::array(),	ApiSettingItem::TYPE_LIST,		true,	{}, ResourceManager::WEB_CFG_EXTENSION_ENGINES_ARGS_HELP, ApiSettingItem::TYPE_STRING },
		}) {}


	bool WebServerSettings::loadSettingFile(Util::Paths aPath, const string& aFileName, JsonParseCallback&& aParseCallback, const MessageCallback& aCustomErrorF, int aMaxConfigVersion) noexcept {
		const auto parseJsonFile = [&](const string& aPath) {
			try {
				// Parse
				auto parsed = json::parse(File(aPath, File::READ, File::OPEN).read());

				// Check version
				int configVersion = parsed.at("version");
				if (configVersion > aMaxConfigVersion) {
					throw std::invalid_argument("Config version " + Util::toString(configVersion) + " is not supported");
				}

				// Parse settings
				aParseCallback(parsed.at("settings"), configVersion);
			} catch (const std::exception& e) {
				aCustomErrorF(STRING_F(LOAD_FAILED_X, aPath % e.what()));
				return false;
			}

			return true;
		};

		return SettingsManager::loadSettingFile(aPath, aFileName, parseJsonFile, aCustomErrorF);
	}

	bool WebServerSettings::saveSettingFile(const json& aJson, Util::Paths aPath, const string& aFileName, const MessageCallback& aCustomErrorF, int aConfigVersion) noexcept {
		auto data = json({
			{ "version", aConfigVersion },
			{ "settings", aJson },
		});

		return SettingsManager::saveSettingFile(data.dump(2), aPath, aFileName, aCustomErrorF);
	}

	json WebServerSettings::toJson() const noexcept {
		json ret;
		for (const auto s: settings) {
			if (!s.isDefault()) {
				ret[s.name] = s.getValue();
			}
		}

		return ret;
	}

	void WebServerSettings::fromJsonThrow(const json& aJson) {
		for (const auto& elem: aJson.items()) {
			auto setting = getSettingItem(elem.key());
			if (!setting) {
				dcdebug("Web server settings: loaded key %s was not found, skipping\n", elem.key().c_str());
				continue;
			}

			try {
				setting->setValue(SettingUtils::validateValue(elem.value(), *setting, nullptr));
			} catch (const ArgumentException& e) {
				dcdebug("Web server settings: validation failed for setting %s (%s)\n", elem.key().c_str(), e.what());
				// ...
			}
		}
	}
}
