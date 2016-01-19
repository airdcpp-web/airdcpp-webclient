/*
 * Copyright (C) 2012-2016 AirDC++ Project
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
#include "Updater.h"

#include "File.h"
#include "HashCalc.h"
#include "LogManager.h"
#include "ScopedFunctor.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "UpdateManager.h"
#include "Util.h"
#include "ZipFile.h"
#include "version.h"

#include <boost/shared_array.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <openssl/rsa.h>
#include <openssl/objects.h>
#include <openssl/pem.h>

#ifdef _WIN64
# define ARCH_STR "x64"
#else
# define ARCH_STR "x86"
#endif

#ifdef _WIN64
# define UPGRADE_TAG "UpdateURLx64"
#else
# define UPGRADE_TAG "UpdateURL"
#endif

namespace dcpp {

bool Updater::extractFiles(const string& curSourcePath, const string& curExtractPath, string& error) {
	bool ret = true;
	File::ensureDirectory(curExtractPath);
	FileFindIter end;
	for(FileFindIter i(curSourcePath, "*"); i != end; ++i) {
		string name = i->getFileName();
		if(name == "." || name == "..")
			continue;

		if(i->isLink() || name.empty())
			continue;

		if(!i->isDirectory()) {
			try {
				if(Util::fileExists(curExtractPath + name))
					File::deleteFile(curExtractPath + name);
				File::copyFile(curSourcePath + name, curExtractPath + name);
			} catch(Exception& e) {
				error = e.getError() + " (" + curExtractPath + name + ")";
				return false; 
			}
		} else {
			ret = extractFiles(curSourcePath + name + PATH_SEPARATOR, curExtractPath + name + PATH_SEPARATOR, error);
			if(!ret) break;
		}
	}
	return ret;
}

bool Updater::applyUpdate(const string& sourcePath, const string& installPath, string& error) {
	bool ret = extractFiles(sourcePath, installPath, error);
	if (ret) {
		//update the version in the registry
		HKEY hk;
		TCHAR Buf[512];
		Buf[0] = 0;

	#ifdef _WIN64
		string regkey = "SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AirDC++\\";
		int flags = KEY_WRITE | KEY_QUERY_VALUE | KEY_WOW64_64KEY;
	#else
		string regkey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AirDC++\\";
		int flags = KEY_WRITE | KEY_QUERY_VALUE;
	#endif

		auto err = ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, Text::toT(regkey).c_str(), 0, flags, &hk);
		if(err == ERROR_SUCCESS) {
			DWORD bufLen = sizeof(Buf);
			DWORD type;
			::RegQueryValueEx(hk, _T("InstallLocation"), 0, &type, (LPBYTE)Buf, &bufLen);
			if(Util::stricmp(Text::toT(installPath).c_str(), Buf) == 0) {
				::RegSetValueEx(hk, _T("DisplayVersion"), 0, REG_SZ, (LPBYTE) Text::toT(shortVersionString).c_str(), sizeof(TCHAR) * (shortVersionString.length() + 1));
			}
			::RegCloseKey(hk);
		}
	}

	return ret;
}

void Updater::createUpdate() {
	auto updaterFilePath = Util::getParentDir(Util::getAppPath());
	string updaterFile = "updater_" ARCH_STR "_" + VERSIONSTRING + ".zip";

	StringPairList files;
	ZipFile::CreateZipFileList(files, Util::getAppFilePath(), Util::emptyString, "^(AirDC.exe|AirDC.pdb)$");

	//add the theme folder
	auto installer = Util::getParentDir(updaterFilePath) + "installer" + PATH_SEPARATOR;
	ZipFile::CreateZipFileList(files, installer, Util::emptyString, "^(Themes)$");
	//Add the web-resources
	ZipFile::CreateZipFileList(files, installer, Util::emptyString, "^(Web-resources)$");

	ZipFile::CreateZipFile(updaterFilePath + updaterFile, files);

	try {
		SimpleXML xml;
		xml.fromXML(File(updaterFilePath + "version.xml", File::READ, File::OPEN).read());
		if(xml.findChild("DCUpdate")) {
			xml.stepIn();
			if (xml.findChild("VersionInfo")) {
				xml.stepIn();
#ifdef _WIN64
				if(xml.findChild("UpdateURLx64")) {
#else
				if(xml.findChild("UpdateURL")) {
#endif
					xml.replaceChildAttrib("TTH", TTH(updaterFilePath + updaterFile));
					xml.replaceChildAttrib("Build", BUILD_NUMBER_STR);
					//xml.replaceChildAttrib("Commit", Util::toString(COMMIT_NUMBER));
					xml.replaceChildAttrib("VersionString", VERSIONSTRING);
					xml.stepIn();
					xml.setData("http://builds.airdcpp.net/updater/" + updaterFile);

					// Replace the line endings to use Unix format (it would be converted by the hosting provider anyway, which breaks the signature)
					auto content = SimpleXML::utf8Header;
					content += xml.toXML();
					boost::replace_all(content, "\r\n", "\n");

					File f(updaterFilePath + "version.xml", File::WRITE, File::CREATE | File::TRUNCATE);
					f.write(content);
					f.close();
				}
			}
		}
	} catch(const Exception& /*e*/) { }

	signVersionFile(updaterFilePath + "version.xml", updaterFilePath + "air_rsa", false);
}

