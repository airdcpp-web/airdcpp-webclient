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

#include <web-server/Extension.h>

#include <web-server/SystemUtil.h>
#include <web-server/WebUserManager.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/version.h>

#include <airdcpp/Exception.h>
#include <airdcpp/File.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/SystemUtil.h>


namespace webserver {
	SharedMutex Extension::cs;

	string Extension::getRootPath(const string& aName) noexcept {
		return EXTENSION_DIR_ROOT + aName + PATH_SEPARATOR_STR;
	}

	string Extension::getRootPath() const noexcept {
		return getRootPath(name);
	}

	string Extension::getMessageLogPath() const noexcept {
		return PathUtil::joinDirectory(getRootPath(), EXT_LOG_DIR) + "output.log";
	}

	string Extension::getErrorLogPath() const noexcept {
		return PathUtil::joinDirectory(getRootPath(), EXT_LOG_DIR) + "error.log";
	}

	Extension::Extension(const string& aPackageDirectory, ErrorF&& aErrorF, bool aSkipPathValidation) : errorF(std::move(aErrorF)), managed(true) {
		initializeThrow(aPackageDirectory, aSkipPathValidation);
	}

	Extension::Extension(const SessionPtr& aSession, const json& aPackageJson) : managed(false), session(aSession) {
		initializeThrow(aPackageJson);
	}

	Extension::~Extension() {
		dcdebug("Extension %s was destroyed\n", name.c_str());
	}

	void Extension::reloadThrow() {
		initializeThrow(PathUtil::joinDirectory(getRootPath(), EXT_PACKAGE_DIR), false);

		fire(ExtensionListener::PackageUpdated(), this);
	}

	void Extension::initializeThrow(const string& aPackageDirectory, bool aSkipPathValidation) {
		string packageStr;
		try {
			packageStr = File(aPackageDirectory + "package.json", File::READ, File::OPEN).read();
		} catch (const FileException& e) {
			throw Exception("Could not open " + aPackageDirectory + "package.json (" + string(e.what()) + ")");
		}

		try {
			const json packageJson = json::parse(packageStr);

			const string packageEntry = packageJson.at("main");
			entry = packageEntry;

			initializeThrow(packageJson);
		} catch (const std::exception& e) {
			throw Exception("Could not parse package.json (" + string(e.what()) + ")");
		}

		if (!aSkipPathValidation && compare(name, PathUtil::getLastDir(PathUtil::getParentDir(aPackageDirectory))) != 0) {
			throw Exception("Extension path doesn't match with the extension name " + name);
		}
	}

	void Extension::initializeThrow(const json& aJson) {
		// Required fields
		const string packageName = aJson.at("name");
		const string packageDescription = aJson.at("description");
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
		version = packageVersion;

		// Optional fields
		homepage = aJson.value("homepage", "");

		{
			// Engines
			auto enginesJson = aJson.find("engines");
			if (enginesJson != aJson.end()) {
				for (const auto& engine : (*enginesJson).items()) {
					engines.emplace_back(engine.key());
				}
			}

			if (engines.empty()) {
				engines.emplace_back(EXT_ENGINE_NODE);
			}
		}

		{
			// Operating system
			auto osJson = aJson.find("os");
			if (osJson != aJson.end()) {
				const StringList osList = *osJson;
				auto currentOs = SystemUtil::getPlatform();
				if (std::find(osList.begin(), osList.end(), currentOs) == osList.end() && currentOs != "other") {
					throw Exception(STRING(WEB_EXTENSION_OS_UNSUPPORTED));
				}
			}
		}

		parseApiDataThrow(aJson.at("airdcpp"));
	}

	void Extension::checkCompatibilityThrow() {
		if (apiVersion != API_VERSION) {
			throw Exception(STRING_F(WEB_EXTENSION_API_VERSION_UNSUPPORTED, Util::toString(apiVersion) % Util::toString(API_VERSION)));
		}

		if (minApiFeatureLevel > API_FEATURE_LEVEL) {
			throw Exception(STRING_F(WEB_EXTENSION_API_FEATURES_UNSUPPORTED, Util::toString(minApiFeatureLevel) % Util::toString(API_FEATURE_LEVEL)));
		}
	}

