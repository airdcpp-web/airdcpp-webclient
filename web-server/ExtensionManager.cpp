/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#include <web-server/stdinc.h>

#include <web-server/ExtensionManager.h>
#include <web-server/Extension.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebSocket.h>

#include <airdcpp/CryptoManager.h>
#include <airdcpp/Encoder.h>
#include <airdcpp/File.h>
#include <airdcpp/HttpDownload.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/ScopedFunctor.h>
#include <airdcpp/ZUtils.h>


namespace webserver {
	ExtensionManager::ExtensionManager(WebServerManager* aWsm) : wsm(aWsm) {
		wsm->addListener(this);

		engines = {
#ifdef _WIN32
			{ "node", "./nodejs/node.exe;node" },
#else
			{ "node", "nodejs;node" },
#endif
			{ "python", "python" },
		};
	}

	ExtensionManager::~ExtensionManager() {
		wsm->removeListener(this);
	}

	void ExtensionManager::on(WebServerManagerListener::Started) noexcept {
		load();
	}

	void ExtensionManager::on(WebServerManagerListener::Stopping) noexcept {
		RLock l(cs);
		for (const auto& ext : extensions) {
			ext->stop();
		}
	}

	void ExtensionManager::on(WebServerManagerListener::SocketDisconnected, const WebSocketPtr& aSocket) noexcept {
		if (!aSocket->getSession()) {
			return;
		}

		ExtensionPtr extension = nullptr;

		// Remove possible unmanaged extensions matching this session
		{
			RLock l(cs);
			auto i = find_if(extensions.begin(), extensions.end(), [&](const ExtensionPtr& aExtension) {
				return aExtension->getSession() == aSocket->getSession();
			});

			if (i == extensions.end() || (*i)->isManaged()) {
				return;
			}

			extension = *i;
		}

		removeExtension(extension);
	}

	void ExtensionManager::load() noexcept {
		auto directories = File::findFiles(EXTENSION_DIR_ROOT, "*", File::TYPE_DIRECTORY);

		for (const auto& path : directories) {
			auto ext = loadLocalExtension(path);
			if (ext) {
				startExtension(ext);
			}
		}
	}

	ExtensionList ExtensionManager::getExtensions() const noexcept {
		RLock l(cs);
		return extensions;
	}

	void ExtensionManager::removeExtension(const ExtensionPtr& aExtension) {
		if (aExtension->isManaged()) {
			// Stop running extensions
			if (!stopExtension(aExtension)) {
				throw Exception("Failed to stop the extension process");
			}

			// Remove from disk
			File::removeDirectoryForced(aExtension->getRootPath());
		}

		// Remove from list
		{
			WLock l(cs);
			auto i = find(extensions.begin(), extensions.end(), aExtension);
			if (i != extensions.end()) {
				extensions.erase(i);
			} else {
				dcassert(0);
			}
		}

		fire(ExtensionManagerListener::ExtensionRemoved(), aExtension);
	}

	ExtensionPtr ExtensionManager::getExtension(const string& aName) const noexcept {
		RLock l(cs);
		auto i = find(extensions.begin(), extensions.end(), aName);
		return i == extensions.end() ? nullptr : *i;
	}

	bool ExtensionManager::downloadExtension(const string& aUrl, const string& aSha1) noexcept {
		WLock l(cs);
		auto ret = httpDownloads.emplace(aUrl, make_shared<HttpDownload>(aUrl, [=]() {
			onExtensionDownloadCompleted(aUrl, aSha1);
		}, false));

		return ret.second;
	}

