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

#include "stdinc.h"

#include "ShareScannerManager.h"

#include <airdcpp/AirUtil.h>
#include <airdcpp/FilteredFile.h>
#include <airdcpp/File.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/QueueManager.h>
#include <airdcpp/ShareManager.h>
#include <airdcpp/StringTokenizer.h>
#include <airdcpp/TimerManager.h>

#include <airdcpp/concurrency.h>

namespace dcpp {

#ifdef ATOMIC_FLAG_INIT
atomic_flag ShareScannerManager::scanning = ATOMIC_FLAG_INIT;
#else
atomic_flag ShareScannerManager::scanning;
#endif

ShareScannerManager::ShareScannerManager() : stop(false), tasks(false) {
	QueueManager::getInstance()->bundleCompletionHook.addSubscriber(SHARE_SCANNER_HOOK_ID, STRING(SHARE_SCANNER), HOOK_HANDLER(ShareScannerManager::bundleCompletionHook));
	QueueManager::getInstance()->fileCompletionHook.addSubscriber(SFV_CHECKER_HOOK_ID, STRING(SHARE_SCANNER), HOOK_HANDLER(ShareScannerManager::fileCompletionHook));

	// Case sensitive
	releaseReg.assign(AirUtil::getReleaseRegBasic());
	simpleReleaseReg.assign("(([A-Z0-9]\\S{3,})-([A-Za-z0-9_]{2,}))");
	longReleaseReg.assign(AirUtil::getReleaseRegLong(false));

	// Files
	rarReg.assign("(.+\\.((r\\w{2})|(0\\d{2})))", boost::regex_constants::icase);
	rarMp3Reg.assign("(.+\\.((r\\w{2})|(0\\d{2})|(mp3)|(flac)))", boost::regex_constants::icase);
	zipReg.assign("(.+\\.zip)", boost::regex_constants::icase);
	mvidReg.assign("(.+\\.(m2v|avi|mkv|mp(e)?g))", boost::regex_constants::icase);
	sampleExtrasReg.assign("(.*(jp(e)?g|png|vob))", boost::regex_constants::icase);
	extraRegs[AUDIOBOOK].assign("(.+\\.(jp(e)?g|png|m3u|cue|zip|sfv|nfo))", boost::regex_constants::icase);
	extraRegs[FLAC].assign("(.+\\.(jp(e)?g|png|m3u|cue|log|sfv|nfo))", boost::regex_constants::icase);
	extraRegs[NORMAL].assign("(.+\\.(jp(e)?g|png|m3u|cue|diz|sfv|nfo))", boost::regex_constants::icase);
	zipFolderReg.assign("(.+\\.(jp(e)?g|png|diz|zip|nfo|sfv))", boost::regex_constants::icase);

	// Other directories
	emptyDirReg.assign("(\\S*(((nfo|dir).?fix)|nfo.only)\\S*)", boost::regex_constants::icase);
	audioBookReg.assign(".+(-|\\()AUDIOBOOK(-|\\)).+", boost::regex_constants::icase);
	flacReg.assign(".+(-|\\()(LOSSLESS|FLAC)((-|\\)).+)?", boost::regex_constants::icase);
	subDirReg.assign("((((DVD)|(CD)|(DIS(K|C))).?([0-9](0-9)?))|(Sample)|(Cover(s)?)|(.{0,5}Sub(s)?))", boost::regex_constants::icase);
	subReg.assign("(.{0,8}[Ss]ub(s|pack)?)", boost::regex_constants::icase);
	diskReg.assign(R"((DVD|CD|(DIS(K|C))).?[0-9](0-9)?((\.|-|_|\s).+)?)", boost::regex_constants::icase);
}

ShareScannerManager::~ShareScannerManager() { 
	Stop();
	join();
}

void ShareScannerManager::scanShare(const StringList& paths) noexcept{
	if (scanning.test_and_set()) {
		LogManager::getInstance()->message(STRING(SCAN_RUNNING), LogMessage::SEV_INFO);
		return;
	}

	tasks.addTask([=] { runShareScan(paths); });

	start();

	LogManager::getInstance()->message(STRING(SCAN_STARTED), LogMessage::SEV_INFO);
}

void ShareScannerManager::checkSfv(const StringList& paths) noexcept{
	if (scanning.test_and_set()) {
		LogManager::getInstance()->message(STRING(SCAN_RUNNING), LogMessage::SEV_INFO);
		return;
	}

	tasks.addTask([=] { runSfvCheck(paths); });

	start();

	LogManager::getInstance()->message(STRING(CRC_STARTED), LogMessage::SEV_INFO);
}

void ShareScannerManager::Stop() {
	stop = true;
}

void ShareScannerManager::runSfvCheck(const StringList& rootPaths) {
	scanFolderSize = 0;
	crcOk = 0;
	crcInvalid = 0;
	checkFailed = 0;

	/* Get the total size and dirs */
	SFVScanList sfvDirPaths;
	StringList sfvFilePaths;
	for (auto& path : rootPaths) {
		if (path.back() == PATH_SEPARATOR) {
			prepareSFVScanDir(path, sfvDirPaths);
		} else {
			prepareSFVScanFile(path, sfvFilePaths);
		}
	}

	/* Scan root files */
	if (!sfvFilePaths.empty()) {
		DirSFVReader sfv(Util::getFilePath(rootPaths.front()));
		for (auto& path : sfvFilePaths) {
			if (stop)
				break;

			checkFileSFV(path, sfv, false);
		}
	}

	/* Scan all directories */
	for (auto& i : sfvDirPaths) {
		if (stop)
			break;

		File::forEachFile(i.first, "*", [&](const FilesystemItem& aInfo) {
			if (stop || aInfo.isDirectory)
				return;

			checkFileSFV(aInfo.name, i.second, true);
		});
	}


	/* Report */
	if (stop) {
		LogManager::getInstance()->message(STRING(CRC_STOPPED), LogMessage::SEV_INFO);
	} else {
		LogManager::getInstance()->message(STRING_F(CRC_FINISHED, crcOk % crcInvalid % checkFailed), LogMessage::SEV_INFO);
	}
}

void ShareScannerManager::runShareScan(const StringList& aPaths) {
	ScanType scanType = TYPE_PARTIAL;
	auto rootPaths = aPaths;

	if (rootPaths.empty()) {
		ShareManager::getInstance()->getRootPaths(rootPaths);
		scanType = TYPE_FULL;
	}

	/* Scan for missing files */

	ScanInfoList scanners;
	for (auto& rootPath : rootPaths) {
		scanners.emplace_back(rootPath, ScanInfo::TYPE_COLLECT_LOG, true);
	}

	try {
		TaskScheduler s;
		parallel_for_each(scanners.begin(), scanners.end(), [&](ScanInfo& s) {
			if (!s.rootPath.empty()) {
				// TODO: FIX LINUX
				FileFindIter i(s.rootPath, Util::emptyString, false);
				if (!i->isHidden()) {
					scanDir(s.rootPath, s);
					if (SETTING(CHECK_DUPES) && (scanType == TYPE_PARTIAL || scanType == TYPE_FULL))
						findDupes(s.rootPath, s);

					find(s.rootPath, s);
				}
			}
		});
	} catch (std::exception& e) {
		LogManager::getInstance()->message("Scanning the share failed: " + string(e.what()), LogMessage::SEV_INFO);
	}

	if (!stop) {
		//merge the results
		ScanInfo total(Util::emptyString, ScanInfo::TYPE_COLLECT_LOG, true);
		for (auto& s : scanners) {
			s.merge(total);
		}

		string report;
		if (scanType == TYPE_FULL) {
			report = CSTRING(SCAN_SHARE_FINISHED);
		} else if (scanType == TYPE_PARTIAL) {
			report = CSTRING(SCAN_FOLDER_FINISHED);
		}

		if (!total.scanMessage.empty()) {
			report += " ";
			report += CSTRING(SCAN_PROBLEMS_FOUND);
			report += ":  ";
			report += total.getResults();
			report += ". " + STRING(SCAN_RESULT_NOTE);

			if (SETTING(LOG_SHARE_SCANS)) {
				auto path = Util::validatePath(Util::formatTime(SETTING(LOG_DIRECTORY) + SETTING(LOG_SHARE_SCAN_PATH), time(nullptr)));
				try {
					File::ensureDirectory(path);

					File f(path, File::WRITE, File::OPEN | File::CREATE);
					f.setEndPos(0);
					f.write(total.scanMessage);

				}
				catch (const FileException& e) {
					LogManager::getInstance()->message(STRING_F(SAVE_FAILED_X, path % e.getError()), LogMessage::SEV_ERROR);
				}
			}

			char buf[255];
			time_t time = GET_TIME();
			tm* _tm = localtime(&time);
			strftime(buf, 254, "%c", _tm);

			fire(ScannerManagerListener::ScanFinished(), total.scanMessage, STRING_F(SCANNING_RESULTS_ON, string(buf)));
		} else {
			report += ", ";
			report += CSTRING(SCAN_NO_PROBLEMS);
		}

		LogManager::getInstance()->message(report, LogMessage::SEV_INFO);
	}

	dupeDirs.clear();
}

int ShareScannerManager::run() {
	stop = false;
	while (tasks.dispatch())
		//...
	
	scanning.clear();
	return 0;
}

void ShareScannerManager::ScanInfo::merge(ScanInfo& collect) const {
	collect.missingFiles += missingFiles;
	collect.missingSFV += missingSFV;
	collect.missingNFO += missingNFO;
	collect.extrasFound += extrasFound;
	collect.noReleaseFiles += noReleaseFiles;
	collect.emptyFolders += emptyFolders;
	collect.dupesFound += dupesFound;
	collect.disksMissing += disksMissing;
	collect.invalidSFVFiles += invalidSFVFiles;

	collect.scanMessage += scanMessage;
}

bool ShareScannerManager::validateShare(const string& aPath, bool aSkipCheckQueue) {
	if (SETTING(CHECK_USE_SKIPLIST)) {
		try {
			ShareManager::getInstance()->validatePath(aPath, aSkipCheckQueue);
		} catch (const Exception&) {
			return false;
		}
	}

	return true;
}

void ShareScannerManager::find(const string& aPath, ScanInfo& aScanInfo) noexcept {
	if(stop)
		return;
	
	File::forEachFile(aPath, "*", [&](const FilesystemItem& aInfo) {
		if (!aInfo.isDirectory || stop)
			return;

		auto currentDir = aInfo.getPath(aPath);

		if (aScanInfo.isManualShareScan) {
			if (QueueManager::getInstance()->findDirectoryBundle(currentDir)) {
				return;
			}

			if (SETTING(CHECK_USE_SKIPLIST) && !ShareManager::getInstance()->isRealPathShared(currentDir))
				return;
		}

		scanDir(currentDir, aScanInfo);
		if (SETTING(CHECK_DUPES) && aScanInfo.isManualShareScan)
			findDupes(currentDir, aScanInfo);

		find(currentDir, aScanInfo);
	});
}


void ShareScannerManager::findDupes(const string& path, ScanInfo& aScan) noexcept {
	if(path.empty())
		return;
	
	string dirName = Util::getLastDir(path);

	//only match release names here
	if (!regex_match(dirName, releaseReg))
		return;
	
	{
		auto dirNameLower = Text::toLower(dirName);

		WLock l(cs);
		auto dupes = dupeDirs.equal_range(dirNameLower);
		if (dupes.first != dupes.second) {
			aScan.dupesFound++;

			//list all dupes here
			for(auto k = dupes.first; k != dupes.second; ++k) {
				reportMessage(STRING_F(X_IS_SAME_THAN, path % k->second), aScan, false);
			}
		}

		dupeDirs.emplace(dirNameLower, path);
	}
}

void ShareScannerManager::scanDir(const string& aPath, ScanInfo& aScan) noexcept {
	if(aPath.empty())
		return;

	int nfoFiles = 0;
	StringList sfvFileList, fileList, folderList;
	for (FileFindIter i(aPath, "*"); i != FileFindIter(); ++i) {
		if (i->isHidden()) {
			continue;
		}

		auto fileName = i->getFileName();
		if (!validateShare(aPath + fileName + (i->isDirectory() ? PATH_SEPARATOR_STR : Util::emptyString), !aScan.isManualShareScan)) {
			continue;
		}

		auto fileNameLower = Text::toLower(fileName);
		if (i->isDirectory()) {
			folderList.push_back(fileNameLower);
			continue;
		}

		if (SETTING(CHECK_IGNORE_ZERO_BYTE)) {
			if (i->getSize() <= 0) {
				continue;
			}
		}

		auto ext = Util::getFileExt(fileNameLower);
		if (ext == ".nfo") {
			nfoFiles++;
		} else if (ext == ".sfv") {
			sfvFileList.push_back(aPath + fileName);
		}

		fileList.push_back(fileNameLower);
	}

	if (fileList.empty()) {
		//check if there are folders
		if (folderList.empty()) {
			if (SETTING(CHECK_EMPTY_DIRS)) {
				reportMessage(STRING(DIR_EMPTY) + ": " + aPath, aScan);
				aScan.emptyFolders++;
			}
			return;
		}
	}

	if (SETTING(CHECK_DISK_COUNTS)) {
		StringList disks;
		copy_if(folderList.begin(), folderList.end(), back_inserter(disks), [this](const string& s) { return regex_match(s, diskReg); });
		if (!disks.empty()) {
			int expectedCount = 0;

			// find the maximum disk number
			for (const auto& s : disks) {
				auto pos = s.find_first_of("0123456789");
				if (pos != string::npos) {
					int num = atoi(s.data() + pos);
					expectedCount = max(num, expectedCount);
				}
			}

			if (disks.size() == 1 || expectedCount > static_cast<int>(disks.size())) {
				reportMessage(STRING(DISKS_MISSING) + " " + aPath, aScan);
				aScan.disksMissing++;
			}
		}
	}

	bool isSample=false, isRelease=false, isZipRls=false, found=false, extrasInFolder = false;

	auto dirName = Util::getLastDir(aPath);

	/* No release files at all? */
	if (!fileList.empty() && ((nfoFiles + sfvFileList.size()) == (int)fileList.size()) && (SETTING(CHECK_EMPTY_RELEASES))) {
		if (!regex_match(dirName, emptyDirReg)) {
			if (folderList.empty()) {
				reportMessage(STRING(RELEASE_FILES_MISSING) + " " + aPath, aScan);
				aScan.noReleaseFiles++;
				return;
			}
		}
	}

	if(SETTING(CHECK_NFO) || SETTING(CHECK_SFV) || SETTING(CHECK_EXTRA_FILES) || SETTING(CHECK_EXTRA_SFV_NFO)) {
		//Check for multiple NFO or SFV files
		if (SETTING(CHECK_EXTRA_SFV_NFO)) {
			if (nfoFiles > 1) {
				reportMessage(STRING(MULTIPLE_NFO) + " " + aPath, aScan);
				aScan.extrasFound++;
				extrasInFolder = true;
			}
			if (sfvFileList.size() > 1) {
				reportMessage(STRING(MULTIPLE_SFV) + " " + aPath, aScan);
				if (!extrasInFolder) {
					extrasInFolder = true;
					aScan.extrasFound++;
				}
			}
		}

		//Check if it's a sample folder
		isSample = (strcmp(Text::toLower(dirName).c_str(), "sample") == 0);

		if (nfoFiles == 0 || sfvFileList.empty() || isSample || SETTING(CHECK_EXTRA_FILES)) {
			//Check if it's a RAR/Music release folder
			isRelease = AirUtil::listRegexMatch(fileList, (SETTING(CHECK_MP3_DIR) ? rarMp3Reg : rarReg));

			if (!isRelease) {
				//Check if it's a zip release folder
				if (regex_match(dirName, simpleReleaseReg)) {
					isZipRls = AirUtil::listRegexMatch(fileList, zipReg);
				}

				//Check if it's a Mvid release folder
				if (!isZipRls && regex_match(dirName, longReleaseReg)) {
					isRelease = AirUtil::listRegexMatch(fileList, mvidReg);
				}

				//Report extra files in a zip folder
				if (isZipRls && SETTING(CHECK_EXTRA_FILES) && sfvFileList.empty()) {
					AirUtil::listRegexSubtract(fileList, zipFolderReg);
					if (!fileList.empty()) {
						reportMessage(STRING_F(EXTRA_FILES_RLSDIR_X, aPath.c_str() % Util::toString(", ", fileList)), aScan);
						aScan.extrasFound++;
					}
				}
			}

			//Report extra files in sample folder
			if (SETTING(CHECK_EXTRA_FILES) && isSample) {
				found = false;
				if (fileList.size() > 1) {
					//check that all files have the same extension.. otherwise there are extras
					string extension;
					for(auto& fileName: fileList) {
						// ignore image files
						// some strange releases also have extra vob files
						if (boost::regex_match(Util::getFileExt(fileName), sampleExtrasReg))
							continue;
						
						string loopExt = Util::getFileExt(fileName);
						if (!extension.empty() && loopExt != extension) {
							found = true;
							break;
						}
						extension = loopExt;
					}
				}

				if (nfoFiles > 0 || !sfvFileList.empty() || isRelease || found) {
					reportMessage(STRING_F(EXTRA_FILES_SAMPLEDIR_X, aPath), aScan);
					aScan.extrasFound++;
				}
			}

			if (isSample)
				return;

			//Report missing NFO
			if (SETTING(CHECK_NFO) && nfoFiles == 0 && regex_match(dirName, simpleReleaseReg)) {
				found = false;
				if (fileList.empty()) {
					found = true;
					//check if there are multiple disks and nfo inside them
					for(auto& subDirName: folderList) {
						if (regex_match(subDirName, subDirReg)) {
							found = false;
							auto filesListSub = File::findFiles(aPath + subDirName + PATH_SEPARATOR, "*.nfo", File::TYPE_FILE);
							if (!filesListSub.empty()) {
								found = true;
								break;
							}
						}
					}
				}

				if (!found) {
					reportMessage(STRING(NFO_MISSING) + aPath, aScan);
					aScan.missingNFO++;
				}
			}

			//Report missing SFV
			if (sfvFileList.empty() && isRelease) {
				//avoid extra matches
				if (!regex_match(dirName,subReg) && SETTING(CHECK_SFV)) {
					reportMessage(STRING(SFV_MISSING) + aPath, aScan);
					aScan.missingSFV++;
				}
				return;
			}
		}
	}

	if (sfvFileList.empty())
		return;


	/* Check for missing files */
	bool hasValidSFV = false;

	int releaseFiles = 0, loopMissing = 0;

	DirSFVReader sfv(aPath, sfvFileList);
	sfv.read([&](const string& aFileName) {
		hasValidSFV = true;
		releaseFiles++;

		auto s = std::find(fileList.begin(), fileList.end(), aFileName);
		if(s == fileList.end()) { 
			loopMissing++;
			if (SETTING(CHECK_MISSING))
				reportMessage(STRING(FILE_MISSING) + " " + aPath + aFileName, aScan);
		} else {
			fileList.erase(s);
		}
	});

	if (SETTING(CHECK_MISSING))
		aScan.missingFiles += loopMissing;

	if (SETTING(CHECK_INVALID_SFV)) {
		aScan.invalidSFVFiles += sfv.getFailedFiles().size();
	}

	/* Extras in folder? */
	releaseFiles = releaseFiles - loopMissing;

	if(SETTING(CHECK_EXTRA_FILES) && ((int)fileList.size() > nfoFiles + sfvFileList.size()) && hasValidSFV) {
		//Find allowed extra files from the release folder
		int8_t extrasType = NORMAL;
		if (regex_match(dirName, audioBookReg)) {
			extrasType = AUDIOBOOK;
		} else if (regex_match(dirName, flacReg)) {
			extrasType = FLAC;
		}

		AirUtil::listRegexSubtract(fileList, extraRegs[extrasType]);
		if (!fileList.empty()) {
			reportMessage(STRING_F(EXTRA_FILES_RLSDIR_X, aPath % Util::toString(", ", fileList)), aScan);
			if (!extrasInFolder)
				aScan.extrasFound++;
		}
	}
}

void ShareScannerManager::prepareSFVScanDir(const string& aPath, SFVScanList& dirs) noexcept {
	DirSFVReader sfv(aPath);

	/* Get the size and see if all files in the sfv exists 
	   TODO: FIX LINUX
	*/
	if (sfv.hasSFV()) {
		sfv.read([&](const string& fileName) {
			if (Util::fileExists(aPath + fileName)) {
				scanFolderSize = scanFolderSize + File::getSize(aPath + fileName);
			} else {
				LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + aPath + fileName, LogMessage::SEV_WARNING);
				checkFailed++;
			}
		});
		dirs.emplace_back(aPath, sfv);
	}

