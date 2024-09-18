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

#include "stdinc.h"

#include "UpdateDownloader.h"
#include "UpdateConstants.h"

#include <airdcpp/File.h>
#include <airdcpp/Exception.h>
#include <airdcpp/HashCalc.h>
#include <airdcpp/HttpDownload.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/ScopedFunctor.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/StringTokenizer.h>
#include <airdcpp/SystemUtil.h>
#include <airdcpp/UpdateManager.h>
#include <airdcpp/Util.h>
#include <airdcpp/ZipFile.h>
#include <airdcpp/ValueGenerator.h>
#include <airdcpp/version.h>

namespace dcpp {

const auto OWN_BUILD = BUILD_NUMBER;

void UpdateDownloader::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(UPDATER));
}

UpdateDownloader::UpdateDownloader(UpdateManager* aUm) noexcept : um(aUm) {
	sessionToken = Util::toString(ValueGenerator::rand());
}

string UpdateDownloader::extractUpdater(const string& aUpdaterPath, int aBuildID, const string& aSessionToken) {
	ZipFile zip;
	zip.Open(aUpdaterPath);

	string srcPath = UPDATE_TEMP_DIR + aSessionToken + PATH_SEPARATOR;
	string dstPath = AppUtil::getAppFilePath();
	string updaterExeFile = srcPath + AppUtil::getAppFileName();

	if (zip.GoToFirstFile()) {
		do {
			zip.OpenCurrentFile();
			if (zip.GetCurrentFileName().find(PathUtil::getFileExt(updaterExeFile)) != string::npos && zip.GetCurrentFileName().find('/') == string::npos) {
				zip.ReadCurrentFile(updaterExeFile);
			} else zip.ReadCurrentFile(srcPath);
			zip.CloseCurrentFile();
		} while (zip.GoToNextFile());
	}

	zip.Close();

	//Write the XML file
	SimpleXML xml;
	xml.addTag("UpdateInfo");
	xml.stepIn();
	xml.addTag("DestinationPath", dstPath);
	xml.addTag("SourcePath", srcPath);
	xml.addTag("ConfigPath", AppUtil::getPath(AppUtil::PATH_USER_CONFIG));
	xml.addTag("UpdaterFile", updaterExeFile);
	xml.addTag("BuildID", aBuildID);
	xml.stepOut();

	{
		File f(UPDATE_TEMP_DIR + "UpdateInfo_" + aSessionToken + ".xml", File::WRITE, File::CREATE | File::TRUNCATE);
		f.write(SimpleXML::utf8Header);
		f.write(xml.toXML());
	}

	return updaterExeFile;
}


void UpdateDownloader::completeUpdateDownload(const string& aUpdaterTTH, int aBuildID, bool aManualCheck) {
	auto& conn = clientDownload;
	ScopedFunctor([&conn] { conn.reset(); });

	if (!conn->buf.empty()) {
		string updaterFile = UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR + "AirDC_Update.zip";
		ScopedFunctor([&updaterFile] { File::deleteFile(updaterFile); });

		try {
			File::removeDirectory(UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR);
			File::ensureDirectory(UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR);
			File(updaterFile, File::WRITE, File::CREATE | File::TRUNCATE).write(conn->buf);
		} catch (const FileException&) {
			failUpdateDownload(STRING(UPDATER_WRITE_FAILED), aManualCheck);
			return;
		}

		// Check integrity
		dcassert(!aUpdaterTTH.empty());
		if (TTH(updaterFile) != aUpdaterTTH) {
			failUpdateDownload(STRING(INTEGRITY_CHECK_FAILED), aManualCheck);
			return;
		}

		// Unzip the update
		try {
			auto updaterExeFile = extractUpdater(updaterFile, aBuildID, sessionToken);

			log(STRING(UPDATE_DOWNLOADED), LogMessage::SEV_INFO);
			installedUpdate = aBuildID;

			conn.reset(); //prevent problems when closing
			um->fire(UpdateManagerListener::UpdateComplete(), updaterExeFile);
		} catch (const Exception& e) {
			failUpdateDownload(e.getError(), aManualCheck);
		}
	} else {
		failUpdateDownload(conn->status, aManualCheck);
	}
}

void UpdateDownloader::failUpdateDownload(const string& aError, bool manualCheck) {
	auto msg = STRING_F(UPDATING_FAILED, aError);
	if (manualCheck) {
		log(msg, LogMessage::SEV_ERROR);
		um->fire(UpdateManagerListener::UpdateFailed(), msg);
	} else {
		log(msg, LogMessage::SEV_WARNING);
	}
}

