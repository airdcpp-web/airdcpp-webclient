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
#include <airdcpp/UpdateManager.h>
#include <airdcpp/UpdateDownloader.h>

#include <airdcpp/CryptoUtil.h>
#include <airdcpp/GeoManager.h>
#include <airdcpp/HashCalc.h>
#include <airdcpp/HttpDownload.h>
#include <airdcpp/Localization.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/ScopedFunctor.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/Text.h>
#include <airdcpp/TimerManager.h>
#include <airdcpp/version.h>

#include <airdcpp/pubkey.h>

#define IP_DB_EXPIRATION_DAYS 90 

namespace dcpp {

const char* UpdateManager::versionUrl[VERSION_LAST] = { 
	"https://version.airdcpp.net/version.xml",
	"https://beta.airdcpp.net/version/version.xml",
	"https://builds.airdcpp.net/version/version.xml"
};

UpdateManager::UpdateManager() : lastIPUpdate(GET_TICK()) {
	TimerManager::getInstance()->addListener(this);

	links.geoip = "http://geoip.airdcpp.net";
	links.ipcheck4 = "http://checkip.dyndns.org/";
	links.ipcheck6 = "http://checkip.dyndns.org/";
	links.language = "http://languages.airdcpp.net/tx/checkLangVersion.php?lc=%[locale]";

	SettingsManager::getInstance()->registerChangeHandler({
		SettingsManager::GET_USER_COUNTRY,
		SettingsManager::UPDATE_CHANNEL,
		SettingsManager::LANGUAGE_FILE
	}, [this](auto, auto aChangedSettings) {
		if (ranges::find(aChangedSettings, SettingsManager::UPDATE_CHANNEL) != aChangedSettings.end()) {
			UpdateManager::getInstance()->checkVersion(false);
		}
		
		if (ranges::find(aChangedSettings, SettingsManager::LANGUAGE_FILE) != aChangedSettings.end()) {
			UpdateManager::getInstance()->checkLanguage();
		}

		if (ranges::find(aChangedSettings, SettingsManager::GET_USER_COUNTRY) != aChangedSettings.end() && SETTING(GET_USER_COUNTRY)) {
			UpdateManager::getInstance()->checkGeoUpdate();
		}
	});
}

UpdateManager::~UpdateManager() { 
	TimerManager::getInstance()->removeListener(this);
}

void UpdateManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(UPDATER));
}

void UpdateManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	if (SETTING(UPDATE_IP_HOURLY) && lastIPUpdate + 60*60*1000 < aTick) {
		checkIP(false, false);
		lastIPUpdate = aTick;
	}
}

bool UpdateManager::verifyVersionData(const string& aVersionData, const ByteVector& aSignature) {
	auto digest = CryptoUtil::calculateSha1(aVersionData);
	if (!digest) {
		return false;
	}

	const uint8_t* key = UpdateManager::publicKey;
	auto keySize = sizeof(UpdateManager::publicKey);

	return CryptoUtil::verifyDigest(*digest, aSignature, key, keySize);
}

void UpdateManager::completeSignatureDownload(bool aManualCheck) {
	auto& conn = conns[CONN_SIGNATURE];
	ScopedFunctor([&conn] { conn.reset(); });

	if(conn->buf.empty()) {
		failVersionDownload(STRING_F(DOWNLOAD_SIGN_FAILED, conn->status), aManualCheck);
	} else {
		versionSig.assign(conn->buf.begin(), conn->buf.end());
	}

	conns[CONN_VERSION] = make_unique<HttpDownload>(
		getVersionUrl(),
		[this, aManualCheck] { completeVersionDownload(aManualCheck); }
	);
}

void UpdateManager::checkIP(bool aManual, bool v6) {
	HttpOptions options;
	options.setV4Only(!v6);

	conns[v6 ? CONN_IP6 : CONN_IP4] = make_unique<HttpDownload>(
		v6 ? links.ipcheck6 : links.ipcheck4,
		[this, aManual, v6] { completeIPCheck(aManual, v6); },
		options
	);
}