	/* Recursively scan subfolders */
	File::forEachFile(aPath, "*", [&](const FilesystemItem& aInfo) {
		if (aInfo.isDirectory) {
			prepareSFVScanDir(aInfo.getPath(aPath), dirs);
		}
	});
}

void ShareScannerManager::prepareSFVScanFile(const string& aPath, StringList& files) noexcept {
	if (Util::fileExists(aPath)) {
		scanFolderSize += File::getSize(aPath);
		files.push_back(Util::getFileName(aPath));
	} else {
		LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + aPath, LogMessage::SEV_WARNING);
		checkFailed++;
	}
}

void ShareScannerManager::checkFileSFV(const string& aFileName, DirSFVReader& aSfvReader, bool aIsDirScan) noexcept {
 
	uint64_t checkStart = 0;
	uint64_t checkEnd = 0;

	const auto fileNameLower = Text::toLower(aFileName);
	if(aSfvReader.hasFile(fileNameLower)) {
		// Perform the check
		bool crcMatch = false;
		try {
			checkStart = GET_TICK();
			crcMatch = aSfvReader.isCrcValid(fileNameLower);
			checkEnd = GET_TICK();
		} catch(const FileException& ) {
			// Couldn't read the file to get the CRC(!!!)
			LogManager::getInstance()->message(STRING_F(CRC_FILE_ERROR, (aSfvReader.getPath() + aFileName)), LogMessage::SEV_ERROR);
		}

		// Update the results
		int64_t size = File::getSize(aSfvReader.getPath() + aFileName);
		int64_t speed = 0;
		if(checkEnd > checkStart) {
			speed = size * 1000LL / (checkEnd - checkStart);
		}

		if(crcMatch) {
			crcOk++;
		} else {
			crcInvalid++;
		}

		scanFolderSize = scanFolderSize - size;

		// Report
		if (SETTING(LOG_CRC_OK)) {
			LogManager::getInstance()->message(STRING_F(CRC_FILE_DONE,
				(crcMatch ? STRING(CRC_OK) : STRING(CRC_FAILED)) %
				(aSfvReader.getPath() + aFileName) %
				Util::formatBytes(speed) %
				Util::formatBytes(scanFolderSize)), (crcMatch ? LogMessage::SEV_INFO : LogMessage::SEV_ERROR));
		} else if (!crcMatch) {
				LogManager::getInstance()->message(STRING_F(CRC_FILE_FAILED,
				(aSfvReader.getPath() + aFileName) %
				Util::formatBytes(speed) %
				Util::formatBytes(scanFolderSize)), LogMessage::SEV_ERROR);
		}


	} else if (!aIsDirScan || regex_match(aFileName, rarMp3Reg)) {
		LogManager::getInstance()->message(STRING_F(CRC_NO_SFV, (aSfvReader.getPath() + aFileName)), LogMessage::SEV_WARNING);
		checkFailed++;
	}
}

