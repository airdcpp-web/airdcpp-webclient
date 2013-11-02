/*
 * Copyright (C) 2012-2013 AirDC++ Project
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

#ifdef WIN32
#include "File.h"
#include "HashCalc.h"
#include "SimpleXML.h"
#include "Util.h"
#include "ZipFile.h"
#include "version.h"

#include <boost/shared_array.hpp>

#include <openssl/rsa.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#endif

#ifdef _WIN64
# define ARCH_STR "x64"
#else
# define ARCH_STR "x86"
#endif

namespace dcpp {

#ifdef _WIN32
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
	auto updaterFilePath = Util::getParentDir(Util::getAppName());
	string updaterFile = "updater_" ARCH_STR "_" + shortVersionString + ".zip";

	StringPairList files;
	ZipFile::CreateZipFileList(files, Util::getFilePath(Util::getAppName()), Util::emptyString, "^(AirDC.exe|AirDC.pdb)$");

	//add the theme folder
	auto installer = Util::getParentDir(updaterFilePath) + "installer" + PATH_SEPARATOR;
	ZipFile::CreateZipFileList(files, installer, Util::emptyString, "^(Themes)$");

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
					xml.replaceChildAttrib("VersionString", shortVersionString);
					xml.stepIn();
					xml.setData("http://builds.airdcpp.net/updater/" + updaterFile);

					File f(updaterFilePath + "version.xml", File::WRITE, File::CREATE | File::TRUNCATE);
					f.write(SimpleXML::utf8Header);
					f.write(xml.toXML());
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

#endif //WIN32

}