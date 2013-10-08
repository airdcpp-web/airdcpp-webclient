/*
 * Copyright (C) 2006-2011 Crise, crise<at>mail.berlios.de
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

#include "stdinc.h"
#include "UpdateManager.h"

#include <openssl/rsa.h>
#include <openssl/objects.h>
#include <openssl/pem.h>

#include "AirUtil.h"
#include "GeoManager.h"
#include "HashCalc.h"
#include "Localization.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "ScopedFunctor.h"
#include "SettingsManager.h"
#include "SimpleXML.h"
#include "Text.h"
#include "TimerManager.h"
#include "version.h"

#include "pubkey.h"

#ifdef _WIN32
#include "ZipFile.h"
#endif

#ifdef _WIN64
# define UPGRADE_TAG "UpdateURLx64"
#else
# define UPGRADE_TAG "UpdateURL"
#endif

namespace dcpp {

UpdateManager::UpdateManager() : installedUpdate(0), lastIPUpdate(GET_TICK()) {
	TimerManager::getInstance()->addListener(this);
	sessionToken = Util::toString(Util::rand());

	links.homepage = "http://www.airdcpp.net/";
	links.downloads = links.homepage + "download/";
	links.geoip6 = "http://geoip6.airdcpp.net";
	links.geoip4 = "http://geoip4.airdcpp.net";
	links.guides = links.homepage + "guides/";
	links.customize = links.homepage + "c/customizations/";
	links.discuss = links.homepage + "forum/";
	links.ipcheck4 = "http://checkip.dyndns.org/";
	links.ipcheck6 = "http://checkip.dyndns.org/";
	links.language = "http://languages.airdcpp.net/tx/";
}

UpdateManager::~UpdateManager() { 
	TimerManager::getInstance()->removeListener(this);
}

void UpdateManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	if (SETTING(UPDATE_IP_HOURLY) && lastIPUpdate + 60*60*1000 < aTick) {
		checkIP(false, false);
		lastIPUpdate = aTick;
	}
}

bool UpdateManager::verifyVersionData(const string& data, const ByteVector& signature) {
	int res = -1;

	// Make SHA hash
	SHA_CTX sha_ctx = { 0 };
	uint8_t digest[SHA_DIGEST_LENGTH];

	res = SHA1_Init(&sha_ctx);
	if(res != 1)
		return false;
	res = SHA1_Update(&sha_ctx, data.c_str(), data.size());
	if(res != 1)
		return false;
	res = SHA1_Final(digest, &sha_ctx);
	if(res != 1)
		return false;

	// Extract Key
	const uint8_t* key = UpdateManager::publicKey;
	RSA* rsa = d2i_RSAPublicKey(NULL, &key, sizeof(UpdateManager::publicKey));
	if(rsa) {
		res = RSA_verify(NID_sha1, digest, sizeof(digest), &signature[0], signature.size(), rsa);

		RSA_free(rsa);
		rsa = NULL;
	} else return false;

	return (res == 1); 
}

void UpdateManager::cleanTempFiles(const string& tmpPath) {
	FileFindIter end;
	for(FileFindIter i(tmpPath, "*"); i != end; ++i) {
		string name = i->getFileName();
		if(name == "." || name == "..")
			continue;

		if(i->isLink() || name.empty())
			continue;

		if(i->isDirectory()) {
			UpdateManager::cleanTempFiles(tmpPath + name + PATH_SEPARATOR);
		} else {
			File::deleteFileEx(tmpPath + name, 3);
		}
	}

	// Remove the empty dir
	File::removeDirectory(tmpPath);
}

#ifdef _WIN32
void UpdateManager::completeUpdateDownload(int buildID, bool manualCheck) {
	auto& conn = conns[CONN_CLIENT];
	ScopedFunctor([&conn] { conn.reset(); });

	if(!conn->buf.empty()) {
		string updaterFile = UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR + "AirDC_Update.zip";
		ScopedFunctor([&updaterFile] { File::deleteFile(updaterFile); });

		try {
			File::removeDirectory(UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR);
			File::ensureDirectory(UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR);
			File(updaterFile, File::WRITE, File::CREATE | File::TRUNCATE).write(conn->buf);
		} catch(const FileException&) { 
			failUpdateDownload(STRING(UPDATER_WRITE_FAILED), manualCheck);
			return;
		}

		// Check integrity
		if(TTH(updaterFile) != updateTTH) {
			failUpdateDownload(STRING(INTEGRITY_CHECK_FAILED), manualCheck);
			return;
		}

		// Unzip the update
		try {
			ZipFile zip;
			zip.Open(updaterFile);

			string srcPath = UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR;
			string dstPath = Util::getFilePath(exename);
			string updaterFile = srcPath + Util::getFileName(exename);

			if(zip.GoToFirstFile()) {
				do {
					zip.OpenCurrentFile();
					if(zip.GetCurrentFileName().find(Util::getFileExt(exename)) != string::npos) {
						zip.ReadCurrentFile(updaterFile);
					} else zip.ReadCurrentFile(srcPath);
					zip.CloseCurrentFile();
				} while(zip.GoToNextFile());
			}

			zip.Close();

			//Write the XML file
			SimpleXML xml;
			xml.addTag("UpdateInfo");
			xml.stepIn();
			xml.addTag("DestinationPath", dstPath);
			xml.addTag("SourcePath", srcPath);
			xml.addTag("UpdaterFile", updaterFile);
			xml.addTag("BuildID", buildID);
			xml.stepOut();

			File f(UPDATE_TEMP_DIR + "UpdateInfo_" + sessionToken + ".xml", File::WRITE, File::CREATE | File::TRUNCATE);
			f.write(SimpleXML::utf8Header);
			f.write(xml.toXML());
			f.close();

			LogManager::getInstance()->message(STRING(UPDATE_DOWNLOADED), LogManager::LOG_INFO);
			installedUpdate = buildID;

			conn.reset(); //prevent problems when closing
			fire(UpdateManagerListener::UpdateComplete(), updaterFile);
		} catch(ZipFileException& e) {
			failUpdateDownload(e.getError(), manualCheck);
		}
	} else {
		failUpdateDownload(conn->status, manualCheck);
	}
}

bool UpdateManager::checkPendingUpdates(const string& aDstDir, string& updater_, bool updated) {
	StringList fileList = File::findFiles(UPDATE_TEMP_DIR, "UpdateInfo_*");
	for (auto& uiPath: fileList) {
		if (Util::getFileExt(uiPath) == ".xml") {
			try {
				SimpleXML xml;
				xml.fromXML(File(uiPath, File::READ, File::OPEN).read());
				if(xml.findChild("UpdateInfo")) {
					xml.stepIn();
					if(xml.findChild("DestinationPath")) {
						xml.stepIn();
						string dstDir = xml.getData();
						xml.stepOut();

						if (dstDir != aDstDir)
							continue;

						if(xml.findChild("UpdaterFile")) {
							xml.stepIn();
							updater_ = xml.getData();
							xml.stepOut();

							if(xml.findChild("BuildID")) {
								xml.stepIn();
								if (Util::toInt(xml.getData()) <= BUILD_NUMBER || updated) {
									//we have an old update for this instance, delete the files
									cleanTempFiles(Util::getFilePath(updater_));
									File::deleteFile(uiPath);
									continue;
								}
								return true;
							}
						}
					}

				}
			} catch(const Exception& e) {
				LogManager::getInstance()->message(STRING_F(FAILED_TO_READ, uiPath % e.getError()), LogManager::LOG_WARNING);
			}
		}
	}

	return false;
}

#else

bool UpdateManager::checkPendingUpdates(const string& aDstDir, string& updater_, bool updated) {
	return false;
}

void UpdateManager::completeUpdateDownload(int buildID, bool manualCheck) {

}

#endif

void UpdateManager::completeSignatureDownload(bool manualCheck) {
	auto& conn = conns[CONN_SIGNATURE];
	ScopedFunctor([&conn] { conn.reset(); });

	if(conn->buf.empty()) {
		failUpdateDownload(STRING_F(DOWNLOAD_SIGN_FAILED, conn->status), manualCheck);
	} else {
		versionSig.assign(conn->buf.begin(), conn->buf.end());
	}

	conns[CONN_VERSION].reset(new HttpDownload(VERSION_URL,
	//conns[CONN_VERSION] = make_unique<HttpDownload>("http://beta.airdcpp.net/testversion/version.xml",
		[this, manualCheck] { completeVersionDownload(manualCheck); }, false));
}

void UpdateManager::failUpdateDownload(const string& aError, bool manualCheck) {
	string msg;
	if (conns[CONN_CLIENT]) {
		msg = STRING_F(UPDATING_FAILED, aError);
	} else {
		msg = STRING_F(VERSION_CHECK_FAILED, aError);
	}

	if (manualCheck) {
		fire(UpdateManagerListener::UpdateFailed(), msg);
	} else {
		LogManager::getInstance()->message(msg, LogManager::LOG_WARNING);
	}

	checkAdditionalUpdates(manualCheck);
}

void UpdateManager::checkIP(bool manual, bool v6) {
	conns[v6 ? CONN_IP6 : CONN_IP4].reset(new HttpDownload(v6 ? links.ipcheck6 : links.ipcheck4,
		[=] { completeIPCheck(manual, v6); }, false, !v6));
}

void UpdateManager::completeIPCheck(bool manual, bool v6) {
	auto& conn = conns[v6 ? CONN_IP6 : CONN_IP4];
	if(!conn) { return; }

	string ip;
	ScopedFunctor([&conn] { conn.reset(); });
	const auto& setting = v6 ? SettingsManager::EXTERNAL_IP6 : SettingsManager::EXTERNAL_IP;

	if (!conn->buf.empty()) {
		try {
			const string pattern = !v6 ? "\\b(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\b" : "(\\A([0-9a-f]{1,4}:){1,1}(:[0-9a-f]{1,4}){1,6}\\Z)|(\\A([0-9a-f]{1,4}:){1,2}(:[0-9a-f]{1,4}){1,5}\\Z)|(\\A([0-9a-f]{1,4}:){1,3}(:[0-9a-f]{1,4}){1,4}\\Z)|(\\A([0-9a-f]{1,4}:){1,4}(:[0-9a-f]{1,4}){1,3}\\Z)|(\\A([0-9a-f]{1,4}:){1,5}(:[0-9a-f]{1,4}){1,2}\\Z)|(\\A([0-9a-f]{1,4}:){1,6}(:[0-9a-f]{1,4}){1,1}\\Z)|(\\A(([0-9a-f]{1,4}:){1,7}|:):\\Z)|(\\A:(:[0-9a-f]{1,4}){1,7}\\Z)|(\\A((([0-9a-f]{1,4}:){6})(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3})\\Z)|(\\A(([0-9a-f]{1,4}:){5}[0-9a-f]{1,4}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3})\\Z)|(\\A([0-9a-f]{1,4}:){5}:[0-9a-f]{1,4}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A([0-9a-f]{1,4}:){1,1}(:[0-9a-f]{1,4}){1,4}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A([0-9a-f]{1,4}:){1,2}(:[0-9a-f]{1,4}){1,3}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A([0-9a-f]{1,4}:){1,3}(:[0-9a-f]{1,4}){1,2}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A([0-9a-f]{1,4}:){1,4}(:[0-9a-f]{1,4}){1,1}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A(([0-9a-f]{1,4}:){1,5}|:):(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A:(:[0-9a-f]{1,4}){1,5}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)";
			const boost::regex reg(pattern);
			boost::match_results<string::const_iterator> results;
			// RSX++ workaround for msvc std lib problems
			string::const_iterator start = conn->buf.begin();
			string::const_iterator end = conn->buf.end();

			if(boost::regex_search(start, end, results, reg, boost::match_default)) {
				if(!results.empty()) {
					ip = results.str(0);
					//const string& ip = results.str(0);
					if (!manual)
						SettingsManager::getInstance()->set(setting, ip);
				}
			}
		} catch(...) { }
	}

	fire(UpdateManagerListener::SettingUpdated(), setting, ip);
}


void UpdateManager::checkGeoUpdate() {
	checkGeoUpdate(true);
	checkGeoUpdate(false);
}

void UpdateManager::checkGeoUpdate(bool v6) {
	// update when the database is non-existent or older than 25 days (GeoIP updates every month).
	try {
		File f(GeoManager::getDbPath(v6) + ".gz", File::READ, File::OPEN);
		if(f.getSize() > 0 && static_cast<time_t>(f.getLastModified()) > GET_TIME() - 3600 * 24 * 25) {
			return;
		}
	} catch(const FileException&) { }
	updateGeo(v6);
}

/*void UpdateManager::updateGeo() {
	if(SETTING(GET_USER_COUNTRY)) {
		updateGeo(true);
		updateGeo(false);
	} else {
		//dwt::MessageBox(this).show(T_("IP -> country mappings are disabled. Turn them back on via Settings > Appearance."),
			//_T(APPNAME) _T(" ") _T(VERSIONSTRING), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONEXCLAMATION);
	}
}*/