ActionHookRejectionPtr ShareScannerManager::fileCompletionHook(const QueueItemPtr&, const HookRejectionGetter&) noexcept {
	/*DirSFVReader sfv(Util::getFilePath(aFile->getTarget()));

	auto fileNameLower = Text::toLower(Util::getFileName(aFile->getTarget()));
	if (!sfv.hasFile(fileNameLower)) {
		return nullptr;
	}

	try {
		if (!sfv.isCrcValid(fileNameLower)) {
			LogManager::getInstance()->message(STRING(CRC_FAILED) + ": " + aFile->getTarget(), LogMessage::SEV_ERROR);
			return aErrorGetter(SFV_CHECKER_ERROR_CRC, STRING(CRC_FAILED));
		}
	} catch (const FileException&) {
		LogManager::getInstance()->message(STRING_F(CRC_FILE_ERROR, aFile->getTarget()), LogMessage::SEV_ERROR);
	}*/

	return nullptr;
}

ActionHookRejectionPtr ShareScannerManager::bundleCompletionHook(const BundlePtr& aBundle, const HookRejectionGetter& aErrorGetter) noexcept{
	if (!SETTING(SCAN_DL_BUNDLES) || aBundle->isFileBundle() || !validateShare(aBundle->getTarget(), true)) {
		return nullptr;
	}

	ScanInfo scanner(aBundle->getName(), ScanInfo::TYPE_SYSLOG, false);

	{
		scanDir(aBundle->getTarget(), scanner);
		find(aBundle->getTarget(), scanner);
	}

	auto hasMissing = scanner.hasMissing();
	auto hasExtras = scanner.hasExtras();
	auto hasInvalidSFV = scanner.invalidSFVFiles > 0;

	if (hasMissing || hasExtras || hasInvalidSFV) {
		LogManager::getInstance()->message(STRING_F(SCAN_FAILED_BUNDLE_FINISHED, aBundle->getName()) + scanner.getResults(), LogMessage::SEV_ERROR);

		auto errorId = (hasExtras || hasInvalidSFV) ? SHARE_SCANNER_ERROR_INVALID_CONTENT : SHARE_SCANNER_ERROR_MISSING;
		return aErrorGetter(errorId, scanner.getResults());
	}

	return nullptr;
}

