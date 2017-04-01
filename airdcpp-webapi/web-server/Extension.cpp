/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#include <web-server/Extension.h>

#include <web-server/SystemUtil.h>
#include <web-server/WebUserManager.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/version.h>

#include <airdcpp/File.h>


namespace webserver {
	SharedMutex Extension::cs;

	Extension::Extension(const string& aPath, ErrorF&& aErrorF, bool aSkipPathValidation) : errorF(std::move(aErrorF)), managed(true) {
		initialize(aPath, aSkipPathValidation);
	}

	Extension::Extension(const SessionPtr& aSession, const json& aPackageJson) : managed(false), session(aSession) {
		initialize(aPackageJson);
	}

	void Extension::reload() {
		initialize(getRootPath(), false);

		fire(ExtensionListener::PackageUpdated());
	}

	void Extension::initialize(const string& aPath, bool aSkipPathValidation) {
		const auto packageStr = File(aPath + "package" + PATH_SEPARATOR_STR + "package.json", File::READ, File::OPEN).read();
		try {
			const json packageJson = json::parse(packageStr);

			initialize(packageJson);
		} catch (const std::exception& e) {
			throw Exception("Could not parse package.json (" + string(e.what()) + ")");
		}

		if (!aSkipPathValidation && compare(name, Util::getLastDir(aPath)) != 0) {
			throw Exception("Extension path doesn't match with the extension name " + name);
		}
	}

	void Extension::initialize(const json& aJson) {
		// Required fields
		const string packageName = aJson.at("name");
		const string packageDescription = aJson.at("description");
		const string packageEntry = aJson.at("main");
		const string packageVersion = aJson.at("version");

		{
			json packageAuthor = aJson.at("author");
			if (packageAuthor.is_string()) {
				author = packageAuthor.get<string>();
			} else {
				author = packageAuthor.at("name").get<string>();
			}
		}


		privateExtension = aJson.value("private", false);

		name = packageName;
		description = packageDescription;
		entry = packageEntry;
		version = packageVersion;

		// Optional fields
		homepage = aJson.value("homepage", Util::emptyString);

		{
			// Engines
			auto enginesJson = aJson.find("engines");
			if (enginesJson != aJson.end()) {
				for (const auto& engine : json::iterator_wrapper(*enginesJson)) {
					engines.emplace_back(engine.key());
				}
			}

			if (engines.empty()) {
				engines.emplace_back("node");
			}
		}

		{
			// Operating system
			auto osJson = aJson.find("os");
			if (osJson != aJson.end()) {
				const StringList osList = *osJson;
				auto currentOs = SystemUtil::getPlatform();
				if (std::find(osList.begin(), osList.end(), currentOs) == osList.end() && currentOs != "other") {
					throw Exception("Extension is not compatible with your operating system, please the extension documentation for more information");
				}
			}
		}

		parseApiData(aJson.at("airdcpp"));
	}

	void Extension::checkCompatibility() {
		if (apiVersion != API_VERSION) {
			throw Exception("Extension requires API version " + Util::toString(apiVersion) + " while the application uses version " + Util::toString(API_VERSION));
		}

		if (minApiFeatureLevel != API_FEATURE_LEVEL) {
			throw Exception("Extension requires API feature level " + Util::toString(minApiFeatureLevel) + " or newer while the application uses version " + Util::toString(API_FEATURE_LEVEL));
		}
	}

	void Extension::parseApiData(const json& aJson) {
		apiVersion = aJson.at("apiVersion");
		minApiFeatureLevel = aJson.value("minApiFeatureLevel", 0);
	}

	FilesystemItemList Extension::getLogs() const noexcept {
		FilesystemItemList ret;

		if (managed) {
			File::forEachFile(getLogPath(), "*.log", [&](const FilesystemItem& aInfo) {
				if (aInfo.isDirectory) {
					return;
				}

				ret.push_back(aInfo);
			});
		}

		return ret;
	}

	ServerSettingItem* Extension::getSetting(const string& aKey) noexcept {
		RLock l(cs);
		return ApiSettingItem::findSettingItem<ServerSettingItem>(settings, aKey);
	}

	bool Extension::hasSettings() const noexcept {
		RLock l(cs);
		return !settings.empty(); 
	}

	ServerSettingItem::List Extension::getSettings() const noexcept {
		RLock l(cs);
		return settings;
	}

	void Extension::swapSettingDefinitions(ServerSettingItem::List& aDefinitions) {
		{
			WLock l(cs);
			settings.swap(aDefinitions);
		}

		fire(ExtensionListener::SettingDefinitionsUpdated());
	}

	void Extension::resetSettings() noexcept {
		{
			WLock l(cs);
			settings.clear();
		}

		fire(ExtensionListener::SettingDefinitionsUpdated());
	}

	void Extension::setSettingValues(const SettingValueMap& aValues) {
		{
			WLock l(cs);
			for (const auto& vp: aValues) {
				auto setting = ApiSettingItem::findSettingItem<ServerSettingItem>(settings, vp.first);
				if (!setting) {
					throw Exception("Setting " + vp.first + " was not found");
				}

				setting->setValue(vp.second);
			}
		}

		fire(ExtensionListener::SettingValuesUpdated(), aValues);
	}