void UpdateManager::updateGeo(bool v6) {
	auto& conn = conns[v6 ? CONN_GEO_V6 : CONN_GEO_V4];
	if(conn)
		return;

	LogManager::getInstance()->message(STRING_F(GEOIP_UPDATING, (v6 ? "IPv6" : "IPv4")), LogManager::LOG_INFO);
	conn.reset(new HttpDownload(v6 ? links.geoip6 : links.geoip4,
		[this, v6] { completeGeoDownload(v6); }, false));
}

void UpdateManager::completeGeoDownload(bool v6) {
	auto& conn = conns[v6 ? CONN_GEO_V6 : CONN_GEO_V4];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if(!conn->buf.empty()) {
		try {
			File(GeoManager::getDbPath(v6) + ".gz", File::WRITE, File::CREATE | File::TRUNCATE).write(conn->buf);
			GeoManager::getInstance()->update(v6);
			LogManager::getInstance()->message(STRING_F(GEOIP_UPDATED, (v6 ? "IPv6" : "IPv4")), LogManager::LOG_INFO);
			return;
		} catch(const FileException&) { }
	}
	LogManager::getInstance()->message(STRING_F(GEOIP_UPDATING_FAILED, (v6 ? "IPv6" : "IPv4")), LogManager::LOG_WARNING);
}