	void ExtensionManager::onExtensionDownloadCompleted(const string& aUrl, const string& aSha1) noexcept {
		auto tempFile = Util::getTempPath() + Util::validateFileName(aUrl) + ".tmp";

		// Don't allow the same download to be initiated again until the installation has finished
		ScopedFunctor([&]() {
			File::deleteFile(tempFile);

			WLock l(cs);
			httpDownloads.erase(aUrl);
		});

		{
			HttpDownloadMap::mapped_type download = nullptr;

			// Get the download
			{
				WLock l(cs);
				auto i = httpDownloads.find(aUrl);
				if (i == httpDownloads.end()) {
					dcassert(0);
					return;
				}

				download = i->second;
			}

			if (download->buf.empty()) {
				failInstallation("Download failed", download->status);
				return;
			}

			// Validate the possible checksum
			if (!aSha1.empty()) {
				auto calculatedSha1 = CryptoManager::calculateSha1(download->buf);
				if (calculatedSha1) {
					char mdString[SHA_DIGEST_LENGTH * 2 + 1];
					for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
						sprintf(&mdString[i * 2], "%02x", (*calculatedSha1)[i]);

					if (compare(string(mdString), aSha1) != 0) {
						failInstallation("Download failed", "Checksum validation mismatch");
						return;
					}
				}
			}

			// Save on disk
			try {
				File(tempFile, File::WRITE, File::CREATE | File::TRUNCATE).write(download->buf);
			} catch (const FileException& e) {
				failInstallation("Failed to save the package", e.what());
				return;
			}
		}

		// Install
		installLocalExtension(tempFile);
	}

	void ExtensionManager::installLocalExtension(const string& aInstallFilePath) noexcept {
		string tarFile = aInstallFilePath + "_DECOMPRESSED";
		ScopedFunctor([&tarFile]() { 
			auto success = File::deleteFile(tarFile);
			dcassert(success);
		});
		try {
			GZ::decompress(aInstallFilePath, tarFile);
		} catch (const Exception& e) {
			failInstallation("Failed to decompress the package", e.what());
			return;
		}

		const auto untar = [](const string& aTarFile, const string& aDestinationDirectory) {
			File::ensureDirectory(aDestinationDirectory);
#ifdef _WIN32
			string command = "7z x " + aTarFile + " -o" + aDestinationDirectory + " -y";
#else
			string command = "tar -xf " + aTarFile + " -C " + aDestinationDirectory;
#endif
			return std::system(command.c_str());
		};

		string tempPackageDirectory = Util::getTempPath() + "extension_" + Util::getFileName(aInstallFilePath) + PATH_SEPARATOR_STR;
		ScopedFunctor([&tempPackageDirectory]() {
			try {
				File::removeDirectoryForced(tempPackageDirectory);
			} catch (const FileException& e) {
				dcdebug("Failed to delete the temporary extension directory %s: %s\n", tempPackageDirectory.c_str(), e.getError().c_str());
			}
		});

		{
			// Unpack the content to temp directory for validation purposes
			auto code = untar(tarFile, tempPackageDirectory);
			if (code != 0) {
				failInstallation("Failed to uncompress the extension to temp directory", "code " + Util::toString(code));
				return;
			}
		}

		string finalInstallPath;
		try {
			// Validate the package content
			Extension extensionInfo(tempPackageDirectory, nullptr, true);
			finalInstallPath = extensionInfo.getRootPath();
		} catch (const std::exception& e) {
			failInstallation("Failed to load extension", e.what());
			return;
		}

		// Updating an existing extension?
		auto extension = getExtension(Util::getLastDir(finalInstallPath));
		if (extension) {
			if (!extension->isManaged()) {
				failInstallation("Extension exits", "Unmanaged extensions can't be upgraded");
				return;
			}

			// Stop and remove the old package
			if (!stopExtension(extension)) {
				return;
			}

			try {
				File::removeDirectoryForced(extension->getPackageDirectory());
			} catch (const FileException& e) {
				failInstallation("Failed to remove the old extension package directory " + extension->getPackageDirectory(), e.getError());
			}
		}

		// Extract to final destination directory
		if (untar(tarFile, finalInstallPath) != 0) {
			dcassert(0);
			failInstallation("Failed to uncompress the extension to destination directory", Util::emptyString);
			return;
		}

		if (extension) {
			// Updating
			try {
				extension->reload();
			} catch (const Exception& e) {
				dcassert(0);
				failInstallation("Failed to load extension " + finalInstallPath, e.what());
				return;
			}

			LogManager::getInstance()->message("Extension " + extension->getName() + " was updated succesfully", LogMessage::SEV_INFO);
		} else {
			// Install new
			extension = loadLocalExtension(finalInstallPath);
			if (!extension) {
				dcassert(0);
				return;
			}

			fire(ExtensionManagerListener::ExtensionAdded(), extension);
			LogManager::getInstance()->message("Extension " + extension->getName() + " was installed succesfully", LogMessage::SEV_INFO);
		}

		startExtension(extension);
	}