string UpdateManager::parseIP(const string& aText, bool v6) {
	const string pattern = !v6 ? 
		"\\b(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\b" : 
		"(\\A([0-9a-f]{1,4}:){1,1}(:[0-9a-f]{1,4}){1,6}\\Z)|(\\A([0-9a-f]{1,4}:){1,2}(:[0-9a-f]{1,4}){1,5}\\Z)|(\\A([0-9a-f]{1,4}:){1,3}(:[0-9a-f]{1,4}){1,4}\\Z)|(\\A([0-9a-f]{1,4}:){1,4}(:[0-9a-f]{1,4}){1,3}\\Z)|(\\A([0-9a-f]{1,4}:){1,5}(:[0-9a-f]{1,4}){1,2}\\Z)|(\\A([0-9a-f]{1,4}:){1,6}(:[0-9a-f]{1,4}){1,1}\\Z)|(\\A(([0-9a-f]{1,4}:){1,7}|:):\\Z)|(\\A:(:[0-9a-f]{1,4}){1,7}\\Z)|(\\A((([0-9a-f]{1,4}:){6})(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3})\\Z)|(\\A(([0-9a-f]{1,4}:){5}[0-9a-f]{1,4}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3})\\Z)|(\\A([0-9a-f]{1,4}:){5}:[0-9a-f]{1,4}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A([0-9a-f]{1,4}:){1,1}(:[0-9a-f]{1,4}){1,4}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A([0-9a-f]{1,4}:){1,2}(:[0-9a-f]{1,4}){1,3}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A([0-9a-f]{1,4}:){1,3}(:[0-9a-f]{1,4}){1,2}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A([0-9a-f]{1,4}:){1,4}(:[0-9a-f]{1,4}){1,1}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A(([0-9a-f]{1,4}:){1,5}|:):(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)|(\\A:(:[0-9a-f]{1,4}){1,5}:(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|[0-1]?\\d?\\d)){3}\\Z)";

	const boost::regex reg(pattern);
	boost::match_results<string::const_iterator> results;
	// RSX++ workaround for msvc std lib problems
	auto start = aText.begin();
	auto end = aText.end();

	if (boost::regex_search(start, end, results, reg, boost::match_default)) {
		if (!results.empty()) {
			return results.str(0);
		}
	}

	return Util::emptyString;
}

void UpdateManager::completeIPCheck(bool aManual, bool v6) {
	auto& conn = conns[v6 ? CONN_IP6 : CONN_IP4];
	if(!conn) { return; }

	string ip;
	ScopedFunctor([&conn] { conn.reset(); });
	const auto& setting = v6 ? SettingsManager::EXTERNAL_IP6 : SettingsManager::EXTERNAL_IP;

	if (!conn->buf.empty()) {
		try {
			ip = parseIP(conn->buf, v6);
			if (!aManual && !ip.empty()) {
				SettingsManager::getInstance()->set(setting, ip);
			}
		} catch(...) { }
	}

	fire(UpdateManagerListener::SettingUpdated(), setting, ip);
}

void UpdateManager::checkGeoUpdate() {
	// update when the database is non-existent or older than X days 
	try {
		File f(GeoManager::getDbPath() + ".gz", File::READ, File::OPEN);
		if(f.getSize() > 0 && f.getLastModified() > GET_TIME() - 3600 * 24 * IP_DB_EXPIRATION_DAYS) {
			return;
		}
	} catch(const FileException&) { }
	updateGeo();
}

void UpdateManager::updateGeo() {
	auto& conn = conns[CONN_GEO];
	if(conn)
		return;

	log(STRING(GEOIP_UPDATING), LogMessage::SEV_INFO);
	conn = make_unique<HttpDownload>(
		links.geoip,
		[this] { completeGeoDownload(); }
	);
}

void UpdateManager::completeGeoDownload() {
	auto& conn = conns[CONN_GEO];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if(!conn->buf.empty()) {
		try {
			File(GeoManager::getDbPath() + ".gz", File::WRITE, File::CREATE | File::TRUNCATE).write(conn->buf);
			GeoManager::getInstance()->update();
			log(STRING(GEOIP_UPDATED), LogMessage::SEV_INFO);
		} catch(const FileException& e) {
			log(STRING(GEOIP_UPDATING_FAILED) + " (" + e.what() + ")", LogMessage::SEV_WARNING);
		}
	} else {
		log(STRING(GEOIP_UPDATING_FAILED) + " (" + conn->status + ")", LogMessage::SEV_WARNING);
	}
}

void UpdateManager::completeLanguageDownload() {
	auto& conn = conns[CONN_LANGUAGE_FILE];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if(!conn->buf.empty()) {
		try {
			auto path = Localization::getCurLanguageFilePath();
			File::ensureDirectory(PathUtil::getFilePath(path));
			File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(conn->buf);
			log(STRING_F(LANGUAGE_UPDATED, Localization::getCurLanguageName()), LogMessage::SEV_INFO);
			fire(UpdateManagerListener::LanguageFinished());

			return;
		} catch(const FileException& e) { 
			log(STRING_F(LANGUAGE_UPDATE_FAILED, Localization::getCurLanguageName() % e.getError()), LogMessage::SEV_WARNING);
		}
	}

	fire(UpdateManagerListener::LanguageFailed(), conn->status);
	log(STRING_F(LANGUAGE_UPDATE_FAILED, Localization::getCurLanguageName() % conn->status), LogMessage::SEV_WARNING);
}