void UpdateManager::completeLanguageDownload() {
	auto& conn = conns[CONN_LANGUAGE_FILE];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if(!conn->buf.empty()) {
		try {
			auto path = Localization::getCurLanguageFilePath();
			File::ensureDirectory(Util::getFilePath(path));
			File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(conn->buf);
			LogManager::getInstance()->message(STRING_F(LANGUAGE_UPDATED, Localization::getLanguageStr()), LogManager::LOG_INFO);
			fire(UpdateManagerListener::LanguageFinished());

			return;
		} catch(const FileException& e) { 
			LogManager::getInstance()->message(STRING_F(LANGUAGE_UPDATE_FAILED, Localization::getLanguageStr() % e.getError()), LogManager::LOG_WARNING);
		}
	}

	fire(UpdateManagerListener::LanguageFailed(), conn->status);
	LogManager::getInstance()->message(STRING_F(LANGUAGE_UPDATE_FAILED, Localization::getLanguageStr() % conn->status), LogManager::LOG_WARNING);
}

bool UpdateManager::getVersionInfo(SimpleXML& xml, string& versionString, int& remoteBuild) {
	while (xml.findChild("VersionInfo")) {
		//the latest OS must come first
		if (Util::toDouble(xml.getChildAttrib("MinOsVersion")) > Util::toDouble(Util::getOsVersion(false, true)))
			continue;

		xml.stepIn();

		if(xml.findChild("Version")) {
			versionString = xml.getChildData();
			xml.resetCurrentChild();
#ifdef BETAVER
			if(xml.findChild(UPGRADE_TAG)) {
				remoteBuild = Util::toInt(xml.getChildAttrib("Build"));
				versionString += "r" + Util::toString(remoteBuild);
			}
#else
			if (xml.findChild("Build")) {
				remoteBuild = Util::toInt(xml.getChildData());
			}
#endif
			xml.resetCurrentChild();
			return true;
		}
		break;
	}

	return false;
}

