/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#include <web-server/ExtensionManager.h>
#include <web-server/Extension.h>
#include <web-server/NpmRepository.h>
#include <web-server/Session.h>
#include <web-server/SocketManager.h>
#include <web-server/TarFile.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/WebSocket.h>

#include <airdcpp/util/CryptoUtil.h>
#include <airdcpp/hash/value/Encoder.h>
#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/connection/http/HttpDownload.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/core/classes/ScopedFunctor.h>
#include <airdcpp/core/io/xml/SimpleXML.h>
#include <airdcpp/util/text/StringTokenizer.h>
#include <airdcpp/util/SystemUtil.h>
#include <airdcpp/core/thread/Thread.h>
#include <airdcpp/core/timer/TimerManager.h>
#include <airdcpp/core/update/UpdateManager.h>
#include <airdcpp/core/io/compress/ZUtils.h>


namespace webserver {
	ExtensionManager::ExtensionManager(WebServerManager* aWsm) : wsm(aWsm) {
		wsm->addListener(this);

		npmRepository = make_unique<NpmRepository>(
			std::bind_front(&ExtensionManager::downloadExtension, this),
			std::bind_front(&ExtensionManager::log, this)
		);
	}

	ExtensionManager::~ExtensionManager() {
		wsm->removeListener(this);
	}

	void ExtensionManager::log(const string& aMsg, LogMessage::Severity aSeverity) const noexcept {
		LogManager::getInstance()->message(aMsg, aSeverity, STRING(EXTENSIONS));
	}

	void ExtensionManager::on(WebServerManagerListener::Started) noexcept {
		wsm->getSocketManager().addListener(this);

		// Don't add in the constructor as core may not have been initialized at that point
		UpdateManager::getInstance()->addListener(this);

		load();
		fire(ExtensionManagerListener::Started());
	}

