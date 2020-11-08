/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#include <web-server/ExtensionManager.h>
#include <web-server/Extension.h>
#include <web-server/TarFile.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebSocket.h>

#include <airdcpp/CryptoManager.h>
#include <airdcpp/Encoder.h>
#include <airdcpp/File.h>
#include <airdcpp/HttpDownload.h>
#include <airdcpp/ScopedFunctor.h>
#include <airdcpp/Thread.h>
#include <airdcpp/TimerManager.h>
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
			{ "python3", "python3;python" },
		};
	}

	ExtensionManager::~ExtensionManager() {
		wsm->removeListener(this);
	}

	void ExtensionManager::on(WebServerManagerListener::Started) noexcept {
		load();
		fire(ExtensionManagerListener::Started());
	}

	void ExtensionManager::on(WebServerManagerListener::Stopping) noexcept {
		{
			RLock l(cs);
			for (const auto& ext: extensions) {
				ext->removeListeners();

				if (!ext->isManaged()) {
					continue;
				}

				try {
					ext->stopThrow();
				} catch (const Exception& e) {
					wsm->log(STRING_F(WEB_EXTENSION_STOP_FAILED, ext->getName() % e.what()), LogMessage::SEV_ERROR);
				}
			}
		}

		fire(ExtensionManagerListener::Stopped());
	}

	void ExtensionManager::on(WebServerManagerListener::Stopped) noexcept {
		WLock l(cs);
		dcassert(all_of(extensions.begin(), extensions.end(), [](const ExtensionPtr& aExtension) { return !aExtension->getSession(); }));
		extensions.clear();
	}

	void ExtensionManager::on(WebServerManagerListener::SocketDisconnected, const WebSocketPtr& aSocket) noexcept {
		if (!aSocket->getSession()) {
			return;
		}

		const auto session = aSocket->getSession();
		aSocket->getSession()->getServer()->addAsyncTask([session, this] {
			ExtensionPtr extension = nullptr;

			// Remove possible unmanaged extensions matching this session
			{
				RLock l(cs);
				auto i = find_if(extensions.begin(), extensions.end(), [&](const ExtensionPtr& aExtension) {
					return aExtension->getSession() == session;
				});

				if (i == extensions.end() || (*i)->isManaged()) {
					return;
				}

				extension = *i;
			}

			unregisterRemoteExtension(extension);
		});
	}

	void ExtensionManager::load() noexcept {
		auto directories = File::findFiles(EXTENSION_DIR_ROOT, "*", File::TYPE_DIRECTORY);

		for (const auto& path : directories) {
			auto ext = loadLocalExtension(path);
			if (ext && startExtensionImpl(ext)) {
				auto isDebug = WEBCFG(EXTENSIONS_DEBUG_MODE).boolean();
				auto message = isDebug ? 
					STRING_F(WEB_EXTENSION_LOADED_DBG, ext->getName()) : 
					STRING_F(WEB_EXTENSION_LOADED, ext->getName());

				wsm->log(message, LogMessage::SEV_INFO);
			}
		}
	}

	bool ExtensionManager::waitLoaded() const noexcept {
		const auto timeout = GET_TICK() + (WEBCFG(EXTENSIONS_INIT_TIMEOUT).num() * 1000);
		const auto isReady = [](const ExtensionPtr& aExt) {
			return !aExt->getSignalReady() || aExt->getReady();
		};

		while (GET_TICK() < timeout) {
			{
				RLock l(cs);
				if (all_of(extensions.begin(), extensions.end(), isReady)) {
					return true;
				}
			}

			Thread::sleep(50);
		}

		{
			RLock l(cs);
			for (const auto& ext: extensions) {
				if (!isReady(ext)) {
					wsm->log(STRING_F(WEB_EXTENSION_INIT_TIMED_OUT, ext->getName()), LogMessage::SEV_WARNING);
				}
			}
		}

		return false;
	}

	ExtensionList ExtensionManager::getExtensions() const noexcept {
		RLock l(cs);
		return extensions;
	}

	void ExtensionManager::unregisterRemoteExtension(const ExtensionPtr& aExtension) noexcept {
		dcassert(!aExtension->isManaged());
		aExtension->resetSession();
		removeExtension(aExtension);
	}

	bool ExtensionManager::removeExtension(const ExtensionPtr& aExtension) noexcept {
		{
			WLock l(cs);
			auto i = find(extensions.begin(), extensions.end(), aExtension);
			if (i != extensions.end()) {
				extensions.erase(i);
			} else {
				dcassert(0);
				return false;
			}
		}

		fire(ExtensionManagerListener::ExtensionRemoved(), aExtension);
		return true;
	}

	void ExtensionManager::uninstallLocalExtensionThrow(const ExtensionPtr& aExtension) {
		dcassert(aExtension->isManaged());
		aExtension->removeListeners();

		// Stop running extensions
		aExtension->stopThrow();

		// Remove from disk
		File::removeDirectoryForced(aExtension->getRootPath());

		// Remove from list
		if (!removeExtension(aExtension)) {
			throw Exception("Extension was not found");
		}

		fire(ExtensionManagerListener::ExtensionRemoved(), aExtension);
	}

	ExtensionPtr ExtensionManager::getExtension(const string& aName) const noexcept {
		RLock l(cs);
		auto i = find(extensions.begin(), extensions.end(), aName);
		return i == extensions.end() ? nullptr : *i;
	}

	bool ExtensionManager::downloadExtension(const string& aInstallId, const string& aUrl, const string& aSha1) noexcept {
		fire(ExtensionManagerListener::InstallationStarted(), aInstallId);

		WLock l(cs);
		auto ret = httpDownloads.emplace(aUrl, make_shared<HttpDownload>(aUrl, [=]() {
			onExtensionDownloadCompleted(aInstallId, aUrl, aSha1);
		}, false));

		return ret.second;
	}

	void ExtensionManager::onExtensionDownloadCompleted(const string& aInstallId, const string& aUrl, const string& aSha1) noexcept {
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
				failInstallation(aInstallId, STRING(WEB_EXTENSION_DOWNLOAD_FAILED), download->status);
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
						failInstallation(aInstallId, STRING(WEB_EXTENSION_DOWNLOAD_FAILED), STRING(WEB_EXTENSION_CHECKSUM_MISMATCH));
						return;
					}
				}
			}

			// Save on disk
			try {
				File(tempFile, File::WRITE, File::CREATE | File::TRUNCATE).write(download->buf);
			} catch (const FileException& e) {
				failInstallation(aInstallId, STRING(WEB_EXTENSION_PACKAGE_SAVE_FAILED), e.what());
				return;
			}
		}

		// Install
		installLocalExtension(aInstallId, tempFile);
	}

	void ExtensionManager::installLocalExtension(const string& aInstallId, const string& aInstallFilePath) noexcept {
		string tarFile = aInstallFilePath + "_DECOMPRESSED";
		ScopedFunctor([&tarFile]() { 
			auto success = File::deleteFile(tarFile);
			dcassert(success);
		});

		try {
			GZ::decompress(aInstallFilePath, tarFile);
		} catch (const Exception& e) {
			failInstallation(aInstallId, STRING(WEB_EXTENSION_PACKAGE_EXTRACT_FAILED), e.what());
			return;
		}

		string tempRoot = Util::getTempPath() + "extension_" + Util::getFileName(aInstallFilePath) + PATH_SEPARATOR_STR;
		ScopedFunctor([&tempRoot]() {
			try {
				File::removeDirectoryForced(tempRoot);
			} catch (const FileException& e) {
				dcdebug("Failed to delete the temporary extension directory %s: %s\n", tempRoot.c_str(), e.getError().c_str());
			}
		});

		try {
			// Unpack the content to temp directory for validation purposes
			TarFile tar(tarFile);
			tar.extract(tempRoot);
		} catch (const Exception& e) {
			failInstallation(aInstallId, STRING(WEB_EXTENSION_PACKAGE_EXTRACT_FAILED), e.what());
			return;
		}

		// Parse the extension directory
		string tempPackageDirectory;
		{
			auto directories = File::findFiles(tempRoot, "*", File::TYPE_DIRECTORY);
			if (directories.size() != 1) {
				failInstallation(aInstallId, STRING(WEB_EXTENSION_PACKAGE_MALFORMED_CONTENT), "There should be a single directory directly inside the extension package");
				return;
			}

			tempPackageDirectory = directories.front();
		}


		string extensionName;
		try {
			// Validate the package content
			Extension extensionInfo(tempPackageDirectory, nullptr, nullptr, true);

			extensionInfo.checkCompatibilityThrow();
			extensionName = extensionInfo.getName();
		} catch (const std::exception& e) {
			failInstallation(aInstallId, STRING(WEB_EXTENSION_LOAD_ERROR), e.what());
			return;
		}

		// Updating an existing extension?
		auto extension = getExtension(extensionName);
		if (extension) {
			if (!extension->isManaged()) {
				failInstallation(aInstallId, STRING(WEB_EXTENSION_EXISTS), "Unmanaged extensions can't be upgraded");
				return;
			}

			// Stop and remove the old package
			try {
				extension->stopThrow();
			} catch (const Exception& e) {
				failInstallation(aInstallId, e.what(), Util::emptyString);
				return;
			}

			try {
				File::removeDirectoryForced(Util::joinDirectory(extension->getRootPath(), EXT_PACKAGE_DIR));
			} catch (const FileException& e) {
				failInstallation(aInstallId, "Failed to remove the old extension package directory " + Util::joinDirectory(extension->getRootPath(), EXT_PACKAGE_DIR), e.getError());
				return;
			}
		}

		try {
			// Move files to final destination directory
			File::moveDirectory(tempPackageDirectory, Util::joinDirectory(Extension::getRootPath(extensionName), EXT_PACKAGE_DIR));
		} catch (const FileException& e) {
			failInstallation(aInstallId, "Failed to move extension files to the final destination directory", e.what());
			return;
		}

		bool updated = !!extension;
		if (updated) {
			// Updating
			try {
				extension->reloadThrow();
			} catch (const Exception& e) {
				// Shouldn't happen since the package has been validated earlier
				dcassert(0);
				failInstallation(aInstallId, "Failed to reload the updated extension package", e.what());
				return;
			}

			wsm->log(STRING_F(WEB_EXTENSION_UPDATED, extension->getName()), LogMessage::SEV_INFO);
		} else {
			// Install new
			extension = loadLocalExtension(Extension::getRootPath(extensionName));
			if (!extension) {
				dcassert(0);
				return;
			}

			fire(ExtensionManagerListener::ExtensionAdded(), extension);
			wsm->log(STRING_F(WEB_EXTENSION_INSTALLED, extension->getName()), LogMessage::SEV_INFO);
		}

		startExtensionImpl(extension);
		fire(ExtensionManagerListener::InstallationSucceeded(), aInstallId, extension, updated);
	}

	void ExtensionManager::failInstallation(const string& aInstallId, const string& aMessage, const string& aException) noexcept {
		auto msg = aMessage;
		if (!aException.empty()) {
			msg += " (" + aException + ")";
		}

		fire(ExtensionManagerListener::InstallationFailed(), aInstallId, msg);

		wsm->log(STRING_F(WEB_EXTENSION_INSTALLATION_FAILED, msg), LogMessage::SEV_ERROR);
	}

	ExtensionPtr ExtensionManager::registerRemoteExtensionThrow(const SessionPtr& aSession, const json& aPackageJson) {
		auto ext = std::make_shared<Extension>(aSession, aPackageJson, std::bind(&ExtensionManager::onExtensionStateUpdated, this, std::placeholders::_1));

		auto existing = getExtension(ext->getName());
		if (existing) {
			if (existing->isManaged()) {
				throw Exception(STRING(WEB_EXTENSION_EXISTS));
			}

			unregisterRemoteExtension(existing);
		}

		{
			WLock l(cs);
			extensions.push_back(ext);
		}

		fire(ExtensionManagerListener::ExtensionAdded(), ext);
		return ext;
	}

	void ExtensionManager::onExtensionStateUpdated(const Extension* aExtension) noexcept {
		fire(ExtensionManagerListener::ExtensionStateUpdated(), aExtension);
	}

