/*
 * Copyright (C) 2012-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_UPDATER_H
#define DCPLUSPLUS_DCPP_UPDATER_H

#include <airdcpp/core/header/typedefs.h>

namespace dcpp {

class File;

#ifndef NO_CLIENT_UPDATER

class Updater {

public:
	class FileLogger {
	public:
		FileLogger(const string& aPath, bool aResetFile);
		void log(const string& aLine, bool aAddDate = true) noexcept;
		void separator() noexcept;

		FileLogger(FileLogger&) = delete;
		FileLogger& operator=(FileLogger&) = delete;
	private:
		unique_ptr<File> f;
	};

	static FileLogger createInstallLogger(const string& aSourcePath) noexcept;
	static bool applyUpdate(const string& aSourcePath, const string& aInstallPath, string& error_, int aMaxRetries, FileLogger& logger_) noexcept;

	// Returns true if there are pending updates available for this instance
	// This will also remove obsolete updater directories for this instance
	// aUpdateAttempted should be set to true if updating was just attempted (succeed or failed)
	static bool checkAndCleanUpdaterFiles(const string& aAppPath, string& updaterFile_, bool aUpdateAttempted);

	static string getFinalLogFilePath() noexcept;
private:
	// Copy files recursively from the temp directory to application directory
	static bool applyUpdaterFiles(const string& aCurTempPath, const string& aCurDestinationPath, string& error_, StringSet& updatedFiles_, FileLogger& aLogger) noexcept;

	struct UpdaterInfo {
		string updaterFilePath;
		int version;
	};

	static optional<UpdaterInfo> parseUpdaterInfo(const string& aFilePath, const string& aAppPath);
	static void removeUpdater(const string& aInfoFilePath, const string& aUpdaterPath, FileLogger& logger_);

	// Removes files that are not listed in updatedFiles_ recursively
	static int cleanExtraFiles(const string& aCurPath, const optional<StringSet>& aProtectedFiles) noexcept;

	// Remove all content from the directory
	// Returns the number of files removed
	static int destroyDirectory(const string& aPath);

	static string toLoggerFilePath(const string& aDirectoryPath) noexcept;
};

#endif

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_UPDATER_H