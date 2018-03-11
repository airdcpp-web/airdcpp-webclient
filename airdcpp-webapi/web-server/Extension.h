/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_EXTENSION_H
#define DCPLUSPLUS_DCPP_EXTENSION_H

#include "stdinc.h"
#include <web-server/ExtensionListener.h>

#include <api/ApiSettingItem.h>

#include <airdcpp/GetSet.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/Util.h>

namespace webserver {
#define EXTENSION_DIR_ROOT Util::getPath(Util::PATH_USER_CONFIG) + "extensions" + PATH_SEPARATOR_STR

	class Extension : public Speaker<ExtensionListener> {
	public:
		typedef std::function<void(Extension*, uint32_t /*exitCode*/)> ErrorF;

		// Throws on errors
		Extension(const string& aPackageDirectory, ErrorF&& aErrorF, bool aSkipPathValidation = false);
		Extension(const SessionPtr& aSession, const json& aPackageJson);

		~Extension();

		// Reload package.json from the supplied path
		// Throws on errors
		void reload();

		// Throws on errors
		void start(const string& aEngine, WebServerManager* wsm);

		// Stop the extension and wait until it's not running anymore
		// Returns false if the process couldn't be stopped
		bool stop() noexcept;

		// Check that the extension is compatible with the current API
		// Throws on errors
		void checkCompatibility();

#define EXT_PACKAGE_DIR "package"
#define EXT_CONFIG_DIR "settings"
#define EXT_LOG_DIR "logs"

		static string getRootPath(const string& aName) noexcept;
		string getRootPath() const noexcept;
		string getMessageLogPath() const noexcept;
		string getErrorLogPath() const noexcept;

		bool isManaged() const noexcept {
			return managed;
		}

		GETSET(string, name, Name);
		GETSET(string, description, Description);
		GETSET(string, entry, Entry);
		GETSET(string, version, Version);
		GETSET(string, author, Author);
		GETSET(string, homepage, Homepage);
		GETSET(StringList, engines, Engines);

		bool isRunning() const noexcept {
			return running;
		}

		bool isPrivate() const noexcept {
			return privateExtension;
		}

		const SessionPtr& getSession() const noexcept {
			return session;
		}

		bool hasSettings() const noexcept;
		ServerSettingItem::List getSettings() const noexcept;
		ServerSettingItem* getSetting(const string& aKey) noexcept;
		void resetSettings() noexcept;

		typedef map<string, json> SettingValueMap;
		void setSettingValues(const SettingValueMap& aValues);
		SettingValueMap getSettingValues() noexcept;

		// Throws on errors
		void swapSettingDefinitions(ServerSettingItem::List& aDefinitions);

		FilesystemItemList getLogs() const noexcept;
	private:
		int apiVersion = 0;
		int minApiFeatureLevel = 0;

		// Reload package.json from the supplied path
		// Throws on errors
		void initialize(const string& aPackageDirectory, bool aSkipPathValidation);

		static SharedMutex cs;
		ServerSettingItem::List settings;

		// Load package JSON
		// Throws on errors
		void initialize(const json& aJson);

		// Parse airdcpp-specific package.json fields
		void parseApiData(const json& aJson);

		const bool managed;
		bool privateExtension = false;

		StringList getLaunchParams(WebServerManager* wsm, const SessionPtr& aSession) const noexcept;
		static string getConnectUrl(WebServerManager* wsm) noexcept;

		bool running = false;

		// Throws on errors
		void createProcess(const string& aEngine, WebServerManager* wsm, const SessionPtr& aSession);

		const ErrorF errorF;
		SessionPtr session = nullptr;

		void checkRunningState(WebServerManager* wsm) noexcept;
		void onFailed(uint32_t aExitCode) noexcept;
		void onStopped(bool aFailed) noexcept;
		TimerPtr timer = nullptr;

		bool terminateProcess() noexcept;
		void resetProcessState() noexcept;
#ifdef _WIN32
		static void initLog(HANDLE& aHandle, const string& aPath);
		static void disableLogInheritance(HANDLE& aHandle);
		static void closeLog(HANDLE& aHandle);

		PROCESS_INFORMATION piProcInfo;
		HANDLE messageLogHandle = INVALID_HANDLE_VALUE;
		HANDLE errorLogHandle = INVALID_HANDLE_VALUE;
#else
		static unique_ptr<File> initLog(const string& aPath);
		pid_t pid = 0;
#endif
	};

	inline bool operator==(const ExtensionPtr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }
}

#endif