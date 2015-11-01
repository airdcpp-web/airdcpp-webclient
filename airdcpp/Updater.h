/*
 * Copyright (C) 2012-2015 AirDC++ Project
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

class UpdateManager;

#ifndef NO_CLIENT_UPDATER

class Updater {

#define UPDATE_TEMP_DIR Util::getTempPath() + "Updater" + PATH_SEPARATOR_STR

public:

	static bool applyUpdate(const string& sourcePath, const string& installPath, string& error);
	static bool extractFiles(const string& curSourcePath, const string& curExtractPath, string& error);
	static void signVersionFile(const string& file, const string& key, bool makeHeader = false);
	static void createUpdate();

	static bool checkPendingUpdates(const string& aDstDir, string& updater_, bool updated);
	static void cleanTempFiles(const string& tmpPath);

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
private:
	UpdateManager* um;
	unique_ptr<HttpDownload> clientDownload;

	string updateTTH;
	string sessionToken;
	int installedUpdate = 0;

	void completeUpdateDownload(int buildID, bool manualCheck);
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