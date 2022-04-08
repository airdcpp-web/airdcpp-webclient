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

#ifndef DCPLUSPLUS_WEBSERVER_SETTINGS_H
#define DCPLUSPLUS_WEBSERVER_SETTINGS_H

#include "forward.h"

#include <web-server/ApiSettingItem.h>
#include <web-server/WebServerManagerListener.h>

#include <airdcpp/SettingsManager.h>

namespace dcpp {
	class SimpleXML;
}

namespace webserver {
	class WebServerSettings : private WebServerManagerListener {
	public:
#ifdef _WIN32
		static const string localNodeDirectoryName;
#endif

		WebServerSettings(WebServerManager* aServer);
		~WebServerSettings();

		enum ServerSettings {
			PLAIN_PORT,
			PLAIN_BIND,

			TLS_PORT,
			TLS_BIND,

			TLS_CERT_PATH,
			TLS_CERT_KEY_PATH,

			SERVER_THREADS,
			EXTENSION_ENGINES,

			DEFAULT_SESSION_IDLE_TIMEOUT,
			PING_INTERVAL,
			PING_TIMEOUT,

			EXTENSIONS_DEBUG_MODE,
			EXTENSIONS_INIT_TIMEOUT,
			EXTENSIONS_AUTO_UPDATE,

			SHARE_FILE_VALIDATION_HOOK_TIMEOUT,
			SHARE_DIRECTORY_VALIDATION_HOOK_TIMEOUT,
			NEW_SHARE_FILE_VALIDATION_HOOK_TIMEOUT,
			NEW_SHARE_DIRECTORY_VALIDATION_HOOK_TIMEOUT,

			OUTGOING_CHAT_MESSAGE_HOOK_TIMEOUT,
			INCOMING_CHAT_MESSAGE_HOOK_TIMEOUT,

			QUEUE_ADD_BUNDLE_FILE_HOOK_TIMEOUT,
			QUEUE_ADD_BUNDLE_HOOK_TIMEOUT,
			QUEUE_ADD_SOURCE_HOOK_TIMEOUT,
			QUEUE_FILE_FINISHED_HOOK_TIMEOUT,
			QUEUE_BUNDLE_FINISHED_HOOK_TIMEOUT,

			LIST_MENUITEMS_HOOK_TIMEOUT,
		};

		ServerSettingItem& getSettingItem(ServerSettings aSetting) noexcept {
			return settings[aSetting];
		}

		ServerSettingItem* getSettingItem(const string& aKey) noexcept {
			return ApiSettingItem::findSettingItem<ServerSettingItem>(settings, aKey);
		}

		typedef std::function<void(const json&, int /*aConfigVersion*/)> JsonParseCallback;
		static bool loadSettingFile(Util::Paths aPath, const string& aFileName, JsonParseCallback&& aParseCallback, const MessageCallback& aCustomErrorF, int aMaxConfigVersion) noexcept;
		static bool saveSettingFile(const json& aJson, Util::Paths aPath, const string& aFileName, const MessageCallback& aCustomErrorF, int aConfigVersion) noexcept;

		json toJson() const noexcept;
		void fromJsonThrow(const json& aJson, int aVersion);

		string getConfigFilePath() const noexcept;

		void setValue(ApiSettingItem& aItem, const json& aJson);
		void setDefaultValue(ApiSettingItem& aItem, const json& aJson);
		void unset(ApiSettingItem& aItem) noexcept;

		WebServerSettings(WebServerSettings&) = delete;
		WebServerSettings& operator=(WebServerSettings&) = delete;
	private:
		WebServerManager* wsm;

		ServerSettingItem::List settings;
		ServerSettingItem::List extensionEngines;

		json getDefaultExtensionEngines() noexcept;

		bool isDirty = false;

		void setDirty() noexcept;

		void on(WebServerManagerListener::LoadLegacySettings, SimpleXML& aXml) noexcept override;
		void on(WebServerManagerListener::LoadSettings, const MessageCallback& aErrorF) noexcept override;
		void on(WebServerManagerListener::SaveSettings, const MessageCallback& aErrorF) noexcept override;

		bool loadLegacySettings(const MessageCallback& aErrorF) noexcept;
		void loadLegacyServer(SimpleXML& aXml, const string& aTagName, ServerSettingItem& aPort, ServerSettingItem& aBindAddress, bool aTls) noexcept;
	};

#define WEBCFG(k) (webserver::WebServerManager::getInstance()->getSettingsManager().getSettingItem(webserver::WebServerSettings::k))
}

#endif