void UpdateManager::completeVersionDownload(bool aManualCheck) {
	auto& conn = conns[CONN_VERSION];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if (conn->buf.empty()) {
		failVersionDownload(STRING_F(DOWNLOAD_VERSION_FAILED, conn->status), aManualCheck);
		return; 
	}

	auto verified = !versionSig.empty() && verifyVersionData(conn->buf, versionSig);
	if (!verified) {
		failVersionDownload(STRING(VERSION_VERIFY_FAILED), aManualCheck);
	}

	try {
		SimpleXML xml;
		xml.fromXML(conn->buf);
		xml.stepIn();

		// Check for updated HTTP links
		if (xml.findChild("Links")) {
			xml.stepIn();

			if (xml.findChild("Languages")) {
				links.language = xml.getChildData();
			}
			xml.resetCurrentChild();
			if (xml.findChild("GeoIP")) {
				links.geoip = xml.getChildData();
			}
			xml.resetCurrentChild();
			if (xml.findChild("IPCheck")) {
				links.ipcheck4 = xml.getChildData();
			}
			xml.resetCurrentChild();
			if (xml.findChild("IPCheck6")) {
				links.ipcheck6 = xml.getChildData();
			}
			xml.stepOut();
		}
		xml.resetCurrentChild();

		fire(UpdateManagerListener::VersionFileDownloaded(), xml);
		updater->onVersionDownloaded(xml, verified, aManualCheck);
	} catch (const Exception& e) {
		failVersionDownload(STRING_F(VERSION_PARSING_FAILED, e.getError()), aManualCheck);
	}

	checkAdditionalUpdates(aManualCheck);
}

void UpdateManager::failVersionDownload(const string& aError, bool manualCheck) {
	auto msg = STRING_F(VERSION_CHECK_FAILED, aError);

	if (manualCheck) {
		log(msg, LogMessage::SEV_ERROR);
		fire(UpdateManagerListener::UpdateFailed(), msg);
	} else {
		log(msg, LogMessage::SEV_WARNING);
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

void UpdateManager::checkLanguage() {
	auto curLanguage = Localization::getCurrentLanguage();
	if (!curLanguage || curLanguage->isDefault() || links.language.empty()) {
		fire(UpdateManagerListener::LanguageFinished());
		return;
	}

	ParamMap params;
	params["locale"] = curLanguage->getLocale();

	auto url = Util::formatParams(links.language, params);
	conns[CONN_LANGUAGE_CHECK] = make_unique<HttpDownload>(
		Util::formatParams(links.language, params),
		[this] { completeLanguageCheck(); }
	);
}

void UpdateManager::completeLanguageCheck() {
	auto& conn = conns[CONN_LANGUAGE_CHECK];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if(!conn->buf.empty()) {
		if (Util::toDouble(conn->buf) > Localization::getCurLanguageVersion()) {
			fire(UpdateManagerListener::LanguageDownloading());
			conns[CONN_LANGUAGE_FILE] = make_unique<HttpDownload>(
				links.language + PathUtil::getFileName(Localization::getCurLanguageFilePath()),
				[this] { completeLanguageDownload(); }
			);
		} else {
			fire(UpdateManagerListener::LanguageFinished());
		}
	} else {
		fire(UpdateManagerListener::LanguageFailed(), conn->status);
	}
}

void UpdateManager::checkVersion(bool aManual) {
	if (conns[CONN_SIGNATURE] || conns[CONN_VERSION] || updater->isUpdating()) {
		if (aManual) {
			fire(UpdateManagerListener::UpdateFailed(), STRING(ALREADY_UPDATING));
		}
		return;
	}

	versionSig.clear();
	conns[CONN_SIGNATURE] = make_unique<HttpDownload>(
		getVersionUrl() + ".sign",
		[this, aManual] { completeSignatureDownload(aManual); }
	);
}

string UpdateManager::getVersionUrl() const {
	// always use the specific update channel for pre-release versions (don't use setDefault so that manually changed channel gets saved)
	return versionUrl[max(SETTING(UPDATE_CHANNEL), static_cast<int>(getVersionType()))];
}

void UpdateManager::init() {
	updater = make_unique<UpdateDownloader>(this);

	checkVersion(false);
}

} // namespace dcpp
