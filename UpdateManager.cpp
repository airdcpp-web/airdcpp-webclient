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

#include <boost/shared_array.hpp>
#include <boost/regex.hpp>

#include <openssl/rsa.h>
#include <openssl/objects.h>
#include <openssl/pem.h>

#include "TimerManager.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "LogManager.h"

#include "AirUtil.h"
#include "GeoManager.h"
#include "ScopedFunctor.h"
#include "Localization.h"

//#include "ZipFile.h"
#include "SimpleXML.h"
//#include "HashCalc.h"

//#include "pubkey.h"

#ifdef _WIN64
# define UPGRADE_TAG "UpdateURLx64"
#else
# define UPGRADE_TAG "UpdateURL"
#endif

#define APP_AUTH_KEY TTH(SHA1(SETTING(BETAUSR) + " - " + MD5(BOOST_STRINGIZE(BUILDID))))

namespace dcpp {

/*void UpdateManager::signVersionFile(const string& file, const string& key, bool makeHeader) {
	string versionData;
	unsigned int sig_len = 0;

	RSA* rsa = RSA_new();

	try {
		// Read All Data from files
		File versionFile(file, File::READ,  File::OPEN);
		versionData = versionFile.read();
		versionFile.close();

		FILE* f = fopen(key.c_str(), "r");
		PEM_read_RSAPrivateKey(f, &rsa, NULL, NULL);
		fclose(f);
	} catch(const FileException&) { return; }

	// Make SHA hash
	int res = -1;
	SHA_CTX sha_ctx = { 0 };
	uint8_t digest[SHA_DIGEST_LENGTH];

	res = SHA1_Init(&sha_ctx);
	if(res != 1)
		return;
	res = SHA1_Update(&sha_ctx, versionData.c_str(), versionData.size());
	if(res != 1)
		return;
	res = SHA1_Final(digest, &sha_ctx);
	if(res != 1)
		return;

	// sign hash
	boost::shared_array<uint8_t> sig = boost::shared_array<uint8_t>(new uint8_t[RSA_size(rsa)]);
	RSA_sign(NID_sha1, digest, sizeof(digest), sig.get(), &sig_len, rsa);

	if(sig_len > 0) {
		string c_key = Util::emptyString;

		if(makeHeader) {
			int buf_size = i2d_RSAPublicKey(rsa, 0);
			boost::shared_array<uint8_t> buf = boost::shared_array<uint8_t>(new uint8_t[buf_size]);

			{
				uint8_t* buf_ptr = buf.get();
				i2d_RSAPublicKey(rsa, &buf_ptr);
			}

			c_key = "// Automatically generated file, DO NOT EDIT!" NATIVE_NL NATIVE_NL;
			c_key += "#ifndef PUBKEY_H" NATIVE_NL "#define PUBKEY_H" NATIVE_NL NATIVE_NL;

			c_key += "uint8_t dcpp::UpdateManager::publicKey[] = { " NATIVE_NL "\t";
			for(int i = 0; i < buf_size; ++i) {
				c_key += (dcpp_fmt("0x%02X") % (unsigned int)buf[i]).str();
				if(i < buf_size - 1) {
					c_key += ", ";
					if((i+1) % 15 == 0) c_key += NATIVE_NL "\t";
				} else c_key += " " NATIVE_NL "};" NATIVE_NL NATIVE_NL;	
			}

			c_key += "#endif // PUBKEY_H" NATIVE_NL;
		}

		try {
			// Write signature file
			File outSig(file + ".sign", File::WRITE, File::TRUNCATE | File::CREATE);
			outSig.write(sig.get(), sig_len);
			outSig.close();

			if(!c_key.empty()) {
				// Write the public key header (openssl probably has something to generate similar file, but couldn't locate it)
				File pubKey(Util::getFilePath(file) + "pubkey.h", File::WRITE, File::TRUNCATE | File::CREATE);
				pubKey.write(c_key);
				pubKey.close();
			}
		} catch(const FileException&) { }
	}

	if(rsa) {
		RSA_free(rsa);
		rsa = NULL;
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

void UpdateManager::updateIP(const string& aServer) {
	HttpManager::getInstance()->addDownload(aServer, boost::bind(&UpdateManager::updateIP, this, _1, _2, _3), false);
}

bool UpdateManager::applyUpdate(const string& sourcePath, const string& installPath) {
	bool ret = true;
	FileFindIter end;
#ifdef _WIN32
	for(FileFindIter i(sourcePath + "*"); i != end; ++i) {
#else
	for(FileFindIter i(sourcePath); i != end; ++i) {
#endif
		string name = i->getFileName();
		if(name == "." || name == "..")
			continue;

		if(i->isLink() || name.empty())
			continue;

		if(!i->isDirectory()) {
			try {
				if(Util::fileExists(installPath + name))
					File::deleteFile(installPath + name);
				File::copyFile(sourcePath + name, installPath + name);
			} catch(Exception&) { return false; }
		} else {
			ret = UpdateManager::applyUpdate(sourcePath + name + PATH_SEPARATOR, installPath + name + PATH_SEPARATOR);
			if(!ret) break;
		}
	}

	return ret;
}

void UpdateManager::cleanTempFiles(const string& tmpPath) {
	FileFindIter end;
#ifdef _WIN32
	for(FileFindIter i(tmpPath + "*"); i != end; ++i) {
#else
	for(FileFindIter i(tmpPath); i != end; ++i) {
#endif
		string name = i->getFileName();
		if(name == "." || name == "..")
			continue;

		if(i->isLink() || name.empty())
			continue;

		if(i->isDirectory()) {
			UpdateManager::cleanTempFiles(tmpPath + name + PATH_SEPARATOR);
		} else File::deleteFile(tmpPath + name);
	}

	// Remove the empty dir
	//File::removeDirectory(tmpPath);
}

void UpdateManager::checkUpdates(const string& aUrl, bool bManual) {
	if(bManual && manualCheck)
		return;

	time_t curTime = GET_TIME();
	if(bManual || ((curTime - SETTING(LAST_UPDATE_NOTICE)) > 60*60*24 || SETTING(LAST_UPDATE_NOTICE) > curTime)) {
		manualCheck = bManual;
		versionUrl = aUrl;

		HttpManager::getInstance()->addFileDownload(aUrl + ".sign", UPDATE_TEMP_DIR "version.sign", boost::bind(&UpdateManager::versionSignature, this, _1, _2, _3), false);
	}
}

void UpdateManager::downloadUpdate(const string& aUrl, const string& aExeName) {
	if(updating)
		return;

	updating = true;
	exename = aExeName;

	HttpManager::getInstance()->addFileDownload(aUrl, UPDATE_TEMP_DIR "Apex_Update.zip", boost::bind(&UpdateManager::updateDownload, this, _1, _2, _3), false);
}

void UpdateManager::updateDownload(const HttpConnection*, const string& aLine, uint8_t stFlags) {
	bool success = ((stFlags & HttpManager::HTTP_COMPLETE) == HttpManager::HTTP_COMPLETE);
	if(success && Util::fileExists(UPDATE_TEMP_DIR "Apex_Update.zip")) {
		// Check integrity
		if(TTH(UPDATE_TEMP_DIR "Apex_Update.zip") != updateTTH) {
			updating = false;
			File::deleteFile(UPDATE_TEMP_DIR "Apex_Update.zip");
			fire(UpdateListener::UpdateFailed(), "File integrity check failed");
			return;
		}

		// Unzip the update
		try {
			ZipFile zip;
			zip.Open(UPDATE_TEMP_DIR "Apex_Update.zip");

			string srcPath = UPDATE_TEMP_DIR + updateTTH + PATH_SEPARATOR;
			string dstPath = Util::getFilePath(exename);
			string exeFile = srcPath + Util::getFileName(exename);

#ifdef _WIN32
			if(srcPath[srcPath.size() - 1] == PATH_SEPARATOR)
				srcPath.insert(srcPath.size() - 1, "\\");

			if(dstPath[dstPath.size() - 1] == PATH_SEPARATOR)
				dstPath.insert(dstPath.size() - 1, "\\");
#endif

			if(zip.GoToFirstFile()) {
				do {
					zip.OpenCurrentFile();
					if(zip.GetCurrentFileName().find(Util::getFileExt(exename)) != string::npos) {
						zip.ReadCurrentFile(exeFile);
					} else zip.ReadCurrentFile(srcPath);
					zip.CloseCurrentFile();
				} while(zip.GoToNextFile());
			}

			zip.Close();

			File::deleteFile(UPDATE_TEMP_DIR "Apex_Update.zip");
			fire(UpdateListener::UpdateComplete(), exeFile, "/update \"" + srcPath + "\" \"" + dstPath + "\"");
		} catch(ZipFileException& e) {
			updating = false;
			File::deleteFile(UPDATE_TEMP_DIR "Apex_Update.zip");
			fire(UpdateListener::UpdateFailed(), e.getError());
		}
	} else {
		updating = false;
		File::deleteFile(UPDATE_TEMP_DIR "Apex_Update.zip");
		fire(UpdateListener::UpdateFailed(), aLine);
	}
}
void UpdateManager::versionSignature(const HttpConnection*, const string& aLine, uint8_t stFlags) {
	if((stFlags & HttpManager::HTTP_FAILED) == HttpManager::HTTP_FAILED) {
		LogManager::getInstance()->message("Could not download digital signature for update check (" + aLine + ")", LogManager::LOG_WARNING);
		manualCheck = false;
		return;
	}

	try {
		File sigFile(UPDATE_TEMP_DIR "version.sign", File::READ, File::OPEN);
		size_t sig_size = static_cast<size_t>(sigFile.getSize());
		versionSig.resize(sig_size);

		sigFile.read(&versionSig[0], sig_size);
		versionSig.resize(sig_size);

		sigFile.close();
	} catch(const FileException& e) {
		LogManager::getInstance()->message("Could not download digital signature for update check (" + e.getError() + ")", LogManager::LOG_WARNING);
		File::deleteFile(UPDATE_TEMP_DIR "version.sign");
		manualCheck = false;
		return;
	}

	HttpManager::getInstance()->addDownload(versionUrl, boost::bind(&UpdateManager::versionCheck, this, _1, _2, _3), false);
	File::deleteFile(UPDATE_TEMP_DIR "version.sign");
}*/

/*void UpdateManager::versionCheck(const HttpConnection*, const string& versionInfo, uint8_t stFlags) {
	if((stFlags & HttpManager::HTTP_FAILED) == HttpManager::HTTP_FAILED) {
		LogManager::getInstance()->message("Could not connect to the update server (" + versionInfo + ")", LogManager::LOG_WARNING);
		manualCheck = false;
		return;
	}

	if(!UpdateManager::verifyVersionData(versionInfo, versionSig)) {
		LogManager::getInstance()->message("Could not verify version data", LogManager::LOG_WARNING);
		manualCheck = false;
		return;
	}

	try {
		SimpleXML xml;
		xml.fromXML(versionInfo);
		xml.stepIn();

		string updateUrl;
		bool updateEnabled = false;
		if(xml.findChild(UPGRADE_TAG)) {
			updateUrl = xml.getChildData();
			updateTTH = xml.getChildAttrib("TTH");
			updateEnabled = xml.getChildAttrib("Enabled", "1") == "1";
		}
		xml.resetCurrentChild();

		string url;
		if(xml.findChild("URL"))
			url = xml.getChildData();
		xml.resetCurrentChild();

		if(xml.findChild("VeryOldVersion")) {
			if(Util::toDouble(xml.getChildData()) >= BUILDID) {
				string msg = xml.getChildAttrib("Message", "Your version of ApexDC++ contains a serious bug that affects all users of the DC network or the security of your computer.");
				fire(UpdateManagerListener::BadVersion(), msg, url, updateUrl);
				return;
			}
		}
		xml.resetCurrentChild();

		if(xml.findChild("BadVersion")) {
			xml.stepIn();
			while(xml.findChild("BadVersion")) {
				double v = Util::toDouble(xml.getChildAttrib("Version"));
				if(v == BUILDID) {
					string msg = xml.getChildAttrib("Message", "Your version of ApexDC++ contains a serious bug that affects all users of the DC network or the security of your computer.");
					fire(UpdateManagerListener::BadVersion(), msg, url, updateUrl);
					return;
				}
			}
			xml.stepOut();
		}
		xml.resetCurrentChild();

		if(xml.findChild("BuildID")) {
			const string& buildId = xml.getChildData();
			xml.resetCurrentChild();

			if(Util::toDouble(buildId) > BUILDID || manualCheck) {
				string title;
				if(xml.findChild("Title"))
					title = xml.getChildData();
				xml.resetCurrentChild();

				string version;
				if(xml.findChild("Version"))
					version = xml.getChildData() + "." + buildId;
				xml.resetCurrentChild();

				if(xml.findChild("Message")) {
					SettingsManager::getInstance()->set(SettingsManager::LAST_UPDATE_NOTICE, GET_TIME());
					url = updateEnabled ? updateUrl : url;

					fire(UpdateManagerListener::UpdateAvailable(), title, xml.getChildData(), version, url, updateEnabled);
				}
				xml.resetCurrentChild();
			}
		}

		xml.stepOut();
	} catch (const Exception& e) {
		LogManager::getInstance()->message("Could not parse update info (" + e.getError() + ")", LogManager::LOG_WARNING);
	}

	manualCheck = false;
}

void UpdateManager::updateIP(const HttpConnection*, const string& ipData, uint8_t stFlags) {
	// Not interested in failures really
	//if((stFlags & HttpManager::HTTP_FAILED) == HttpManager::HTTP_FAILED)
	//	return;

	try {
		const string pattern = "\\b(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\b";
		const boost::regex reg(pattern, boost::regex_constants::icase);
		boost::match_results<string::const_iterator> results;
		// RSX++ workaround for msvc std lib problems
		string::const_iterator start = ipData.begin();
		string::const_iterator end = ipData.end();

		if(boost::regex_search(start, end, results, reg, boost::match_default)) {
			if(!results.empty()) {
				const string& ip = results.str(0);
				SettingsManager::getInstance()->set(SettingsManager::EXTERNAL_IP, ip);
				fire(UpdateManagerListener::SettingUpdated(), SettingsManager::EXTERNAL_IP, ip);
			}
		}
	} catch(...) { }
}*/


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

void UpdateManager::updateGeo() {
	if(BOOLSETTING(GET_USER_COUNTRY)) {
		updateGeo(true);
		updateGeo(false);
	} else {
		//dwt::MessageBox(this).show(T_("IP -> country mappings are disabled. Turn them back on via Settings > Appearance."),
			//_T(APPNAME) _T(" ") _T(VERSIONSTRING), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONEXCLAMATION);
	}
}

void UpdateManager::updateGeo(bool v6) {
	auto& conn = conns[v6 ? CONN_GEO_V6 : CONN_GEO_V4];
	if(conn)
		return;

	LogManager::getInstance()->message(str(boost::format("Updating the %1% GeoIP database...") % (v6 ? "IPv6" : "IPv4")), LogManager::LOG_INFO);
	conn.reset(new HttpDownload(v6 ? links.geoip6 : links.geoip4,
		[this, v6] { completeGeoUpdate(v6); }, false));
}

void UpdateManager::completeGeoUpdate(bool v6) {
	auto& conn = conns[v6 ? CONN_GEO_V6 : CONN_GEO_V4];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if(!conn->buf.empty()) {
		try {
			File(GeoManager::getDbPath(v6) + ".gz", File::WRITE, File::CREATE | File::TRUNCATE).write(conn->buf);
			GeoManager::getInstance()->update(v6);
			LogManager::getInstance()->message(str(boost::format("The %1% GeoIP database has been successfully updated") % (v6 ? "IPv6" : "IPv4")), LogManager::LOG_INFO);
			return;
		} catch(const FileException&) { }
	}
	LogManager::getInstance()->message(str(boost::format("The %1% GeoIP database could not be updated") % (v6 ? "IPv6" : "IPv4")), LogManager::LOG_WARNING);
}

void UpdateManager::completeLanguageUpdate() {
	auto& conn = conns[CONN_LANGUAGE];
	if(!conn) { return; }
	ScopedFunctor([&conn] { conn.reset(); });

	if(!conn->buf.empty()) {
		try {
			auto path = Localization::getCurLanguageFilePath();
			File::ensureDirectory(Util::getFilePath(path));
			File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(conn->buf);
			LogManager::getInstance()->message(str(boost::format("The language %1% has been successfully updated. The new language will take effect after restarting the client.") 
				% Localization::getLanguageStr()), LogManager::LOG_INFO);

			return;
		} catch(const FileException&) { }
	}
	LogManager::getInstance()->message(str(boost::format("The language %1% could not be updated") % Localization::getLanguageStr()), LogManager::LOG_WARNING);
}


void UpdateManager::completeVersionUpdate() {

	if(!conns[CONN_VERSION]) { return; }
	try {
		SimpleXML xml;
		xml.fromXML(conns[CONN_VERSION]->buf);
		xml.stepIn();

		string url;
#		ifdef _WIN64
			if(xml.findChild("URL64")) {
			url = xml.getChildData();
		}
#		else
		if(xml.findChild("URL")) {
			url = xml.getChildData();
		}
#		endif

		//ApexDC
		xml.resetCurrentChild();
		if(!BOOLSETTING(AUTO_DETECT_CONNECTION)) {
			if(BOOLSETTING(IP_UPDATE) && xml.findChild("IP")) {
				string ip = xml.getChildData();
				SettingsManager::getInstance()->set(SettingsManager::EXTERNAL_IP, (!ip.empty() ? ip : AirUtil::getLocalIp()));
			} else if(BOOLSETTING(IP_UPDATE)) {
				SettingsManager::getInstance()->set(SettingsManager::EXTERNAL_IP, AirUtil::getLocalIp());
			}
			xml.resetCurrentChild();
		}

		if(xml.findChild("Version")) {
			double remoteVer = Util::toDouble(xml.getChildData());
			xml.resetCurrentChild();
			double ownVersion = Util::toDouble(VERSIONFLOAT);

#ifdef SVNVERSION
			if (xml.findChild("SVNrev")) {
				remoteVer = Util::toDouble(xml.getChildData());
				xml.resetCurrentChild();
			}
			string tmp = SVNVERSION;
			ownVersion = Util::toDouble(tmp.substr(1, tmp.length()-1));
#endif

			if(remoteVer > ownVersion) {
				xml.resetCurrentChild();
				if(xml.findChild("Title")) {
					const string& title = xml.getChildData();
					xml.resetCurrentChild();
					if(xml.findChild("Message")) {
						if(url.empty()) {
							//const string& msg = xml.getChildData();
							//MessageBox(Text::toT(msg).c_str(), Text::toT(title).c_str(), MB_OK);
						} else {
							//string msg = xml.getChildData() + "\r\n" + STRING(OPEN_DOWNLOAD_PAGE);
							fire(UpdateManagerListener::UpdateAvailable(), title, xml.getChildData(), Util::toString(remoteVer), url, true);
						}
					}
				}
				xml.resetCurrentChild();

				if(xml.findChild("VeryOldVersion")) {
					if(Util::toDouble(xml.getChildData()) >= Util::toDouble(VERSIONFLOAT)) {
						string msg = xml.getChildAttrib("Message", "Your version of AirDC++ contains a serious bug that affects all users of the DC network or the security of your computer.");
						fire(UpdateManagerListener::BadVersion(), msg, url, Util::emptyString);

						/*string msg = xml.getChildAttrib("Message", "Your version of AirDC++ contains a serious bug that affects all users of the DC network or the security of your computer.");
						MessageBox(Text::toT(msg + "\r\nPlease get a new one at " + url).c_str());
						oldshutdown = true;
						PostMessage(WM_CLOSE);*/
					}
				}
				xml.resetCurrentChild();

				if(xml.findChild("BadVersion")) {
					xml.stepIn();
					while(xml.findChild("BadVersion")) {
						double v = Util::toDouble(xml.getChildAttrib("Version"));
						if(v == Util::toDouble(VERSIONFLOAT)) {
							string msg = xml.getChildAttrib("Message", "Your version of AirDC++ contains a serious bug that affects all users of the DC network or the security of your computer.");
							fire(UpdateManagerListener::BadVersion(), msg, url, Util::emptyString);

							/*string msg = xml.getChildAttrib("Message", "Your version of DC++ contains a serious bug that affects all users of the DC network or the security of your computer.");
							MessageBox(Text::toT(msg + "\r\nPlease get a new one at " + url).c_str(), _T("Bad DC++ version"), MB_OK | MB_ICONEXCLAMATION);
							oldshutdown = true;
							PostMessage(WM_CLOSE);*/
						}
					}
				}
			} 
			
			if(!SETTING(LANGUAGE_FILE).empty()) {
				if (xml.findChild(Localization::getCurLanguageFileName())) {
					string langVersion = xml.getChildData();
					//double LangVersion = Util::toDouble(version);
					xml.resetCurrentChild();
					if (xml.findChild("LANGURL")) {
						const auto& langUrl = xml.getChildData();
						if (Util::toDouble(langVersion) > Localization::getCurLanguageVersion()){
							updateLanguage(langUrl + Localization::getCurLanguageFileName());
						}
						xml.resetCurrentChild();
					}
				}
			}
		}
	} catch (const Exception&) {
		// ...
	}

	conns[CONN_VERSION].reset();

	// check after the version.xml download in case it contains updated GeoIP links.
	/*if(BOOLSETTING(GET_USER_COUNTRY)) {
		checkGeoUpdate();
	} */
}

void UpdateManager::checkVersion() {
	conns[CONN_VERSION].reset(new HttpDownload(VERSION_URL,
		[this] { completeVersionUpdate(); }, false));
}

void UpdateManager::updateLanguage(const string& aUrl) {
	conns[CONN_LANGUAGE].reset(new HttpDownload(aUrl,
		[this] { completeLanguageUpdate(); }, false));
}

void UpdateManager::init() {
	links.homepage = "http://www.airdcpp.net/";
	links.downloads = links.homepage + "download/";
	links.geoip6 = "http://geoip6.airdcpp.net";
	links.geoip4 = "http://geoip4.airdcpp.net";
	links.guides = links.homepage + "guides/";
	links.customize = links.homepage + "c/customizations/";
	links.discuss = links.homepage + "forum/";

	checkVersion();

	auto prevGeo = BOOLSETTING(GET_USER_COUNTRY);
	auto prevGeoFormat = SETTING(COUNTRY_FORMAT);

	bool rebuildGeo = prevGeo && SETTING(COUNTRY_FORMAT) != prevGeoFormat;
	//if(BOOLSETTING(GET_USER_COUNTRY) != prevGeo) {
		if(BOOLSETTING(GET_USER_COUNTRY)) {
			GeoManager::getInstance()->init();
			checkGeoUpdate();
		} else {
			GeoManager::getInstance()->close();
			rebuildGeo = false;
		}
	//}
	if(rebuildGeo) {
		GeoManager::getInstance()->rebuild();
	}
}

} // namespace dcpp
