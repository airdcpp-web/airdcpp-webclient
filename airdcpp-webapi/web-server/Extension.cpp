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

#include <web-server/WebUserManager.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>

#include <airdcpp/File.h>


namespace webserver {
	Extension::Extension(const string& aPath, ErrorF&& aErrorF, bool aSkipPathValidation) : errorF(std::move(aErrorF)) {
		const auto packageStr = File(aPath + "package" + PATH_SEPARATOR_STR + "package.json", File::READ, File::OPEN).read();

		try {
			const json packageJson = json::parse(packageStr);

			const string packageName = packageJson.at("name");
			const string packageDescription = packageJson.at("description");
			const string packageEntry = packageJson.at("main");
			const string packageVersion = packageJson.at("version");
			const string packageAuthor = packageJson.at("author").at("name");

			auto enginesJson = packageJson.find("engines");
			if (enginesJson != packageJson.end()) {
				for (const auto& engine: json::iterator_wrapper(*enginesJson)) {
					engines.emplace_back(engine.key());
				}
			}

			if (engines.empty()) {
				engines.emplace_back("node");
			}

			name = packageName;
			description = packageDescription;
			entry = packageEntry;
			version = packageVersion;
			author = packageAuthor;

			privateExtension = packageJson.value("private", false);
		} catch (const std::exception& e) {
			throw Exception("Could not parse package.json (" + string(e.what()) + ")");
		}

		if (!aSkipPathValidation && compare(name, Util::getLastDir(aPath)) != 0) {
			throw Exception("Extension path doesn't match with the extension name " + name);
		}

		// TODO: validate platform and API compatibility
	}

	void Extension::start(const string& aEngine, WebServerManager* wsm) {
		File::ensureDirectory(getLogPath());
		File::ensureDirectory(getSettingsPath());

		if (isRunning()) {
			dcassert(0);
			return;
		}

		session = wsm->getUserManager().createExtensionSession(name);
		
		createProcess(aEngine, wsm, session);
		running = true;

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

		// Connect URL
		ret.push_back(getConnectUrl(wsm));

		// Session token
		ret.push_back(aSession->getAuthToken());

		// Package directory
		ret.push_back(getRootPath());

		return ret;
	}

	bool Extension::stop() noexcept {
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
		if (aFailed) {
			timer->stop(false);
		}

		if (session) {
			session->getServer()->getUserManager().logout(session);
			session = nullptr;
		}

		resetProcessState();

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