void UpdateManager::completeVersionDownload(bool manualCheck) {
	auto& conn = conns[CONN_VERSION];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if (conn->buf.empty()) {
		failUpdateDownload(STRING_F(DOWNLOAD_VERSION_FAILED, conn->status), manualCheck);
		return; 
	}

	bool verified = !versionSig.empty() && UpdateManager::verifyVersionData(conn->buf, versionSig);
	if(!verified) {
		failUpdateDownload(STRING(VERSION_VERIFY_FAILED), manualCheck);
	}

	try {
		SimpleXML xml;
		xml.fromXML(conn->buf);
		xml.stepIn();


		//Check for updated HTTP links
		if(xml.findChild("Links")) {
			xml.stepIn();
			if(xml.findChild("Homepage")) {
				links.homepage = xml.getChildData();
			}
			xml.resetCurrentChild();
			if(xml.findChild("Downloads")) {
				links.downloads = xml.getChildData();
			}
			xml.resetCurrentChild();
			if(xml.findChild("GeoIPv6")) {
				links.geoip6 = xml.getChildData();
			}
			xml.resetCurrentChild();
			if(xml.findChild("GeoIPv4")) {
				links.geoip4 = xml.getChildData();
			}
			xml.resetCurrentChild();
			if(xml.findChild("Customize")) {
				links.customize = xml.getChildData();
			}
			xml.resetCurrentChild();
			if(xml.findChild("Forum")) {
				links.discuss = xml.getChildData();
			}
			xml.resetCurrentChild();
			if(xml.findChild("Languages")) {
				links.language = xml.getChildData();
			}
			xml.resetCurrentChild();
			if(xml.findChild("Guides")) {
				links.guides = xml.getChildData();
			}
			xml.resetCurrentChild();
			if(xml.findChild("IPCheck")) {
				links.ipcheck4 = xml.getChildData();
			}
			xml.resetCurrentChild();
			if(xml.findChild("IPCheck6")) {
				links.ipcheck6 = xml.getChildData();
			}
			xml.stepOut();
		}
		xml.resetCurrentChild();


		int ownBuild = BUILD_NUMBER;
		string versionString;
		int remoteBuild = 0;

		if (getVersionInfo(xml, versionString, remoteBuild)) {

			//Get the update information from the XML
			string updateUrl;
			bool autoUpdateEnabled = false;
			if(xml.findChild(UPGRADE_TAG)) {
				updateUrl = xml.getChildData();
				updateTTH = xml.getChildAttrib("TTH");
				autoUpdateEnabled = (verified && xml.getIntChildAttrib("MinUpdateRev") <= ownBuild);
			}
			xml.resetCurrentChild();

			string url;
			if(xml.findChild("URL"))
				url = xml.getChildData();
			xml.resetCurrentChild();

			//Check for bad version
			auto reportBadVersion = [&] () -> void {
				string msg = xml.getChildAttrib("Message", "Your version of AirDC++ contains a serious bug that affects all users of the DC network or the security of your computer.");
				fire(UpdateManagerListener::BadVersion(), msg, url, updateUrl, remoteBuild, autoUpdateEnabled);
			};

			if(verified && xml.findChild("VeryOldVersion")) {
				if(Util::toInt(xml.getChildData()) >= ownBuild) {
					reportBadVersion();
					return;
				}
			}
			xml.resetCurrentChild();

			if(verified && xml.findChild("BadVersion")) {
				xml.stepIn();
				while(xml.findChild("BadVersion")) {
					double v = Util::toDouble(xml.getChildAttrib("Version"));
					if(v == ownBuild) {
						reportBadVersion();
						return;
					}
				}
			}
			xml.resetCurrentChild();


			//Check for updated version

			if((remoteBuild > ownBuild && remoteBuild > installedUpdate && Util::toDouble(versionString) >= Util::toDouble(VERSIONSTRING)) || manualCheck) {
				auto updateMethod = SETTING(UPDATE_METHOD);
				if ((!autoUpdateEnabled || updateMethod == UPDATE_PROMPT) || manualCheck) {
					if(xml.findChild("Title")) {
						const string& title = xml.getChildData();
						xml.resetCurrentChild();
						if(xml.findChild("Message")) {
							fire(UpdateManagerListener::UpdateAvailable(), title, xml.childToXML(), versionString, url, autoUpdateEnabled, remoteBuild, updateUrl);
						}
					}
					//fire(UpdateManagerListener::UpdateAvailable(), title, xml.getChildData(), Util::toString(remoteVer), url, true);
				} else if (updateMethod == UPDATE_AUTO) {
					LogManager::getInstance()->message(STRING_F(BACKGROUND_UPDATER_START, versionString), LogManager::LOG_INFO);
					downloadUpdate(updateUrl, remoteBuild, manualCheck);
				}
				xml.resetCurrentChild();
			}
		}
	} catch (const Exception& e) {
		failUpdateDownload(STRING_F(VERSION_PARSING_FAILED, e.getError()), manualCheck);
	}

	checkAdditionalUpdates(manualCheck);
}

