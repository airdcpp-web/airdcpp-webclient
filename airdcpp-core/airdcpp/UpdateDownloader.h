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

#ifndef DCPLUSPLUS_DCPP_UPDATE_DOWNLOADER_H
#define DCPLUSPLUS_DCPP_UPDATE_DOWNLOADER_H

#include <airdcpp/UpdateVersion.h>

#include <airdcpp/Message.h>

namespace dcpp {

struct HttpDownload;
class UpdateManager;

#ifndef NO_CLIENT_UPDATER

class UpdateDownloader {

// #define FORCE_UPDATE

public:
	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	explicit UpdateDownloader(UpdateManager* aUm) noexcept;
	int getInstalledUpdate() const noexcept { return installedUpdate; }
	bool isUpdating() const noexcept;
	void downloadUpdate(const UpdateVersion& aVersion, bool aManualCheck);

	bool onVersionDownloaded(SimpleXML& xml, bool aVerified, bool aManualCheck);

	enum class UpdateMethod {
		UNDEFINED,
		AUTO,
		PROMPT
	};

	// Extract the updater package
	// Returns the path of the extracted updater executable
	// Throws FileException, ZipFileException
	static string extractUpdater(const string& aUpdaterPath, int aBuildID, const string& aSessionToken);

	static optional<UpdateVersion> parseVersionFile(SimpleXML& xml, bool aVerified);
private:
	UpdateManager* um;
	unique_ptr<HttpDownload> clientDownload;

	string sessionToken;
	int installedUpdate = 0;

	void completeUpdateDownload(const string& aUpdaterTTH, int aBuildID, bool aManualCheck);

	void failUpdateDownload(const string& aError, bool aManualCheck);

	void announceVersion(SimpleXML& xml, const UpdateVersion& aVersion, bool aManualCheck);
	static optional<UpdateVersion> parseVersionInfo(SimpleXML& xml, bool aVerified);
	static bool isBadVersion(SimpleXML& xml);
};

#else
class UpdateDownloader {
public:
	UpdateDownloader(UpdateManager*) noexcept {

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

#endif // DCPLUSPLUS_DCPP_UPDATE_DOWNLOADER_H