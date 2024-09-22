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
#include <airdcpp/CryptoUtil.h>
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

string UpdaterCreator::createUpdate(const FileListF& aFileListF, const ErrorF& aErrorF) noexcept {
	auto updaterFilePath = PathUtil::getParentDir(AppUtil::getAppPath());

	// Create zip
	{
		StringPairList files;

		aFileListF(files, updaterFilePath);
		ZipFile::CreateZipFile(updaterFilePath + UPDATER_FILE_NAME, files);
	}

	// Update version file
	if (!updateVersionFile(updaterFilePath, aErrorF)) {
		return Util::emptyString;
	}

	// Signature file
	if (!signVersionFile(updaterFilePath + VERSION_FILE_NAME, updaterFilePath + "air_rsa", aErrorF, false)) {
		return Util::emptyString;
	}

	return updaterFilePath + UPDATER_FILE_NAME;
}

bool UpdaterCreator::updateVersionFile(const string& aUpdaterPath, const ErrorF& aErrorF) {
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
					return true;
				}
			}
		}
	} catch(const Exception& /*e*/) { 
		aErrorF("Failed to update version.xml");
		return false;
	}

	aErrorF("Invalid version.xml content");
	return false;
}

optional<ByteVector> UpdaterCreator::calculateFileSha1(const string& aVersionFilePath, const ErrorF& aErrorF) {
	string versionData;
	try {
		// Read All Data from files
		{
			File versionFile(aVersionFilePath, File::READ, File::OPEN);
			versionData = versionFile.read();
		}
	} catch (const FileException&) { return nullopt; }

	if (versionData.find("\r\n") != string::npos) {
		aErrorF("The version file contains Windows line endings. UNIX endings should be used instead.");
		return nullopt;
	}

	return CryptoUtil::calculateSha1(aVersionFilePath);
}

bool UpdaterCreator::signVersionFile(const string& aVersionFilePath, const string& aPrivateKeyFilePath, const ErrorF& aErrorF, bool aMakeHeader) {
	auto versionSha1 = calculateFileSha1(aVersionFilePath, aErrorF);
	if (!versionSha1) {
		aErrorF("Could not generate version SHA1 hash");
		return false;
	}

	// Sign
	auto signatureData = CryptoUtil::signDigest(*versionSha1, aPrivateKeyFilePath);
	if (!signatureData) {
		aErrorF("Could not create signature");
		return false;
	}

	auto& [sig, publicKey] = *signatureData;

	// Write signature file
	try {
		File outSig(aVersionFilePath + ".sign", File::WRITE, File::TRUNCATE | File::CREATE);
		outSig.write(sig.data(), sig.size());
	} catch(const FileException&) {
		aErrorF("Could not write private key");
		return false;
	}

	// Assertion
	auto isValid = CryptoUtil::verifyDigest(*versionSha1, sig, publicKey.data(), publicKey.size());
	if (!isValid) {
		dcassert(0);
		aErrorF("Private key verification failed");
		return false;
	}

	// Public key (optional)
	if (aMakeHeader) {
		writePublicKey(PathUtil::getFilePath(aVersionFilePath) + "pubkey.h", publicKey, aErrorF);
	}

	return true;
}

bool UpdaterCreator::writePublicKey(const string& aOutputPath, const ByteVector& aPubKey, const ErrorF& aErrorF) noexcept {
	string c_key = Util::emptyString;
	c_key = "// Automatically generated file, DO NOT EDIT!" NATIVE_NL NATIVE_NL;
	c_key += "#ifndef PUBKEY_H" NATIVE_NL "#define PUBKEY_H" NATIVE_NL NATIVE_NL;

	c_key += "uint8_t dcpp::UpdateManager::publicKey[] = { " NATIVE_NL "\t";
	for (int i = 0; i < aPubKey.size(); ++i) {
		c_key += (dcpp_fmt("0x%02X") % (unsigned int)aPubKey[i]).str();
		if (i < aPubKey.size() - 1) {
			c_key += ", ";
			if ((i + 1) % 15 == 0) c_key += NATIVE_NL "\t";
		}
		else c_key += " " NATIVE_NL "};" NATIVE_NL NATIVE_NL;
	}

	c_key += "#endif // PUBKEY_H" NATIVE_NL;

	try {
		// Write the public key header (openssl probably has something to generate similar file, but couldn't locate it)
		File pubKey(aOutputPath, File::WRITE, File::TRUNCATE | File::CREATE);
		pubKey.write(c_key);
	} catch (const FileException&) {
		aErrorF("Could not write private/public key");
		return false;
	}

	return true;
}

} // namespace dcpp