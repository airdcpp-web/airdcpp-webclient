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

#include "UpdaterCreator.h"

#include <airdcpp/UpdateConstants.h>

#include <airdcpp/format.h>

#include <airdcpp/AppUtil.h>
#include <airdcpp/CryptoManager.h>
#include <airdcpp/File.h>
#include <airdcpp/HashCalc.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/Util.h>
#include <airdcpp/ZipFile.h>
#include <airdcpp/ValueGenerator.h>
#include <airdcpp/version.h>

#include <boost/shared_array.hpp>
#include <boost/algorithm/string/replace.hpp>

#ifdef _WIN64
# define ARCH_STR "x64"
#else
# define ARCH_STR "x86"
#endif

#define UPDATER_LOCATION_BASE "https://builds.airdcpp.net/updater/"
#define UPDATER_FILE_NAME "updater_" ARCH_STR "_" + VERSIONSTRING + ".zip"
#define VERSION_FILE_NAME "version.xml"

namespace dcpp {

string UpdaterCreator::createUpdate(const FileListF& aFileListF) noexcept {
	auto updaterFilePath = PathUtil::getParentDir(AppUtil::getAppPath());

	// Create zip
	{
		StringPairList files;

		aFileListF(files, updaterFilePath);
		ZipFile::CreateZipFile(updaterFilePath + UPDATER_FILE_NAME, files);
	}

	// Update version file
	updateVersionFile(updaterFilePath);

	// Signature file
	signVersionFile(updaterFilePath + VERSION_FILE_NAME, updaterFilePath + "air_rsa", false);
	return updaterFilePath + UPDATER_FILE_NAME;
}

void UpdaterCreator::updateVersionFile(const string& aUpdaterPath) {
	try {
		SimpleXML xml;
		xml.fromXML(File(aUpdaterPath + VERSION_FILE_NAME, File::READ, File::OPEN).read());
		if(xml.findChild("DCUpdate")) {
			xml.stepIn();
			if (xml.findChild("VersionInfo")) {
				xml.stepIn();
				if(xml.findChild(UPGRADE_TAG)) {
					xml.replaceChildAttrib("TTH", TTH(aUpdaterPath + UPDATER_FILE_NAME));
					xml.replaceChildAttrib("Build", BUILD_NUMBER_STR);
					xml.replaceChildAttrib("VersionString", VERSIONSTRING);
					xml.stepIn();
					xml.setData(UPDATER_LOCATION_BASE UPDATER_FILE_NAME);

					// Replace the line endings to use Unix format (it would be converted by the hosting provider anyway, which breaks the signature)
					auto content = SimpleXML::utf8Header;
					content += xml.toXML();
					boost::replace_all(content, "\r\n", "\n");

					File f(aUpdaterPath + VERSION_FILE_NAME, File::WRITE, File::CREATE | File::TRUNCATE);
					f.write(content);
				}
			}
		}
	} catch(const Exception& /*e*/) { }
}

// Deprecations, TODO: fix
# pragma warning(disable: 4996) 
void UpdaterCreator::signVersionFile(const string& aVersionFilePath, const string& aPrivateKeyFilePath, bool aMakeHeader) {
	string versionData;
	unsigned int sig_len = 0;

	RSA* rsa = RSA_new();

	try {
		// Read All Data from files
		{
			File versionFile(aVersionFilePath, File::READ, File::OPEN);
			versionData = versionFile.read();
		}

		FILE* f = fopen(aPrivateKeyFilePath.c_str(), "r");
		PEM_read_RSAPrivateKey(f, &rsa, NULL, NULL);
		fclose(f);
	} catch(const FileException&) { return; }

#ifdef _WIN32
	if (versionData.find("\r\n") != string::npos) {
		::MessageBox(NULL, _T("The version file contains Windows line endings. UNIX endings should be used instead."), _T(""), MB_OK | MB_ICONERROR);
		return;
	}
#endif

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
	auto sig = boost::shared_array<uint8_t>(new uint8_t[RSA_size(rsa)]);
	RSA_sign(NID_sha1, digest, sizeof(digest), sig.get(), &sig_len, rsa);

	if (sig_len > 0) {
		string c_key = Util::emptyString;

		if (aMakeHeader) {
			int buf_size = i2d_RSAPublicKey(rsa, 0);
			auto buf = boost::shared_array<uint8_t>(new uint8_t[buf_size]);

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
			{
				File outSig(aVersionFilePath + ".sign", File::WRITE, File::TRUNCATE | File::CREATE);
				outSig.write(sig.get(), sig_len);
			}

			if (!c_key.empty()) {
				// Write the public key header (openssl probably has something to generate similar file, but couldn't locate it)
				File pubKey(PathUtil::getFilePath(aVersionFilePath) + "pubkey.h", File::WRITE, File::TRUNCATE | File::CREATE);
				pubKey.write(c_key);
			}
		} catch(const FileException&) { }
	}

	if (rsa) {
		RSA_free(rsa);
		rsa = NULL;
	}
}

} // namespace dcpp