void UpdateManager::checkAdditionalUpdates(bool manualCheck) {
	//v4
	if(!manualCheck && SETTING(IP_UPDATE) && !SETTING(AUTO_DETECT_CONNECTION) && SETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_DISABLED) {
		checkIP(false, false);
	}

	//v6
	if(!manualCheck && SETTING(IP_UPDATE6) && !SETTING(AUTO_DETECT_CONNECTION6) && SETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_DISABLED) {
		checkIP(false, true);
	}

	checkLanguage();

	if(SETTING(GET_USER_COUNTRY)) {
		checkGeoUpdate();
	}
}

bool UpdateManager::isUpdating() {
	return conns[CONN_CLIENT] ? true : false;
}

void UpdateManager::downloadUpdate(const string& aUrl, int newBuildID, bool manualCheck) {
	if(conns[CONN_CLIENT])
		return;

	conns[CONN_CLIENT].reset(new HttpDownload(aUrl,
		[this, newBuildID, manualCheck] { completeUpdateDownload(newBuildID, manualCheck); }, false));
}

void UpdateManager::checkLanguage() {
	if(!Localization::usingInbuiltLanguage() || links.language.empty()) {
		fire(UpdateManagerListener::LanguageFinished());
		return;
	}

	conns[CONN_LANGUAGE_CHECK].reset(new HttpDownload(links.language + "checkLangVersion.php?lc=" + Localization::getCurrentLocale(),
		[this] { completeLanguageCheck(); }, false));
}