	void ExtensionManager::on(WebServerManagerListener::Stopping) noexcept {
		if (updateCheckTask) {
			updateCheckTask->stop(true);
		}

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
					log(STRING_F(WEB_EXTENSION_STOP_FAILED, ext->getName() % e.what()), LogMessage::SEV_ERROR);
				}
			}
		}

		wsm->getSocketManager().removeListener(this);

		UpdateManager::getInstance()->removeListener(this);
		fire(ExtensionManagerListener::Stopped());
	}

	void ExtensionManager::on(WebServerManagerListener::Stopped) noexcept {
		for (;;) {
			{
				RLock l(cs);
				if (httpDownloads.empty()) {
					break;
				}
			}

			Thread::sleep(50);
		}

		WLock l(cs);
		dcassert(ranges::all_of(extensions, [](const ExtensionPtr& aExtension) { return !aExtension->getSession(); }));
		extensions.clear();
	}

	void ExtensionManager::on(SocketManagerListener::SocketDisconnected, const WebSocketPtr& aSocket) noexcept {
		if (!aSocket->getSession()) {
			return;
		}

		aSocket->getSession()->getServer()->addAsyncTask([session = aSocket->getSession(), this] {
			ExtensionPtr extension = nullptr;

			// Remove possible unmanaged extensions matching this session
			{
				RLock l(cs);
				auto i = ranges::find_if(extensions, [&](const ExtensionPtr& aExtension) {
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

	void ExtensionManager::on(UpdateManagerListener::VersionFileDownloaded, SimpleXML& xml) noexcept {
		if (WEBCFG(EXTENSIONS_AUTO_UPDATE).boolean()) {
			// Wait 10 seconds so that the extensions aren't updated right after they were started
			// (stopping the extensions may fail in such case)
			updateCheckTask = wsm->addTimer([this] {
				checkExtensionUpdates();

				updateCheckTask->stop(true);
				wsm->addAsyncTask([this] {
					updateCheckTask.reset();
				});
			}, 10000);

			updateCheckTask->start(false);
		}

		try {
			xml.resetCurrentChild();
			if (xml.findChild("BlockedExtensions")) {
				xml.stepIn();

				while (xml.findChild("BlockedExtension")) {
					const auto& reason = xml.getChildAttrib("Reason");

					xml.stepIn();
					const auto& name = xml.getData();
					xml.stepOut();

					{
						WLock l(cs);
						blockedExtensions.try_emplace(name, reason);
					}
				}

				xml.stepOut();
			}
		} catch (const SimpleXMLException& e) {
			log("Failed to read blocked extensions: " + e.getError(), LogMessage::SEV_ERROR);
		}

		uninstallBlockedExtensions();
	}

	void ExtensionManager::uninstallBlockedExtensions() noexcept {
		vector<pair<ExtensionPtr, string>> toRemove;

		{
			RLock l(cs);
			for (const auto& e : extensions) {
				auto i = blockedExtensions.find(e->getName());
				if (i != blockedExtensions.end()) {
					toRemove.emplace_back(e, i->second);
				}
			}
		}

		for (const auto& [ext, message] : toRemove) {
			try {
				log(STRING_F(WEB_EXTENSION_UNINSTALL_BLOCKED, ext->getName() % message), LogMessage::SEV_WARNING);
				uninstallLocalExtensionThrow(ext, true);
			} catch (const Exception& e) {
				log(e.what(), LogMessage::SEV_ERROR);
			}
		}
	}

	void ExtensionManager::load() noexcept {
		auto directories = File::findFiles(EXTENSION_DIR_ROOT, "*", File::TYPE_DIRECTORY);
		auto engines = getEngines();

		int started = 0;
		for (const auto& path : directories) {
			auto ext = loadLocalExtension(path);
			if (ext && startExtensionImpl(ext, engines)) {
				started++;
			}
		}

		if (started > 0) {
			auto isDebug = WEBCFG(EXTENSIONS_DEBUG_MODE).boolean();
			auto message = STRING_F(X_EXTENSIONS_LOADED, started);
			log(isDebug ? STRING_F(X_DEBUG_MODE, message) : message, LogMessage::SEV_INFO);
		}
	}

	void ExtensionManager::checkExtensionUpdates() const noexcept {
		RLock l(cs);

		for (const auto& ext : extensions) {
			if (!ext->isManaged() || ext->isPrivate()) {
				continue;
			}

			npmRepository->checkUpdates(ext->getName(), ext->getVersion());
		}
	}

	bool ExtensionManager::waitLoaded() const noexcept {
		const auto timeout = GET_TICK() + (WEBCFG(EXTENSIONS_INIT_TIMEOUT).num() * 1000);
		const auto isReady = [](const ExtensionPtr& aExt) {
			return !aExt->isRunning() || !aExt->getSignalReady() || aExt->getReady();
		};

		while (GET_TICK() < timeout) {
			{
				RLock l(cs);
				if (ranges::all_of(extensions, isReady)) {
					return true;
				}
			}

			Thread::sleep(50);
		}

		{
			RLock l(cs);
			for (const auto& ext: extensions) {
				if (!isReady(ext)) {
					log(STRING_F(WEB_EXTENSION_INIT_TIMED_OUT, ext->getName()), LogMessage::SEV_WARNING);
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
			auto i = ranges::find(extensions, aExtension);
			if (i != extensions.end()) {
				extensions.erase(i);
			} else {
				dcassert(0);
				return false;
			}
		}

		aExtension->removeListener(this);
		fire(ExtensionManagerListener::ExtensionRemoved(), aExtension);
		return true;
	}

	void ExtensionManager::uninstallLocalExtensionThrow(const ExtensionPtr& aExtension, bool aForced) {
		dcassert(aExtension->isManaged());
		aExtension->removeListeners();

		// Stop running extensions
		try {
			aExtension->stopThrow();
		} catch (const Exception& e) {
			if (!aForced) {
				throw;
			}

			// Try to continue in any case...
			log(e.getError(), LogMessage::SEV_ERROR);
		}

		// Remove from disk
		File::removeDirectoryForced(aExtension->getRootPath());

		// Remove from list
		if (!removeExtension(aExtension)) {
			throw Exception("Extension was not found");
		}

		log(STRING_F(WEB_EXTENSION_UNINSTALLED, aExtension->getName()), LogMessage::SEV_INFO);
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
		auto [_, added] = httpDownloads.try_emplace(aUrl, make_shared<HttpDownload>(aUrl, [aInstallId, aUrl, aSha1, this]() {
			onExtensionDownloadCompleted(aInstallId, aUrl, aSha1);
		}));

		return added;
	}

	bool ExtensionManager::validateSha1(const string& aData, const string& aSha1) noexcept {
		if (aSha1.empty()) {
			return true;
		}

		auto calculatedSha1 = CryptoUtil::calculateSha1(aData);
		if (calculatedSha1) {
			char mdString[SHA_DIGEST_LENGTH * 2 + 1];
			for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
				snprintf(&mdString[i * 2], sizeof(mdString), "%02x", (*calculatedSha1)[i]);

			if (compare(string(mdString), aSha1) == 0) {
				return true;
			}
		}

		return false;
	}

	void ExtensionManager::onExtensionDownloadCompleted(const string& aInstallId, const string& aUrl, const string& aSha1) noexcept {
		auto tempFile = AppUtil::getPath(AppUtil::PATH_TEMP) + PathUtil::validateFileName(aUrl) + ".tmp";

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
			if (!validateSha1(download->buf, aSha1)) {
				failInstallation(aInstallId, STRING(WEB_EXTENSION_DOWNLOAD_FAILED), STRING(WEB_EXTENSION_CHECKSUM_MISMATCH));
				return;
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

		string tempRoot = AppUtil::getPath(AppUtil::PATH_TEMP) + "extension_" + PathUtil::getFileName(aInstallFilePath) + PATH_SEPARATOR_STR;
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
			Extension extensionInfo(tempPackageDirectory, nullptr, true);

			extensionInfo.checkCompatibilityThrow();
			extensionName = extensionInfo.getName();
		} catch (const std::exception& e) {
			failInstallation(aInstallId, STRING(WEB_EXTENSION_LOAD_ERROR), e.what());
			return;
		}

		// Check blocked extensions
		{
			RLock l(cs);
			auto i = blockedExtensions.find(extensionName);
			if (i != blockedExtensions.end()) {
				failInstallation(aInstallId, STRING(WEB_EXTENSION_INSTALL_BLOCKED), i->second);
				return;
			}
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
				File::removeDirectoryForced(PathUtil::joinDirectory(extension->getRootPath(), EXT_PACKAGE_DIR));
			} catch (const FileException& e) {
				failInstallation(aInstallId, "Failed to remove the old extension package directory " + PathUtil::joinDirectory(extension->getRootPath(), EXT_PACKAGE_DIR), e.getError());
				return;
			}
		}

		try {
			// Move files to final destination directory
			File::moveDirectory(tempPackageDirectory, PathUtil::joinDirectory(Extension::getRootPath(extensionName), EXT_PACKAGE_DIR));
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

			log(STRING_F(WEB_EXTENSION_UPDATED, extension->getName()), LogMessage::SEV_INFO);
		} else {
			// Install new
			extension = loadLocalExtension(Extension::getRootPath(extensionName));
			if (!extension) {
				dcassert(0);
				return;
			}

			fire(ExtensionManagerListener::ExtensionAdded(), extension);
			log(STRING_F(WEB_EXTENSION_INSTALLED, extension->getName()), LogMessage::SEV_INFO);
		}

		startExtensionImpl(extension, getEngines());
		fire(ExtensionManagerListener::InstallationSucceeded(), aInstallId, extension, updated);
	}

	void ExtensionManager::failInstallation(const string& aInstallId, const string& aMessage, const string& aException) noexcept {
		auto msg = aMessage;
		if (!aException.empty()) {
			msg += " (" + aException + ")";
		}

		fire(ExtensionManagerListener::InstallationFailed(), aInstallId, msg);

		log(STRING_F(WEB_EXTENSION_INSTALLATION_FAILED, aInstallId % msg), LogMessage::SEV_ERROR);
	}

	ExtensionPtr ExtensionManager::registerRemoteExtensionThrow(const SessionPtr& aSession, const json& aPackageJson) {
		auto ext = std::make_shared<Extension>(aSession, aPackageJson);

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

		ext->addListener(this);

		fire(ExtensionManagerListener::ExtensionAdded(), ext);
		return ext;
	}

	void ExtensionManager::onExtensionStateUpdated(const Extension* aExtension) noexcept {
		fire(ExtensionManagerListener::ExtensionStateUpdated(), aExtension);
	}


	void ExtensionManager::on(ExtensionListener::ExtensionStarted, const Extension* aExt) noexcept {
		onExtensionStateUpdated(aExt);
	}

	void ExtensionManager::on(ExtensionListener::ExtensionStopped, const Extension* aExt, bool /*aFailed*/) noexcept {
		onExtensionStateUpdated(aExt);
	}

	void ExtensionManager::on(ExtensionListener::SettingValuesUpdated, const Extension* aExt, const SettingValueMap&) noexcept {
		onExtensionStateUpdated(aExt);
	}

	void ExtensionManager::on(ExtensionListener::SettingDefinitionsUpdated, const Extension* aExt) noexcept {
		onExtensionStateUpdated(aExt);
	}

	void ExtensionManager::on(ExtensionListener::PackageUpdated, const Extension* aExt) noexcept {
		onExtensionStateUpdated(aExt);
	}

#define EXIT_CODE_TIMEOUT 124
#define EXIT_CODE_IO_ERROR 74
#define EXIT_CODE_TEMP_ERROR 75
	void ExtensionManager::onExtensionFailed(const Extension* aExtension, uint32_t aExitCode) noexcept {
		if (aExitCode == EXIT_CODE_TIMEOUT || aExitCode == EXIT_CODE_IO_ERROR || aExitCode == EXIT_CODE_TEMP_ERROR) {
			// Attempt to restart it (but outside of extension's timer thread)
			const auto& name = aExtension->getName();
			wsm->addAsyncTask([aExtension, name, this] {
				// Wait for the log file handles to get closed
				Thread::sleep(3000);

				auto extension = getExtension(name);
				if (extension && startExtensionImpl(extension, getEngines())) {
					log(STRING_F(WEB_EXTENSION_TIMED_OUT, aExtension->getName()), LogMessage::SEV_INFO);
				}
			});
		} else {
			if (WEBCFG(EXTENSIONS_DEBUG_MODE).boolean()) {
				log(
					"Extension " + aExtension->getName() + " exited with code " + Util::toString(aExitCode), 
					LogMessage::SEV_ERROR
				);
			}

			log(
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
				PathUtil::joinDirectory(aPath, EXT_PACKAGE_DIR),
				std::bind_front(&ExtensionManager::onExtensionFailed, this)
			);
		} catch (const Exception& e) {
			log(STRING_F(WEB_EXTENSION_LOAD_ERROR_X, aPath % e.what()), LogMessage::SEV_ERROR);
			return nullptr;
		}

		if (getExtension(ext->getName())) {
			dcassert(0);
			log(STRING_F(WEB_EXTENSION_LOAD_ERROR_X, aPath % STRING(WEB_EXTENSION_EXISTS)), LogMessage::SEV_ERROR);
			return nullptr;
		}

		// Store in the extension list
		{
			WLock l(cs);
			extensions.push_back(ext);
		}

		ext->addListener(this);
		return ext;
	}

	bool ExtensionManager::startExtensionImpl(const ExtensionPtr& aExtension, const ExtensionEngine::List& aInstalledEngines) noexcept {
		try {
			auto launchInfo = getStartCommandThrow(aExtension->getEngines(), aInstalledEngines);
			aExtension->startThrow(launchInfo.command, wsm, launchInfo.arguments);
		} catch (const Exception& e) {
			log(STRING_F(WEB_EXTENSION_START_ERROR, aExtension->getName() % e.what()), LogMessage::SEV_ERROR);
			return false;
		}

		return true;
	}

	ExtensionManager::ExtensionLaunchInfo ExtensionManager::getStartCommandThrow(const StringList& aSupportedExtEngines, const ExtensionEngine::List& aInstalledEngines) const {
		string lastError;
		for (const auto& supportedExtEngine: aSupportedExtEngines) {
			// Find an installed engine that can run this extension
			auto engineIter = ranges::find_if(aInstalledEngines, [&supportedExtEngine](const auto& e) { return e.name == supportedExtEngine; });
			if (engineIter == aInstalledEngines.end()) {
				lastError = STRING_F(WEB_EXTENSION_ENGINE_NO_CONFIG, supportedExtEngine);
				continue;
			}

			// We have a match, choose the correct command
			auto parsedCommand = selectEngineCommand(engineIter->command);
			if (!parsedCommand.empty()) {
				return { parsedCommand, engineIter->arguments };
			}

			lastError = STRING_F(WEB_EXTENSION_ENGINE_NOT_INSTALLED, supportedExtEngine % engineIter->command);
		}

		dcassert(!lastError.empty());
		throw Exception(lastError);
	}

	ExtensionEngine::List ExtensionManager::getEngines() const noexcept {
		return WEBCFG(EXTENSION_ENGINES).getValue().get<ExtensionEngine::List>();
	}

	string ExtensionManager::selectEngineCommand(const string& aEngineCommands) noexcept {
		auto tokens = StringTokenizer<string>(aEngineCommands, ';', false);
		for (const auto& token: tokens.getTokens()) {
			if (File::isAbsolutePath(token)) {
				// Full path
				if (PathUtil::fileExists(token)) {
					return token;
				}
			} else if (token.length() >= 2 && token.compare(0, 2, "./") == 0) {
				// Relative path
				auto fullPath = AppUtil::getAppFilePath() + token.substr(2);
				if (PathUtil::fileExists(fullPath)) {
					return fullPath;
				}
			} else {
				// Application in PATH
#ifdef _WIN32
				string testCommand = "where";
#else
				string testCommand = "command -v";
#endif
				testCommand += " " + token;

				if (dcpp::SystemUtil::runSystemCommand(testCommand) == 0) {
					return token;
				}
			}
		}

		return Util::emptyString;
	}
}