bool ShareScannerManager::onScanSharedDir(const string& aDir, bool report) noexcept {
	if (!SETTING(SCAN_MONITORED_FOLDERS))
		return true;

	ScanInfo scanner(aDir, report ? ScanInfo::TYPE_SYSLOG : ScanInfo::TYPE_NOREPORT, false);

	scanDir(aDir, scanner);
	find(aDir, scanner);

	if (scanner.hasMissing() || scanner.hasExtras()) {
		if (report) {
			string logMsg;
			if (ShareManager::getInstance()->isRealPathShared(aDir)) {
				logMsg += STRING_F(SCAN_SHARE_EXISTING_FAILED, aDir % scanner.getResults());
			} else {
				logMsg += STRING_F(SCAN_SHARE_DIR_FAILED, aDir % scanner.getResults());
			}

			logMsg += ". ";
			logMsg += STRING(FORCE_SHARE_SCAN);

			LogManager::getInstance()->message(logMsg, LogMessage::SEV_ERROR);
		}
		return false;
	}

	return true;
}

void ShareScannerManager::reportMessage(const string& aMessage, ScanInfo& aScan, bool warning /*true*/) noexcept {
	if (aScan.reportType == ScanInfo::TYPE_SYSLOG) {
		LogManager::getInstance()->message(aMessage, warning ? LogMessage::SEV_WARNING : LogMessage::SEV_INFO);
	} else if (aScan.reportType == ScanInfo::TYPE_COLLECT_LOG) {
		aScan.scanMessage += aMessage + "\r\n";
	}
}