#define EXIT_CODE_TIMEOUT 124
	void ExtensionManager::onExtensionFailed(const Extension* aExtension, uint32_t aExitCode) noexcept {
		if (aExitCode == EXIT_CODE_TIMEOUT) {
			// Attempt to restart it (but outside of extension's timer thread)
			auto name = aExtension->getName();
			wsm->addAsyncTask([=] {
				auto extension = getExtension(name);
				if (extension && startExtensionImpl(extension)) {
					wsm->log(STRING_F(WEB_EXTENSION_TIMED_OUT, aExtension->getName()), LogMessage::SEV_INFO);
				}
			});
		} else {
			if (WEBCFG(EXTENSIONS_DEBUG_MODE).boolean()) {
				wsm->log(
					"Extension " + aExtension->getName() + " exited with code " + Util::toString(aExitCode), 
					LogMessage::SEV_ERROR
				);
			}

			wsm->log(
				STRING_F(WEB_EXTENSION_EXITED, aExtension->getName() % aExtension->getErrorLogPath()),
				LogMessage::SEV_ERROR
			);
		 }
	}

	ExtensionPtr ExtensionManager::loadLocalExtension(const string& aPath) noexcept {
		// Parse
		ExtensionPtr ext = nullptr;
		try {
			ext = std::make_shared<Extension>(
				Util::joinDirectory(aPath, EXT_PACKAGE_DIR),
				std::bind(&ExtensionManager::onExtensionFailed, this, std::placeholders::_1, std::placeholders::_2),
				std::bind(&ExtensionManager::onExtensionStateUpdated, this, std::placeholders::_1)
			);
		} catch (const Exception& e) {
			wsm->log(STRING_F(WEB_EXTENSION_LOAD_ERROR_X, aPath % e.what()), LogMessage::SEV_ERROR);
			return nullptr;
		}

		if (getExtension(ext->getName())) {
			dcassert(0);
			wsm->log(STRING_F(WEB_EXTENSION_LOAD_ERROR_X, aPath % STRING(WEB_EXTENSION_EXISTS)), LogMessage::SEV_ERROR);
			return nullptr;
		}

		// Store in the extension list
		{
			WLock l(cs);
			extensions.push_back(ext);
		}

		return ext;
	}

	bool ExtensionManager::startExtensionImpl(const ExtensionPtr& aExtension) noexcept {
		try {
			auto command = getStartCommandThrow(aExtension->getEngines());
			aExtension->startThrow(command, wsm);
		} catch (const Exception& e) {
			wsm->log(STRING_F(WEB_EXTENSION_START_ERROR, aExtension->getName() % e.what()), LogMessage::SEV_ERROR);
			return false;
		}

		return true;
	}

	string ExtensionManager::getStartCommandThrow(const StringList& aEngines) const {
		string lastError;
		for (const auto& extEngine : aEngines) {
			string engineCommandStr;

			{
				RLock l(cs);
				auto i = engines.find(extEngine);
				if (i == engines.end()) {
					lastError = STRING_F(WEB_EXTENSION_ENGINE_NO_CONFIG, extEngine);
					continue;
				}

				engineCommandStr = i->second;
			}

			// We have a match
			auto parsedCommand = selectEngineCommand(engineCommandStr);
			if (!parsedCommand.empty()) {
				return parsedCommand;
			}

			lastError = STRING_F(WEB_EXTENSION_ENGINE_NOT_INSTALLED, extEngine % engineCommandStr);
		}

		dcassert(!lastError.empty());
		throw Exception(lastError);
	}

	ExtensionManager::EngineMap ExtensionManager::getEngines() const noexcept {
		return engines;
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