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

#ifndef DCPLUSPLUS_DCPP_EXTENSION_H
#define DCPLUSPLUS_DCPP_EXTENSION_H

#include <web-server/stdinc.h>

#include <airdcpp/GetSet.h>
#include <airdcpp/Util.h>

namespace webserver {
#define EXTENSION_DIR_ROOT Util::getPath(Util::PATH_USER_CONFIG) + "extensions" + PATH_SEPARATOR_STR

	class Extension {
	public:
		typedef std::function<void(const Extension*)> ErrorF;

		// Throws on errors
		Extension(const string& aPath, ErrorF&& aErrorF, bool aSkipPathValidation = false);

		// Throws on errors
		void start(WebServerManager* wsm);

		// Stop the extension and wait until it's not running anymore
		// Returns false if the process couldn't be stopped
		bool stop() noexcept;

		string getRootPath() const noexcept {
			return EXTENSION_DIR_ROOT + name + PATH_SEPARATOR_STR;
		}

		string getMessageLogPath() const noexcept {
			return getRootPath() + "output.log";
		}

		string getErrorLogPath() const noexcept {
			return getRootPath() + "error.log";
		}

		string getPackageDirectory() const noexcept {
			return getRootPath() + "package" + PATH_SEPARATOR_STR;
		}

		GETSET(string, name, Name);
		GETSET(string, entry, Entry);
		GETSET(string, version, Version);
		GETSET(string, author, Author);

		bool isRunning() const noexcept {
			return running;
		}

		bool isPrivate() const noexcept {
			return privateExtension;
		}
	private:
		bool privateExtension = false;

		StringList getLaunchParams(WebServerManager* wsm, const SessionPtr& aSession) const noexcept;

		bool running = false;

		// Throws on errors
		void createProcess(WebServerManager* wsm, const SessionPtr& aSession);

		const ErrorF errorF;
		SessionPtr session = nullptr;

		void checkRunningState(WebServerManager* wsm) noexcept;
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
		pid_t pid = 0;
#endif
	};

	inline bool operator==(const ExtensionPtr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }
}

#endif