bool ShareScannerManager::ScanInfo::hasMissing() const {
	return (missingFiles > 0 || missingNFO > 0 || missingSFV > 0 || noReleaseFiles > 0 || disksMissing > 0);
}

bool ShareScannerManager::ScanInfo::hasExtras() const {
	return extrasFound > 0;
}

string ShareScannerManager::ScanInfo::getResults() const {
	string tmp;
	bool first = true;

	auto checkFirst = [&] {
		if (!first) {
			tmp += ", ";
		}
		first = false;
	};

	if (missingFiles > 0) {
		checkFirst();
		tmp += STRING_F(X_MISSING_RELEASE_FILES, missingFiles);
	}

	if (invalidSFVFiles > 0) {
		checkFirst();
		tmp += STRING_F(X_MISSING_INVALID_SFV_FILES, invalidSFVFiles);
	}

	if (missingSFV > 0) {
		checkFirst();
		tmp += STRING_F(X_MISSING_SFV_FILES, missingSFV);
	}

	if (missingNFO > 0) {
		checkFirst();
		tmp += STRING_F(X_MISSING_NFO_FILES, missingNFO);
	}

	if (extrasFound > 0) {
		checkFirst();
		tmp += STRING_F(X_FOLDERS_EXTRAS, extrasFound);
	}

	if (noReleaseFiles > 0) {
		checkFirst();
		tmp += STRING_F(X_NO_RELEASE_FILES, noReleaseFiles);
	}

	if (emptyFolders > 0) {
		checkFirst();
		tmp += STRING_F(X_EMPTY_FOLDERS, emptyFolders);
	}

	if (dupesFound > 0) {
		checkFirst();
		tmp += STRING_F(X_DUPE_FOLDERS, dupesFound);
	}

	if (disksMissing > 0) {
		checkFirst();
		tmp += STRING_F(X_MISSING_DISKS, disksMissing);
	}

	return tmp;
}

} // namespace dcpp