	Extension::SettingValueMap Extension::getSettingValues() noexcept {
		SettingValueMap values;

		{
			RLock l(cs);
			for (const auto& setting: settings) {
				values[setting.name] = setting.getValue();
			}
		}

		return values;
	}

	void Extension::start(const string& aEngine, WebServerManager* wsm) {
		if (!managed) {
			return;
		}

		if (isRunning()) {
			dcassert(0);
			return;
		}

		File::ensureDirectory(getLogPath());
		File::ensureDirectory(getSettingsPath());

		checkCompatibility();

		session = wsm->getUserManager().createExtensionSession(name);
		
		createProcess(aEngine, wsm, session);

		running = true;
		fire(ExtensionListener::ExtensionStarted());

		// Monitor the running state of the script
		timer = wsm->addTimer([this, wsm] { checkRunningState(wsm); }, 2500);
		timer->start(false);
	}

	string Extension::getConnectUrl(WebServerManager* wsm) noexcept {
		const auto& serverConfig = wsm->isListeningPlain() ? wsm->getPlainServerConfig() : wsm->getTlsServerConfig();

		auto bindAddress = serverConfig.bindAddress.str();
		if (bindAddress.empty()) {
			auto protocol = WebServerManager::getDefaultListenProtocol();
			bindAddress = protocol == boost::asio::ip::tcp::v6() ? "[::1]" : "127.0.0.1";
		}

		string address = wsm->isListeningPlain() ? "ws://" : "wss://";
		address += bindAddress;
		address += ":" + Util::toString(serverConfig.port.num()) + "/api/v1/ ";
		return address;
	}

	StringList Extension::getLaunchParams(WebServerManager* wsm, const SessionPtr& aSession) const noexcept {
		StringList ret;

		// Script to launch
		ret.push_back(getPackageDirectory() + entry);

		// Params
		auto addParam = [&ret](const string& aName, const string& aParam) {
			ret.push_back("--" + aName + "=" + aParam);
		};

		// Name
		addParam("name", name);

		// Connect URL
		addParam("apiUrl", getConnectUrl(wsm));

		// Session token
		addParam("authToken", aSession->getAuthToken());

		// Paths
		addParam("logPath", getLogPath());
		addParam("settingsPath", getSettingsPath());

		return ret;
	}

	bool Extension::stop() noexcept {
		if (!managed) {
			return false;
		}

		if (!isRunning()) {
			return true;
		}

		timer->stop(false);
		if (!terminateProcess()) {
			return false;
		}

		onStopped(false);
		return true;
	}

	void Extension::onStopped(bool aFailed) noexcept {
		fire(ExtensionListener::ExtensionStopped(), aFailed);
		if (aFailed) {
			timer->stop(false);
		}

		if (session) {
			session->getServer()->getUserManager().logout(session);
			session = nullptr;
		}

		resetProcessState();
		resetSettings();

		running = false;
		if (aFailed && errorF) {
			errorF(this);
		}
	}
#ifdef _WIN32
	void Extension::initLog(HANDLE& aHandle, const string& aPath) {
		dcassert(aHandle == INVALID_HANDLE_VALUE);

		SECURITY_ATTRIBUTES saAttr;
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
		saAttr.bInheritHandle = TRUE;
		saAttr.lpSecurityDescriptor = NULL;


		if (Util::fileExists(aPath) && !File::deleteFile(aPath)) {
			dcdebug("Failed to delete the old extension output log %s: %s\n", aPath.c_str(), Util::translateError(::GetLastError()).c_str());
			throw Exception("Failed to delete the old extension output log");
		}

		aHandle = CreateFile(Text::toT(aPath).c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_WRITE | FILE_SHARE_READ,
			&saAttr,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);

		if (aHandle == INVALID_HANDLE_VALUE) {
			dcdebug("Failed to create extension output log %s: %s\n", aPath.c_str(), Util::translateError(::GetLastError()).c_str());
			throw Exception("Failed to create extension output log");
		}
	}

	void Extension::disableLogInheritance(HANDLE& aHandle) {
		if (!SetHandleInformation(aHandle, HANDLE_FLAG_INHERIT, 0))
			throw Exception("Failed to set handle information");
	}

	void Extension::closeLog(HANDLE& aHandle) {
		if (aHandle != INVALID_HANDLE_VALUE) {
			auto result = CloseHandle(aHandle);
			dcassert(result != 0);
			aHandle = INVALID_HANDLE_VALUE;
		}
	}