void Updater::signVersionFile(const string& file, const string& key, bool makeHeader) {
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

Updater::Updater(UpdateManager* aUm) noexcept : um(aUm) {
	sessionToken = Util::toString(Util::rand());
}

void Updater::completeUpdateDownload(int buildID, bool manualCheck) {
	auto& conn = clientDownload;
	ScopedFunctor([&conn] { conn.reset(); });

	if (!conn->buf.empty()) {
		string updaterFile = UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR + "AirDC_Update.zip";
		ScopedFunctor([&updaterFile] { File::deleteFile(updaterFile); });

		try {
			File::removeDirectory(UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR);
			File::ensureDirectory(UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR);
			File(updaterFile, File::WRITE, File::CREATE | File::TRUNCATE).write(conn->buf);
		}
		catch (const FileException&) {
			failUpdateDownload(STRING(UPDATER_WRITE_FAILED), manualCheck);
			return;
		}

		// Check integrity
		if (TTH(updaterFile) != updateTTH) {
			failUpdateDownload(STRING(INTEGRITY_CHECK_FAILED), manualCheck);
			return;
		}

		// Unzip the update
		try {
			ZipFile zip;
			zip.Open(updaterFile);

			string srcPath = UPDATE_TEMP_DIR + sessionToken + PATH_SEPARATOR;
			string dstPath = Util::getAppFilePath();
			string updaterExeFile = srcPath + Util::getAppFileName();

			if (zip.GoToFirstFile()) {
				do {
					zip.OpenCurrentFile();
					if (zip.GetCurrentFileName().find(Util::getFileExt(updaterExeFile)) != string::npos) {
						zip.ReadCurrentFile(updaterExeFile);
					}
					else zip.ReadCurrentFile(srcPath);
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
			xml.addTag("UpdaterFile", updaterExeFile);
			xml.addTag("BuildID", buildID);
			xml.stepOut();

			File f(UPDATE_TEMP_DIR + "UpdateInfo_" + sessionToken + ".xml", File::WRITE, File::CREATE | File::TRUNCATE);
			f.write(SimpleXML::utf8Header);
			f.write(xml.toXML());
			f.close();

			LogManager::getInstance()->message(STRING(UPDATE_DOWNLOADED), LogMessage::SEV_INFO);
			installedUpdate = buildID;

			conn.reset(); //prevent problems when closing
			um->fire(UpdateManagerListener::UpdateComplete(), updaterExeFile);
		}
		catch (ZipFileException& e) {
			failUpdateDownload(e.getError(), manualCheck);
		}
	} else {
		failUpdateDownload(conn->status, manualCheck);
	}
}

bool Updater::checkPendingUpdates(const string& aDstDir, string& updater_, bool updated) {
	StringList fileList = File::findFiles(UPDATE_TEMP_DIR, "UpdateInfo_*");
	for (auto& uiPath : fileList) {
		if (Util::getFileExt(uiPath) == ".xml") {
			try {
				SimpleXML xml;
				xml.fromXML(File(uiPath, File::READ, File::OPEN).read());
				if (xml.findChild("UpdateInfo")) {
					xml.stepIn();
					if (xml.findChild("DestinationPath")) {
						xml.stepIn();
						string dstDir = xml.getData();
						xml.stepOut();

						if (dstDir != aDstDir)
							continue;

						if (xml.findChild("UpdaterFile")) {
							xml.stepIn();
							updater_ = xml.getData();
							xml.stepOut();

							if (xml.findChild("BuildID")) {
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
			}
			catch (const Exception& e) {
				LogManager::getInstance()->message(STRING_F(FAILED_TO_READ, uiPath % e.getError()), LogMessage::SEV_WARNING);
			}
		}
	}

	return false;
}

void Updater::failUpdateDownload(const string& aError, bool manualCheck) {
	auto msg = STRING_F(UPDATING_FAILED, aError);
	if (manualCheck) {
		LogManager::getInstance()->message(msg, LogMessage::SEV_ERROR);
		um->fire(UpdateManagerListener::UpdateFailed(), msg);
	} else {
		LogManager::getInstance()->message(msg, LogMessage::SEV_WARNING);
	}
}

bool Updater::onVersionDownloaded(SimpleXML& xml, bool aVerified, bool aManualCheck) {
	int ownBuild = BUILD_NUMBER;
	string versionString;
	int remoteBuild = 0;

	if (!getUpdateVersionInfo(xml, versionString, remoteBuild)) {
		return false;
	}


	//Get the update information from the XML
	string updateUrl;
	bool autoUpdateEnabled = false;
	if (xml.findChild(UPGRADE_TAG)) {
		updateUrl = xml.getChildData();
		updateTTH = xml.getChildAttrib("TTH");
		autoUpdateEnabled = (aVerified && xml.getIntChildAttrib("MinUpdateRev") <= ownBuild);
	}
	xml.resetCurrentChild();

	string url;
	if (xml.findChild("URL"))
		url = xml.getChildData();
	xml.resetCurrentChild();

	//Check for bad version
	auto reportBadVersion = [&]() -> void {
		string msg = xml.getChildAttrib("Message", "Your version of AirDC++ contains a serious bug that affects all users of the DC network or the security of your computer.");
		um->fire(UpdateManagerListener::BadVersion(), msg, url, updateUrl, remoteBuild, autoUpdateEnabled);
	};

	if (aVerified && xml.findChild("VeryOldVersion")) {
		if (Util::toInt(xml.getChildData()) >= ownBuild) {
			reportBadVersion();
			return false;
		}
	}
	xml.resetCurrentChild();

	if (aVerified && xml.findChild("BadVersions")) {
		xml.stepIn();
		while (xml.findChild("Version")) {
			xml.stepIn();
			double v = Util::toDouble(xml.getData());
			xml.stepOut();

			if (v == ownBuild) {
				reportBadVersion();
				return false;
			}
		}

		xml.stepOut();
	}
	xml.resetCurrentChild();


	//Check for updated version

	if ((remoteBuild > ownBuild && remoteBuild > installedUpdate) || aManualCheck) {
		auto updateMethod = SETTING(UPDATE_METHOD);
		if ((!autoUpdateEnabled || updateMethod == UPDATE_PROMPT) || aManualCheck) {
			if (xml.findChild("Title")) {
				const string& title = xml.getChildData();
				xml.resetCurrentChild();
				if (xml.findChild("Message")) {
					um->fire(UpdateManagerListener::UpdateAvailable(), title, xml.childToXML(), versionString, url, autoUpdateEnabled, remoteBuild, updateUrl);
				}
			}
			//fire(UpdateManagerListener::UpdateAvailable(), title, xml.getChildData(), Util::toString(remoteVer), url, true);
		} else if (updateMethod == UPDATE_AUTO) {
			LogManager::getInstance()->message(STRING_F(BACKGROUND_UPDATER_START, versionString), LogMessage::SEV_INFO);
			downloadUpdate(updateUrl, remoteBuild, aManualCheck);
		}
		xml.resetCurrentChild();
	}

	return true;
}

bool Updater::isUpdating() {
	return clientDownload ? true : false;
}

void Updater::downloadUpdate(const string& aUrl, int newBuildID, bool manualCheck) {
	if (clientDownload)
		return;

	clientDownload.reset(new HttpDownload(aUrl,
		[this, newBuildID, manualCheck] { completeUpdateDownload(newBuildID, manualCheck); }, false));
}

bool Updater::getUpdateVersionInfo(SimpleXML& xml, string& versionString, int& remoteBuild) {
	while (xml.findChild("VersionInfo")) {
		//the latest OS must come first
		StringTokenizer<string> t(xml.getChildAttrib("MinOsVersion"), '.');
		StringList& l = t.getTokens();

		if (!Util::IsOSVersionOrGreater(Util::toInt(l[0]), Util::toInt(l[1])))
			continue;

		xml.stepIn();

		if (xml.findChild("Version")) {
			versionString = xml.getChildData();
			xml.resetCurrentChild();
			if (xml.findChild(UPGRADE_TAG)) {
				remoteBuild = Util::toInt(xml.getChildAttrib("Build"));
				string tmp = xml.getChildAttrib("VersionString");
				if (!tmp.empty())
					versionString = tmp;
			}
			xml.resetCurrentChild();
			return true;
		}
		break;
	}

	return false;
}

void Updater::cleanTempFiles(const string& tmpPath) {
	FileFindIter end;
	for (FileFindIter i(tmpPath, "*"); i != end; ++i) {
		string name = i->getFileName();
		if (name == "." || name == "..")
			continue;

		if (i->isLink() || name.empty())
			continue;

		if (i->isDirectory()) {
			cleanTempFiles(tmpPath + name + PATH_SEPARATOR);
		}
		else {
			File::deleteFileEx(tmpPath + name, 3);
		}
	}

	// Remove the empty dir
	File::removeDirectory(tmpPath);
}

} // namespace dcpp