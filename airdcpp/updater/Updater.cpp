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
#include "Updater.h"

#include <airdcpp/UpdateConstants.h>

#include <airdcpp/AppUtil.h>
#include <airdcpp/Exception.h>
#include <airdcpp/File.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/Thread.h>
#include <airdcpp/Util.h>
#include <airdcpp/version.h>

#define UPDATER_LOG_FILE "updater.log"

namespace dcpp {

string Updater::toLoggerFilePath(const string& aDirectoryPath) noexcept {
	return aDirectoryPath + UPDATER_LOG_FILE;
}


// UPDATER INSTANCE
int Updater::cleanExtraFiles(const string& aCurPath, const optional<StringSet>& aProtectedFiles) noexcept {
	int deletedFiles = 0;
	File::forEachFile(aCurPath, "*", [&](const FilesystemItem& aInfo) {
		auto fullPath = aInfo.getPath(aCurPath);
		if (!aInfo.isDirectory) {
			if ((!aProtectedFiles || !(*aProtectedFiles).contains(fullPath)) && File::deleteFile(fullPath)) {
				deletedFiles++;
			}
		} else {
			deletedFiles += cleanExtraFiles(fullPath, aProtectedFiles);
		}
	});

	File::removeDirectory(aCurPath);
	return deletedFiles;
}

int Updater::destroyDirectory(const string& aPath) {
	int removed = 0;

	// The updater exe may not shut down instantly
	for (int i = 0; i < 3; i++) {
		removed += cleanExtraFiles(aPath, nullopt);
		if (PathUtil::fileExists(aPath)) {
			Sleep(1000);
		} else {
			break;
		}
	}

	return removed;
}

bool Updater::applyUpdaterFiles(const string& aCurTempPath, const string& aCurDestinationPath, string& error_, StringSet& updatedFiles_, FileLogger& aLogger) noexcept {
	File::ensureDirectory(aCurDestinationPath);

	try {
		File::forEachFile(aCurTempPath, "*", [&](const FilesystemItem& aInfo) {
			auto destFilePath = aInfo.getPath(aCurDestinationPath);
			auto tempFilePath = aInfo.getPath(aCurTempPath);

			if (!aInfo.isDirectory) {
				try {
					if (PathUtil::fileExists(destFilePath)) {
						File::deleteFile(destFilePath);
					}

					File::copyFile(tempFilePath, destFilePath);
					updatedFiles_.insert(destFilePath);

					aLogger.log("Installed file " + destFilePath);
				} catch (const Exception& e) {
					throw FileException("Failed to copy the file " + destFilePath + " (" + e.getError() + ")");
				}
			} else {
				applyUpdaterFiles(tempFilePath, destFilePath, error_, updatedFiles_, aLogger);
			}
		});
	} catch (const FileException& e) {
		error_ = e.getError();
		aLogger.log(e.getError());
		return false;
	}

	return true;
}

Updater::FileLogger::FileLogger(const string& aPath, bool aResetFile) {
	if (aResetFile) {
		File::deleteFile(aPath);
	}

	try {
		f.reset(new File(aPath, File::WRITE, File::OPEN | File::CREATE));
		f->setEndPos(0);
	} catch (...) {
		f = nullptr;
	}
}

void Updater::FileLogger::log(const string& aLine, bool aAddDate) noexcept {
	if (f && f->isOpen()) {
		string date;
		if (aAddDate) {
			time_t _tt;
			time(&_tt);
			date = Util::formatTime("[%Y-%m-%d %H:%M:%S]  ", _tt);
		}

		try {
			f->write(date + aLine + "\r\n");
		} catch (const FileException&) {

		}
	}
}

void Updater::FileLogger::separator() noexcept {
	log("\r\n", false);
}

Updater::FileLogger Updater::createInstallLogger(const string& aSourcePath) noexcept {
	// The path must be provided via startup params in case we are using a custom temp path (the installer won't load the boot config)
	// We need to put it in the root directory as we don't know the session token in the beginning after
	// starting the updated instance
	auto updaterFileRoot = PathUtil::getParentDir(aSourcePath);
	return FileLogger(toLoggerFilePath(updaterFileRoot), true);
}

bool Updater::applyUpdate(const string& aSourcePath, const string& aApplicationPath, string& error_, int aMaxRetries, FileLogger& logger_) noexcept {
	logger_.log("Starting to install build " + BUILD_NUMBER_STR);

	{
		// Copy new files
		StringSet updatedFiles;

		bool success = false;
		for (int i = 0; i < aMaxRetries && (success = Updater::applyUpdaterFiles(aSourcePath, aApplicationPath, error_, updatedFiles, logger_)) == false; ++i) {
			logger_.log("Updating failed, retrying after one second...");
			Thread::sleep(1000);
		}

		if (!success) {
			return false;
		}

		logger_.log(Util::toString(updatedFiles.size()) + " files were updated successfully");

		// Clean up files from old directories

		// Web UI filenames contain unique hashes that will change in each version
		auto removed = cleanExtraFiles(aApplicationPath + "Web-resources" + PATH_SEPARATOR, updatedFiles);
		logger_.log("Web-resources: " + Util::toString(removed) + " obsolete files were removed");
	}

	return true;
}

string Updater::getFinalLogFilePath() noexcept {
	return toLoggerFilePath(AppUtil::getPath(AppUtil::PATH_USER_LOCAL));
}

void Updater::removeUpdater(const string& aInfoFilePath, const string& aUpdaterFilePath, FileLogger& logger) {
	auto updateDirectory = PathUtil::getFilePath(aUpdaterFilePath);
	auto removed = destroyDirectory(updateDirectory);
	logger.log(Util::toString(removed) + " files were removed from the updater directory " + updateDirectory);
	if (PathUtil::fileExists(updateDirectory)) {
		logger.log("WARNING: update directory " + updateDirectory + " could not be removed");
	}

	if (File::deleteFile(aInfoFilePath)) {
		logger.log("Update info XML " + aInfoFilePath + " was removed");
	}
}

optional<Updater::UpdaterInfo> Updater::parseUpdaterInfo(const string& aFilePath, const string& aAppPath) {
	SimpleXML xml;
	xml.fromXML(File(aFilePath, File::READ, File::OPEN).read());
	if (xml.findChild("UpdateInfo")) {
		xml.stepIn();
		if (xml.findChild("DestinationPath")) {
			xml.stepIn();
			const auto& infoAppPath = xml.getData();
			xml.stepOut();

			if (infoAppPath != aAppPath)
				return nullopt;

			if (xml.findChild("UpdaterFile")) {
				xml.stepIn();
				const auto& updaterFile = xml.getData();
				xml.stepOut();

				if (xml.findChild("BuildID")) {
					xml.stepIn();
					auto version = Util::toInt(xml.getData());

					return UpdaterInfo({ updaterFile, version });
				}
			}
		}

	}

	return nullopt;
}

// POST INSTALL CLEANUP
bool Updater::checkAndCleanUpdaterFiles(const string& aAppPath, string& updaterFile_, bool aUpdateAttempted) {
	const auto infoFileList = File::findFiles(UPDATE_TEMP_DIR, "UpdateInfo_*");
	if (infoFileList.empty()) {
		return false;
	}

	if (aUpdateAttempted) {
		// Save the log before the temp directory gets deleted 
		try {
			auto tempLogFilePath = toLoggerFilePath(UPDATE_TEMP_DIR);
			File::renameFile(tempLogFilePath, getFinalLogFilePath());
		} catch (...) {
			// ...
		}
	}

	FileLogger logger(getFinalLogFilePath(), false);
	if (aUpdateAttempted) {
		logger.log("New instance was started, cleaning up files...");
	}

	for (const auto& infoFilePath : infoFileList) {
		if (PathUtil::getFileExt(infoFilePath) != ".xml") {
			continue;
		}

		try {
			auto updaterInfo = parseUpdaterInfo(infoFilePath, aAppPath);
			if (!updaterInfo) {
				continue;
			}


			if (updaterInfo->version <= BUILD_NUMBER || aUpdateAttempted) {
				// We have an old update for this instance, delete the files
				removeUpdater(infoFilePath, updaterInfo->updaterFilePath, logger);
				continue;
			}

			updaterFile_ = updaterInfo->updaterFilePath;
			return true;
		} catch (const Exception& e) {
			dcassert(0);
			logger.log("Failed to read updater info file " + infoFilePath + " (" + e.getError() + ")");
		}
	}

	return false;
}

} // namespace dcpp