void UpdateManager::completeLanguageCheck() {
	auto& conn = conns[CONN_LANGUAGE_CHECK];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if(!conn->buf.empty()) {
		if (Util::toDouble(conn->buf) > Localization::getCurLanguageVersion()) {
			fire(UpdateManagerListener::LanguageDownloading());
			conns[CONN_LANGUAGE_FILE].reset(new HttpDownload(links.language + Localization::getCurLanguageFileName(),
				[this] { completeLanguageDownload(); }, false));
		} else {
			fire(UpdateManagerListener::LanguageFinished());
		}
	} else {
		fire(UpdateManagerListener::LanguageFailed(), conn->status);
	}
}

void UpdateManager::checkVersion(bool aManual) {
	if (conns[CONN_SIGNATURE] || conns[CONN_VERSION] || conns[CONN_CLIENT]) {
		if (aManual) {
			fire(UpdateManagerListener::UpdateFailed(), STRING(ALREADY_UPDATING));
		}
		return;
	}

	versionSig.clear();
	conns[CONN_SIGNATURE].reset(new HttpDownload(static_cast<string>(VERSION_URL) + ".sign",
	//conns[CONN_VERSION] = make_unique<HttpDownload>("http://beta.airdcpp.net/testversion/version.xml.sign",
		[this, aManual] { completeSignatureDownload(aManual); }, false));
}

void UpdateManager::init(const string& aExeName) {
	exename = aExeName;

	checkVersion(false);

	/*if(SETTING(GET_USER_COUNTRY)) {
		GeoManager::getInstance()->init();
		checkGeoUpdate();
	} else {
		GeoManager::getInstance()->close();
	}*/
}

} // namespace dcpp
