/*
 * Copyright (C) 2012-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_UPDATER_H
#define DCPLUSPLUS_DCPP_UPDATER_H

#include "HttpDownload.h"

namespace dcpp {

class File;
class FileException;
class UpdateManager;
class ZipFileException;

#ifndef NO_CLIENT_UPDATER

class Updater {

#define UPDATE_TEMP_DIR Util::getTempPath() + "Updater" + PATH_SEPARATOR_STR
#define UPDATE_TEMP_LOG Util::getTempPath() + "airdcpp_updater.log"

public:
	class FileLogger {
	public:
		FileLogger(const string& aPath, bool aResetFile);
		void log(const string& aLine, bool aAddDate = true) noexcept;
		void separator() noexcept;
	private:
		unique_ptr<File> f;
	};

	static bool applyUpdate(const string& aSourcePath, const string& aInstallPath, string& error_, int aMaxRetries) noexcept;

	static void signVersionFile(const string& file, const string& key, bool makeHeader = false);

	// Create an updater zip file from the current application (it must be in the default "compiled" path)
	// Returns the path of the created updater file
	static string createUpdate() noexcept;

	// Returns true if there are pending updates available for this instance
	// This will also remove obsolate updater directories for this instance
	// aUpdateAttempted should be set to true if updating was just attempted (succeed or failed)
	static bool checkPendingUpdates(const string& aAppPath, string& updaterFile_, bool aUpdateAttempted);

	static bool getUpdateVersionInfo(SimpleXML& xml, string& versionString, int& remoteBuild);

	Updater(UpdateManager* aUm) noexcept;
	int getInstalledUpdate() { return installedUpdate; }
	bool isUpdating();
	void downloadUpdate(const string& aUrl, int newBuildID, bool manualCheck);

	bool onVersionDownloaded(SimpleXML& xml, bool aVerified, bool aManualCheck);

	enum UpdateMethod {
		UPDATE_UNDEFINED,
		UPDATE_AUTO,
		UPDATE_PROMPT
	};

	// Extract the updater package
	// Returns the path of the extracted updater executable
	// Throws FileException, ZipFileException
	static string extractUpdater(const string& aUpdaterPath, int aBuildID, const string& aSessionToken);
private:
	// Copy files recursively from the temp directory to application directory
	static bool applyUpdaterFiles(const string& aCurTempPath, const string& aCurDestinationPath, string& error_, StringSet& updatedFiles_, FileLogger& aLogger) noexcept;

	// Removes files that are not listed in updatedFiles_ recursively
	static int cleanExtraFiles(const string& aCurPath, const optional<StringSet>& aProtectedFiles) noexcept;

	// Remove all content from the directory
	// Returns the number of files removed
	static int destroyDirectory(const string& aPath);
	 
	UpdateManager* um;
	unique_ptr<HttpDownload> clientDownload;

	string updateTTH;
	string sessionToken;
	int installedUpdate = 0;

	void completeUpdateDownload(int aBuildID, bool aManualCheck);

	void failUpdateDownload(const string& aError, bool manualCheck);
};

#else
class Updater {
public:
	Updater(UpdateManager*) noexcept {

	}

	bool onVersionDownloaded(SimpleXML&, bool, bool) {
		return false;
	}

	bool isUpdating() const noexcept {
		return false;
	}
};

#endif

} // namespace dcpp

#endif // UPDATER_H