	void Extension::parseApiDataThrow(const json& aJson) {
		apiVersion = aJson.at("apiVersion");
		minApiFeatureLevel = aJson.value("minApiFeatureLevel", 0);
		signalReady = aJson.value("signalReady", false);
	}

	FilesystemItemList Extension::getLogs() const noexcept {
		FilesystemItemList ret;

		if (managed) {
			File::forEachFile(PathUtil::joinDirectory(getRootPath(), EXT_LOG_DIR), "*.log", [&](const FilesystemItem& aInfo) {
				if (aInfo.isDirectory) {
					return;
				}

				ret.push_back(aInfo);
			});
		}

		return ret;
	}

	ExtensionSettingItem* Extension::getSetting(const string& aKey) noexcept {
		RLock l(cs);
		return ApiSettingItem::findSettingItem<ExtensionSettingItem>(settings, aKey);
	}

	bool Extension::hasSettings() const noexcept {
		RLock l(cs);
		return !settings.empty(); 
	}

	ExtensionSettingItem::List Extension::getSettings() const noexcept {
		RLock l(cs);
		return settings;
	}

	void Extension::swapSettingDefinitions(ExtensionSettingItem::List& aDefinitions) noexcept {
		{
			WLock l(cs);
			settings.swap(aDefinitions);
		}

		fire(ExtensionListener::SettingDefinitionsUpdated(), this);
	}

	void Extension::resetSettings() noexcept {
		{
			WLock l(cs);
			settings.clear();
			userReferences.clear();
		}

		fire(ExtensionListener::SettingDefinitionsUpdated(), this);
	}