	void Extension::createProcess(const string& aEngine, WebServerManager* wsm, const SessionPtr& aSession) {
		// Setup log file for console output
		initLog(messageLogHandle, getMessageLogPath());
		initLog(errorLogHandle, getErrorLogPath());

		// Set streams
		STARTUPINFO siStartInfo;
		ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
		siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
		siStartInfo.hStdInput = NULL;
		siStartInfo.hStdOutput = messageLogHandle;
		siStartInfo.hStdError = errorLogHandle;

		ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

		auto paramList = getLaunchParams(wsm, aSession);

		string command(aEngine + " ");
		for (const auto& p: paramList) {
			command += p + " ";
		}

		// Start the process
		tstring commandT = Text::toT(command);
		dcdebug("Starting extension %s, command %s\n", name.c_str(), command.c_str());

#ifdef _DEBUG
		// Show the console window in debug mode
		// The connection may stay alive indefinitely when the process is killed 
		// and the extension will not quit until the ping fails
		//DWORD flags = DETACHED_PROCESS | CREATE_NO_WINDOW;
		DWORD flags = 0;
		//siStartInfo.wShowWindow = SW_MINIMIZE;
#else
		DWORD flags = CREATE_NO_WINDOW;
#endif

		auto res = CreateProcess(
			NULL,
			(LPWSTR)commandT.c_str(),
			0,
			0,
			TRUE,
			flags,
			0,
			NULL,
			&siStartInfo,
			&piProcInfo
		);

		if (res == 0) {
			dcdebug("Failed to start the extension process: %s (code %d)\n", Util::translateError(::GetLastError()).c_str(), res);
			throw Exception("Failed to create process for the extension");
		}

		CloseHandle(piProcInfo.hThread);

		// Extensions spawned after this shouldn't inherit our log handles...
		disableLogInheritance(messageLogHandle);
		disableLogInheritance(errorLogHandle);
	}

	void Extension::checkRunningState(WebServerManager*) noexcept {
		DWORD exitCode = 0;
		if (GetExitCodeProcess(piProcInfo.hProcess, &exitCode) != 0) {
			if (exitCode != STILL_ACTIVE) {
				dcdebug("Extension %s exited with code %d\n", name.c_str(), exitCode);
				onStopped(true);
			}
		} else {
			dcdebug("Failed to check running state of extension %s (%s)\n", name.c_str(), Util::translateError(::GetLastError()).c_str());
			dcassert(0);
		}
	}

	void Extension::resetProcessState() noexcept {
		closeLog(messageLogHandle);
		closeLog(errorLogHandle);

		if (piProcInfo.hProcess != INVALID_HANDLE_VALUE) {
			CloseHandle(piProcInfo.hProcess);
			piProcInfo.hProcess = INVALID_HANDLE_VALUE;
		}
	}

	bool Extension::terminateProcess() noexcept {
		if (TerminateProcess(piProcInfo.hProcess, 0) == 0) {
			dcdebug("Failed to terminate the extension %s: %s\n", name.c_str(), Util::translateError(::GetLastError()).c_str());
			dcassert(0);
			return false;
		}

		WaitForSingleObject(piProcInfo.hProcess, 5000);
		return true;
	}
#else
#include <sys/wait.h>

	void Extension::checkRunningState(WebServerManager* wsm) noexcept {
		int status = 0;
		if (waitpid(pid, &status, WNOHANG) != 0) {
			onStopped(true);
		}
	}

	void Extension::resetProcessState() noexcept {
		pid = 0;
	}

	void Extension::createProcess(const string& aEngine, WebServerManager* wsm, const SessionPtr& aSession) {
		// Init logs
		File messageLog(getMessageLogPath(), File::CREATE, File::RW);
		File errorLog(getErrorLogPath(), File::CREATE, File::RW);


		// Construct argv
		char* app = (char*)aEngine.c_str();

		vector<char*> argv;
		argv.push_back(app);

		{
			auto paramList = getLaunchParams(wsm, aSession);
			for (const auto& p : paramList) {
				argv.push_back((char*)p.c_str());
			}

#ifdef _DEBUG
			string command = string(app) + " ";
			for (const auto& p : paramList) {
				command += p + " ";
			}

			dcdebug("Starting extension %s, command %s\n", name.c_str(), command.c_str());
#endif
		}

		argv.push_back(0);


		// Create fork
		pid = fork();
		if (pid == -1) {
			throw Exception("Failed to fork the process process: " + Util::translateError(errno));
		}

		if (pid == 0) {
			// Child process

			// Redirect messages to log files
			dup2(messageLog.getNativeHandle(), STDOUT_FILENO);
			dup2(errorLog.getNativeHandle(), STDERR_FILENO);

			// Run, checkRunningState will handle errors...
			if (execvp(aEngine.c_str(), &argv[0]) == -1) {
				fprintf(stderr, "Failed to start the extension %s: %s\n", name.c_str(), Util::translateError(errno).c_str());
			}

			exit(0);
		}
	}

	bool Extension::terminateProcess() noexcept {
		auto res = kill(pid, SIGTERM);
		if (res == -1) {
			dcdebug("Failed to terminate the extension %s: %s\n", name.c_str(), Util::translateError(errno).c_str());
			return false;
		}

		int exitStatus = 0;
		if (waitpid(pid, &exitStatus, 0) == -1) {
			dcdebug("Failed to terminate the extension %s: %s\n", name.c_str(), Util::translateError(errno).c_str());
			return false;
		}



		return true;
	}

#endif
}