bool UpdateDownloader::isBadVersion(SimpleXML& xml) {
	if (xml.findChild("VeryOldVersion")) {
		if (Util::toInt(xml.getChildData()) >= OWN_BUILD) {
			return true;
		}
	}
	xml.resetCurrentChild();

	if (xml.findChild("BadVersions")) {
		xml.stepIn();
		while (xml.findChild("Version")) {
			xml.stepIn();
			double v = Util::toDouble(xml.getData());
			xml.stepOut();

			if (v == OWN_BUILD) {
				return true;
			}
		}

		xml.stepOut();
	}
	xml.resetCurrentChild();
	return false;
}


optional<UpdateVersion> UpdateDownloader::parseVersionFile(SimpleXML& xml, bool aVerified) {
	xml.resetCurrentChild();
	while (xml.findChild("VersionInfo")) {
		// The latest OS must come first
		StringTokenizer<string> t(xml.getChildAttrib("MinOsVersion"), '.');
		StringList& l = t.getTokens();

		if (!SystemUtil::isOSVersionOrGreater(Util::toInt(l[0]), Util::toInt(l[1]))) {
			continue;
		}

		xml.stepIn();
		auto versionInfo = parseVersionInfo(xml, aVerified);
		xml.resetCurrentChild();
		return versionInfo;
	}

	dcassert(0);
	return nullopt;
}


optional<UpdateVersion> UpdateDownloader::parseVersionInfo(SimpleXML& xml, bool aVerified) {
	UpdateVersion versionInfo;

	//Get the update information from the XML
	if (xml.findChild(UPGRADE_TAG)) {
		versionInfo.build = Util::toInt(xml.getChildAttrib("Build"));
		versionInfo.versionStr = xml.getChildAttrib("VersionString");
		versionInfo.updateUrl = xml.getChildData();
		versionInfo.tth = xml.getChildAttrib("TTH");

		auto minUpdateBuild = xml.getIntChildAttrib("MinUpdateRev");
		versionInfo.autoUpdate = (aVerified && minUpdateBuild <= OWN_BUILD);
	} else {
		dcassert(0);
		return nullopt;
	}
	xml.resetCurrentChild();

	// Info URL
	if (xml.findChild("URL")) {
		versionInfo.infoUrl = xml.getChildData();
	}

	xml.resetCurrentChild();
	return versionInfo;
}

void UpdateDownloader::announceVersion(SimpleXML& xml, const UpdateVersion& aVersion, bool aManualCheck) {
	auto updateMethod = static_cast<UpdateMethod>(SETTING(UPDATE_METHOD));
	if ((!aVersion.autoUpdate || updateMethod == UpdateMethod::PROMPT) || aManualCheck) {
		if (xml.findChild("Title")) {
			const string& title = xml.getChildData();
			xml.resetCurrentChild();
			if (xml.findChild("Message")) {
				um->fire(UpdateManagerListener::UpdateAvailable(), title, xml.childToXML(), aVersion);
			}
		}
	} else if (updateMethod == UpdateMethod::AUTO) {
		log(STRING_F(BACKGROUND_UPDATER_START, aVersion.versionStr), LogMessage::SEV_INFO);
		downloadUpdate(aVersion, aManualCheck);
	}

	xml.resetCurrentChild();
}

bool UpdateDownloader::onVersionDownloaded(SimpleXML& xml, bool aVerified, bool aManualCheck) {
	auto version = parseVersionFile(xml, aVerified);
	if (!version) {
		return false;
	}

	if (aVerified && isBadVersion(xml)) {
		string msg = xml.getChildAttrib("Message", "Your version of AirDC++ contains a serious bug that affects all users of the DC network or the security of your computer.");
		um->fire(UpdateManagerListener::BadVersion(), msg, *version);
		return false;
	}

	//Check for updated version

#ifdef FORCE_UPDATE
	auto announce = true;
#else
	auto announce = (version->build > OWN_BUILD && version->build > installedUpdate) || aManualCheck;
#endif

	if (announce) {
		announceVersion(xml, *version, aManualCheck);
	}

	return true;
}

bool UpdateDownloader::isUpdating() const noexcept {
	return clientDownload ? true : false;
}

void UpdateDownloader::downloadUpdate(const UpdateVersion& aVersion, bool aManualCheck) {
	if (clientDownload)
		return;

	clientDownload = make_unique<HttpDownload>(
		aVersion.updateUrl,
		[this, tth = aVersion.tth, build = aVersion.build, aManualCheck] { 
			completeUpdateDownload(tth, build, aManualCheck); 
		}
	);
}

} // namespace dcpp