	void Extension::setValidatedSettingValues(const SettingValueMap& aValues, const UserList& aUserReferences) noexcept {
		{
			WLock l(cs);
			for (const auto& vp: aValues) {
				auto setting = ApiSettingItem::findSettingItem<ExtensionSettingItem>(settings, vp.first);
				if (!setting) {
					dcassert(0);
					continue;
				}

				setting->setValue(vp.second);
			}

			userReferences.insert(aUserReferences.begin(), aUserReferences.end());
		}

		fire(ExtensionListener::SettingValuesUpdated(), this, aValues);
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

	void Extension::startThrow(const string& aEngine, WebServerManager* wsm, const StringList& aExtraArgs) {
		if (!managed) {
			return;
		}

		if (!wsm->isRunning()) {
			throw Exception(STRING(WEB_EXTENSION_SERVER_NOT_RUNNING));
		}

		if (!wsm->isListeningPlain()) {
			throw Exception(STRING(WEB_EXTENSION_HTTP_NOT_ENABLED));
		}

		if (isRunning()) {
			dcassert(0);
			return;
		}

		File::ensureDirectory(PathUtil::joinDirectory(getRootPath(), EXT_LOG_DIR));
		File::ensureDirectory(PathUtil::joinDirectory(getRootPath(), EXT_CONFIG_DIR));

		checkCompatibilityThrow();

		session = wsm->getUserManager().createExtensionSession(name);
		
		createProcessThrow(aEngine, wsm, session, aExtraArgs);

		running = true;
		fire(ExtensionListener::ExtensionStarted(), this);

		// Monitor the running state of the script
		timer = wsm->addTimer([this, wsm] { checkRunningState(wsm); }, 2500);
		timer->start(false);
	}

	string Extension::getConnectUrl(WebServerManager* wsm) noexcept {
		return wsm->getLocalServerAddress(wsm->getPlainServerConfig()) + "/api/v1/";
	}

	StringList Extension::getLaunchParams(WebServerManager* wsm, const SessionPtr& aSession, bool aEscape, const StringList& aExtraArgs) const noexcept {
		// Add custom args before the file path
		StringList ret = aExtraArgs;

		// Wrap strings possibly containing whitespaces in doube quotes
		auto maybeEscape = [aEscape](const string& aStr) {
			if (!aEscape || aStr.empty()) {
				return aStr;
			}

			string ret = "\"" + aStr;

			// At least Windows has problems with backslashes before double quotes 
			// (the slash won't be escaped properly in argv)
			if (ret.back() == '\\') {
				// Make it double backslash
				ret += "\\";
			}

			return ret + "\"";
		};

		// Script to launch
		ret.push_back(maybeEscape(PathUtil::joinDirectory(getRootPath(), EXT_PACKAGE_DIR) + entry));


		// Params
		auto addParamImpl = [&ret, &maybeEscape](const string& aName, const string& aParam = Util::emptyString) {
			auto arg = "--" + aName;
			if (!aParam.empty()) {
				arg += "=" + aParam;
			}

			ret.push_back(arg);
		};

		auto addStrParam = [&maybeEscape, &addParamImpl](const string& aName, const string& aParam) {
			addParamImpl(aName, maybeEscape(aParam));
		};

		auto addIntParam = [&addParamImpl](const string& aName, int aParam) {
			addParamImpl(aName, Util::toString(aParam));
		};

		auto addFlagParam = addParamImpl;

		// Name
		addStrParam("name", name);

		// Connect URL
		addStrParam("apiUrl", getConnectUrl(wsm));

		// Session token
		addStrParam("authToken", aSession->getAuthToken());

		// Paths
		addStrParam("logPath", PathUtil::joinDirectory(getRootPath(), EXT_LOG_DIR));
		addStrParam("settingsPath", PathUtil::joinDirectory(getRootPath(), EXT_CONFIG_DIR));

		if (WEBCFG(EXTENSIONS_DEBUG_MODE).boolean()) {
			addFlagParam("debug");
		}

		if (signalReady) {
			addFlagParam("signalReady");
		}

		// Process ID
		addIntParam("appPid", getAppPid());

		return ret;
	}

	void Extension::stopThrow() {
		if (!managed) {
			throw Exception("Remote extensions can't be stopped");
		}

		if (!isRunning()) {
			return;
		}

		timer->stop(false);
		try {
			terminateProcessThrow();
		} catch (const Exception& e) {
			throw Exception(STRING_F(WEB_EXTENSION_TERMINATE_PROCESS_FAILED, e.what()));
		}

		onStopped(false);
	}

	void Extension::onFailed(uint32_t aExitCode) noexcept {
		dcdebug("Extension %s failed with code %u\n", name.c_str(), aExitCode);

		timer->stop(false);

		onStopped(true);

		if (errorF) {
			errorF(this, aExitCode);
		}
	}

	void Extension::resetSession() noexcept {
		if (session) {
			if (managed) {
				session->getServer()->getUserManager().logout(session);
				dcassert(session.use_count() == 1);
			}

			session = nullptr;
		}
	}

	void Extension::onStopped(bool aFailed) noexcept {
		fire(ExtensionListener::ExtensionStopped(), this, aFailed);
		
		dcdebug("Extension %s was stopped", name.c_str());
		if (session) {
			dcdebug(" (session %s, use count %ld)", session->getAuthToken().c_str(), session.use_count());
		}
		dcdebug("\n");

		resetSession();
		resetProcessState();
		resetSettings();

		dcassert(running);
		running = false;
	}

	void Extension::rotateLog(const string& aPath) {
		auto oldFilePath = aPath + ".old";

		try {
			if (PathUtil::fileExists(oldFilePath)) {
				File::deleteFileThrow(oldFilePath);
			}

			if (PathUtil::fileExists(aPath)) {
				File::copyFile(aPath, oldFilePath);
				File::deleteFileThrow(aPath);
			}
		} catch (const FileException& e) {
			throw Exception("Failed to initialize the extension log " + aPath + ": " + e.getError());
		}
	}

#ifdef _WIN32
	void Extension::initLog(HANDLE& aHandle, const string& aPath) {
		dcassert(aHandle == INVALID_HANDLE_VALUE);

		SECURITY_ATTRIBUTES saAttr;
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
		saAttr.bInheritHandle = TRUE;
		saAttr.lpSecurityDescriptor = NULL;

		rotateLog(aPath);

		aHandle = CreateFile(Text::toT(aPath).c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_WRITE | FILE_SHARE_READ,
			&saAttr,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);

		if (aHandle == INVALID_HANDLE_VALUE) {
			dcdebug("Failed to create extension output log %s: %s\n", aPath.c_str(), dcpp::SystemUtil::translateError(::GetLastError()).c_str());
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

	void Extension::createProcessThrow(const string& aEngine, WebServerManager* wsm, const SessionPtr& aSession, const StringList& aExtraArgs) {
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

		auto paramList = getLaunchParams(wsm, aSession, true, aExtraArgs);

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
			dcdebug("Failed to start the extension process: %s (code %d)\n", dcpp::SystemUtil::translateError(::GetLastError()).c_str(), res);
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
				onFailed(exitCode);
			}
		} else {
			dcdebug("Failed to check running state of extension %s (%s)\n", name.c_str(), dcpp::SystemUtil::translateError(::GetLastError()).c_str());
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

	void Extension::terminateProcessThrow() {
		if (TerminateProcess(piProcInfo.hProcess, 0) == 0) {
			throw Exception(dcpp::SystemUtil::translateError(::GetLastError()));
		}

		auto res = WaitForSingleObject(piProcInfo.hProcess, 5000);
		if (res != WAIT_OBJECT_0) {
			auto error = res == WAIT_FAILED ? dcpp::SystemUtil::translateError(res).c_str() : STRING(SETTINGS_ODC_SHUTDOWNTIMEOUT);
			throw Exception(error);
		}
	}

	int Extension::getAppPid() noexcept {
		return GetCurrentProcessId();
	}
#else
#include <sys/wait.h>

	void Extension::checkRunningState(WebServerManager* wsm) noexcept {
		int status = 0;
		if (waitpid(pid, &status, WNOHANG) != 0) {
			int exitCode = 1;
			if (WIFEXITED(status)) {
				exitCode = WEXITSTATUS(status);
			}

			onFailed(exitCode);
		}
	}

	void Extension::resetProcessState() noexcept {
		pid = 0;
	}

	unique_ptr<File> Extension::initLog(const string& aPath) {
		rotateLog(aPath);
		return make_unique<File>(aPath, File::RW, File::CREATE | File::TRUNCATE);
	}

	void Extension::createProcessThrow(const string& aEngine, WebServerManager* wsm, const SessionPtr& aSession, const StringList& aExtraArgs) {
		// Init logs
		auto messageLog = std::move(initLog(getMessageLogPath()));
		auto errorLog = std::move(initLog(getErrorLogPath()));

		// Construct argv
		char* app = (char*)aEngine.c_str();

		// Note that pushed pointed params should not be destructed until the extension is running...
		vector<char*> argv;
		argv.push_back(app);

		auto paramList = getLaunchParams(wsm, aSession, false, aExtraArgs);
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

		argv.push_back(0);


		// Create fork
		pid = fork();
		if (pid == -1) {
			throw Exception("Failed to fork the process process: " + dcpp::SystemUtil::translateError(errno));
		}

		if (pid == 0) {
			// Child process

			// Redirect messages to log files
			dup2(messageLog->getNativeHandle(), STDOUT_FILENO);
			dup2(errorLog->getNativeHandle(), STDERR_FILENO);

			// Run, checkRunningState will handle errors...
			if (execvp(aEngine.c_str(), &argv[0]) == -1) {
				fprintf(stderr, "Failed to start the extension %s: %s\n", name.c_str(), dcpp::SystemUtil::translateError(errno).c_str());
			}

			exit(0);
		}
	}

	void Extension::terminateProcessThrow() {
		auto res = kill(pid, SIGTERM);
		if (res == -1) {
			throw Exception(dcpp::SystemUtil::translateError(errno));
		}

		int exitStatus = 0;
		if (waitpid(pid, &exitStatus, 0) == -1) {
			throw Exception(dcpp::SystemUtil::translateError(errno));
		}
	}

	int Extension::getAppPid() noexcept {
		return getpid();
	}
#endif
}