	void ExtensionManager::failInstallation(const string& aMessage, const string& aException) noexcept {
		string msg = "Extension installation failed: " + aMessage;
		if (!aException.empty()) {
			msg += " (" + aException + ")";
		}

		LogManager::getInstance()->message(msg, LogMessage::SEV_ERROR);
	}

	ExtensionPtr ExtensionManager::registerRemoteExtension(const SessionPtr& aSession, const json& aPackageJson) {
		auto ext = std::make_shared<Extension>(aSession, aPackageJson);

		if (getExtension(ext->getName())) {
			throw Exception("Extension " + ext->getName() + " exists already");
		}

		{
			WLock l(cs);
			extensions.push_back(ext);
		}

		fire(ExtensionManagerListener::ExtensionAdded(), ext);
		return ext;
	}

	ExtensionPtr ExtensionManager::loadLocalExtension(const string& aPath) noexcept {
		// Parse
		ExtensionPtr ext = nullptr;
		try {
			ext = std::make_shared<Extension>(aPath, [](const Extension* aExtension) {
				LogManager::getInstance()->message("Extension " + aExtension->getName() + " has exited (see the extension log for error details)", LogMessage::SEV_ERROR);
			});
		} catch (const Exception& e) {
			LogManager::getInstance()->message("Failed to load extension " + aPath + ": " + e.what(), LogMessage::SEV_ERROR);
			return nullptr;
		}

		// Store in the extension list
		{
			WLock l(cs);
			extensions.push_back(ext);
		}

		return ext;
	}

	bool ExtensionManager::startExtension(const ExtensionPtr& aExtension) noexcept {
		try {
			auto command = getStartCommand(aExtension);
			aExtension->start(command, wsm);
		} catch (const Exception& e) {
			LogManager::getInstance()->message("Failed to start the extension " + aExtension->getName() + ": " + e.what(), LogMessage::SEV_ERROR);
			return false;
		}

		LogManager::getInstance()->message("Extension " + aExtension->getName() + " was started", LogMessage::SEV_INFO);
		return true;
	}

	bool ExtensionManager::stopExtension(const ExtensionPtr& aExtension) noexcept {
		if (!aExtension->stop()) {
			return false;
		}

		LogManager::getInstance()->message("Extension " + aExtension->getName() + " was stopped", LogMessage::SEV_INFO);
		return true;
	}

	string ExtensionManager::getStartCommand(const ExtensionPtr& aExtension) const {
		string lastError;
		for (const auto& extEngine : aExtension->getEngines()) {
			string engineCommandStr;

			{
				RLock l(cs);
				auto i = engines.find(extEngine);
				if (i == engines.end()) {
					lastError = "Scripting engine \"" + extEngine + "\" is not configured in application settings";
					continue;
				}

				engineCommandStr = i->second;
			}

			// We have a match
			auto parsedCommand = selectEngineCommand(engineCommandStr);
			if (!parsedCommand.empty()) {
				return parsedCommand;
			}

			lastError = "Scripting engine \"" + extEngine + "\" is not installed on the system (tested commands: " + engineCommandStr + ").";
		}

		dcassert(!lastError.empty());
		throw Exception(lastError);
	}

	string ExtensionManager::selectEngineCommand(const string& aEngineCommands) noexcept {
		auto tokens = StringTokenizer<string>(aEngineCommands, ';', false);
		for (const auto& token: tokens.getTokens()) {
			if (File::isAbsolutePath(token)) {
				// Full path
				if (Util::fileExists(token)) {
					return token;
				}
			} else if (token.length() >= 2 && token.compare(0, 2, "./") == 0) {
				// Relative path
				auto fullPath = Util::getAppFilePath() + token.substr(2);
				if (Util::fileExists(fullPath)) {
					return fullPath;
				}
			} else {
				// Application in PATH
#ifdef _WIN32
				string testCommand = "where";
#else
				string testCommand = "which";
#endif
				testCommand += " " + token;

				if (Util::runSystemCommand(testCommand) == 0) {
					return token;
				}
			}
		}

		return Util::emptyString;
	}
}