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

#ifndef DCPLUSPLUS_WEBSERVER_EXTENSION_H
#define DCPLUSPLUS_WEBSERVER_EXTENSION_H

#include "forward.h"

#include <web-server/ExtensionListener.h>
#include <web-server/ApiSettingItem.h>

#include <airdcpp/GetSet.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/User.h>
#include <airdcpp/Util.h>

namespace webserver {
#define EXTENSION_DIR_ROOT AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "extensions" + PATH_SEPARATOR_STR

	class Extension : public Speaker<ExtensionListener> {
	public:
		typedef std::function<void(const Extension*, uint32_t /*exitCode*/)> ErrorF;

		// Managed extension
		// Throws on errors
		Extension(const string& aPackageDirectory, ErrorF&& aErrorF, bool aSkipPathValidation = false);

		// Unmanaged extension
		// Throws on errors
		Extension(const SessionPtr& aSession, const json& aPackageJson);

		~Extension();

		// Reload package.json from the supplied path
		// Throws on errors
		void reloadThrow();

		// Throws on errors
		void startThrow(const string& aEngine, WebServerManager* wsm, const StringList& aExtraArgs);

		// Stop the extension and wait until it's not running anymore
		// Returns false if the process couldn't be stopped
		void stopThrow();

		// Check that the extension is compatible with the current API
		// Throws on errors
		void checkCompatibilityThrow();

#define EXT_ENGINE_NODE "node"

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
		IGETSET(bool, signalReady, SignalReady, false);
		IGETSET(bool, ready, Ready, false);
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
		ExtensionSettingItem::List getSettings() const noexcept;
		ExtensionSettingItem* getSetting(const string& aKey) noexcept;
		void resetSettings() noexcept;
		void resetSession() noexcept;

		typedef map<string, json> SettingValueMap;

		// Values and keys should have been validated earlier
		void setValidatedSettingValues(const SettingValueMap& aValues, const UserList& aUserReferences) noexcept;
		SettingValueMap getSettingValues() noexcept;

		void swapSettingDefinitions(ExtensionSettingItem::List& aDefinitions) noexcept;

		FilesystemItemList getLogs() const noexcept;

		Extension(Extension&) = delete;
		Extension& operator=(Extension&) = delete;
	private:
		int apiVersion = 0;
		int minApiFeatureLevel = 0;

		// Reload package.json from the supplied path
		// Throws on errors
		void initializeThrow(const string& aPackageDirectory, bool aSkipPathValidation);

		static SharedMutex cs;
		ExtensionSettingItem::List settings;

		// Keep references to all users in settings to avoid them from being deleted
		unordered_set<UserPtr, User::Hash> userReferences;

		// Load package JSON
		// Throws on errors
		void initializeThrow(const json& aJson);

		// Parse airdcpp-specific package.json fields
		void parseApiDataThrow(const json& aJson);

		const bool managed;
		bool privateExtension = false;

		// Get the arguments for launching the extension
		// The escape option should be used only when the args can't be passed separately (the extension is launched with one string command)
		StringList getLaunchParams(WebServerManager* wsm, const SessionPtr& aSession, bool aEscape, const StringList& aExtraArgs) const noexcept;
		static string getConnectUrl(WebServerManager* wsm) noexcept;

		bool running = false;

		// Throws on errors
		void createProcessThrow(const string& aEngine, WebServerManager* wsm, const SessionPtr& aSession, const StringList& aExtraArgs);

		const ErrorF errorF;
		SessionPtr session = nullptr;

		void checkRunningState(WebServerManager* wsm) noexcept;
		void onFailed(uint32_t aExitCode) noexcept;
		void onStopped(bool aFailed) noexcept;
		TimerPtr timer = nullptr;

		void terminateProcessThrow();
		void resetProcessState() noexcept;

		static void rotateLog(const string& aPath);
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
		static int getAppPid() noexcept;
	};

	inline bool operator==(const ExtensionPtr& a, const string& b) noexcept { return Util::stricmp(a->getName(), b) == 0; }
}

#endif