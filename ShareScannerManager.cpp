/*
 * Copyright (C) 2011-2012 AirDC++ Project
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
#include "HashManager.h"

#include "FileReader.h"
#include "LogManager.h"
#include "ShareManager.h"
#include "StringTokenizer.h"
#include "FilteredFile.h"
#include "File.h"
#include "Wildcards.h"
#include "QueueManager.h"
#include "format.h"

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>

#ifndef _WIN32
#include <dirent.h>
#include <fnmatch.h>
#endif

namespace dcpp {

ShareScannerManager::ShareScannerManager() : scanning(false) {
	releaseReg.assign(AirUtil::getReleaseRegBasic());
	simpleReleaseReg.assign("(([A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))");
	emptyDirReg.assign("(\\S*(((nfo|dir).?fix)|nfo.only)\\S*)", boost::regex_constants::icase);
	rarReg.assign("(.+\\.((r\\w{2})|(0\\d{2})))");
	rarMp3Reg.assign("(.+\\.((r\\w{2})|(0\\d{2})|(mp3)|(flac)))");
	audioBookReg.assign(".+(-|\\()AUDIOBOOK(-|\\)).+", boost::regex_constants::icase);
	flacReg.assign(".+(-|\\()(LOSSLESS|FLAC)((-|\\)).+)?", boost::regex_constants::icase);
	zipReg.assign("(.+\\.zip)");
	longReleaseReg.assign(AirUtil::getReleaseRegLong(false));
	mvidReg.assign("(.+\\.(m2v|avi|mkv|mp(e)?g))");
	proofImageReg.assign("(.*(jp(e)?g|png))", boost::regex_constants::icase);
	subDirReg.assign("((((DVD)|(CD)|(DIS(K|C))).?([0-9](0-9)?))|(Sample)|(Cover(s)?)|(.{0,5}Sub(s)?))", boost::regex_constants::icase);
	extraRegs[AUDIOBOOK].assign("(.+\\.(jp(e)?g|png|m3u|cue|zip|sfv|nfo))");
	extraRegs[FLAC].assign("(.+\\.(jp(e)?g|png|m3u|cue|log|sfv|nfo))");
	extraRegs[NORMAL].assign("(.+\\.(jp(e)?g|png|m3u|cue|diz|sfv|nfo))");
	zipFolderReg.assign("(.+\\.(jp(e)?g|png|diz|zip|nfo|sfv))");
	subReg.assign("(.{0,8}[Ss]ub(s|pack)?)");
}

ShareScannerManager::~ShareScannerManager() { 
	Stop();
	join();
}

int ShareScannerManager::scan(StringList paths, bool sfv /*false*/) {
	stop = false;
	//initiate the thread always here for now.
	if(scanning.test_and_set()){
		LogManager::getInstance()->message(STRING(SCAN_RUNNING), LogManager::LOG_INFO);
		return 1;
	}
	isCheckSFV = false;
	isDirScan = false;

	if(sfv) {
		isCheckSFV = true;
		rootPaths = paths;
	} else if(!paths.empty())  {
		isDirScan = true;
		rootPaths = paths;
	} else {
		ShareManager::getInstance()->getParentPaths(rootPaths);
	}

	start();
	

	if(sfv) {
		LogManager::getInstance()->message(STRING(CRC_STARTED), LogManager::LOG_INFO);
		crcOk = 0;
		crcInvalid = 0;
		checkFailed = 0;
	} else {
		LogManager::getInstance()->message(STRING(SCAN_STARTED), LogManager::LOG_INFO);
	}
	return 0;
}

void ShareScannerManager::Stop() {
	rootPaths.clear();
	stop = true;
}

int ShareScannerManager::run() {
	if (isCheckSFV) {

		/* Get the total size and dirs */
		scanFolderSize = 0;
		SFVScanList sfvDirPaths;
		StringList sfvFilePaths;
		for(auto i = rootPaths.begin(); i != rootPaths.end(); i++) {
			string path = *i;
			if(path[path.size() -1] == PATH_SEPARATOR) {
				prepareSFVScanDir(path, sfvDirPaths);
			} else {
				prepareSFVScanFile(path, sfvFilePaths);
			}
		}

		/* Scan root files */
		if (!sfvFilePaths.empty()) {
			DirSFVReader sfv = DirSFVReader(Util::getFilePath(rootPaths.front()));
			for(auto i = sfvFilePaths.begin(); i != sfvFilePaths.end(); i++) {
				if (stop)
					break;

				checkFileSFV(*i, sfv, false);
			}
		}

		/* Scan all directories */
		for(auto i = sfvDirPaths.begin(); i != sfvDirPaths.end(); i++) {
			if (stop)
				break;

			auto files = findFiles(i->first, "*", false, false);
			for(auto s = files.begin(); s != files.end(); ++s) {
				if (stop)
					break;

				checkFileSFV(*s, i->second, true);
			}
		}


		/* Report */
		if (stop) {
			LogManager::getInstance()->message(STRING(CRC_STOPPED), LogManager::LOG_INFO);
		} else {
			LogManager::getInstance()->message(STRING_F(CRC_FINISHED, crcOk % crcInvalid % checkFailed), LogManager::LOG_INFO);
		}
	} else {
		/* Scan for missing files */
		QueueManager::getInstance()->getUnfinishedPaths(bundleDirs);
		sort(bundleDirs.begin(), bundleDirs.end());

		string dir;
		int missingFiles = 0, dupesFound = 0, extrasFound = 0, missingNFO = 0, missingSFV = 0, emptyFolders = 0;

		for(auto i = rootPaths.begin(); i != rootPaths.end(); ++i) {
			dir = *i;
			DWORD attrib = GetFileAttributes(Text::toT(dir).c_str());
			if(attrib != INVALID_FILE_ATTRIBUTES && attrib != FILE_ATTRIBUTE_HIDDEN && attrib != FILE_ATTRIBUTE_SYSTEM && attrib != FILE_ATTRIBUTE_OFFLINE) {
				if (matchSkipList(Util::getDir(dir, false, true))) {
					continue;
				}
				if (std::binary_search(bundleDirs.begin(), bundleDirs.end(), dir)) {
					continue;
				}
				scanDir(dir, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
				find(dir, missingFiles, missingSFV, missingNFO, extrasFound, dupesFound, emptyFolders, false);
			}
		}
		reportResults(Util::emptyString, isDirScan ? TYPE_PARTIAL : TYPE_FULL, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders, dupesFound);
		bundleDirs.clear();
		dupeDirs.clear();
	}
	
	scanning.clear();
	rootPaths.clear();
	return 0;
}

bool ShareScannerManager::matchSkipList(const string& dir) {
	if (SETTING(CHECK_USE_SKIPLIST)) {
		return BOOLSETTING(SHARE_SKIPLIST_USE_REGEXP) ? AirUtil::matchSkiplist(dir) : Wildcard::patternMatch(Text::utf8ToAcp(dir), Text::utf8ToAcp(SETTING(SKIPLIST_SHARE)), '|');
	}
	return false;
}

void ShareScannerManager::find(const string& path, int& missingFiles, int& missingSFV, int& missingNFO, int& extrasFound, int& dupesFound, int& emptyFolders, bool isBundleScan) {
	if(path.empty())
		return;

	string dir;
	StringList dirs;
	
	for(FileFindIter i(path + "*"); i != FileFindIter(); ++i) {
		try {
			if(i->isDirectory() && strcmpi(i->getFileName().c_str(), ".") != 0 && strcmpi(i->getFileName().c_str(), "..") != 0){
				if (matchSkipList(i->getFileName())) {
					continue;
				}
				dir = path + i->getFileName() + PATH_SEPARATOR;
				
				if (!isBundleScan && std::binary_search(bundleDirs.begin(), bundleDirs.end(), dir)) {
					continue;
				}
				if(!i->isHidden()) {
					scanDir(dir, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
					if(SETTING(CHECK_DUPES) && !isBundleScan)
						findDupes(dir, dupesFound);
					dirs.push_back(dir);
				}
			}
		} catch(const FileException&) { } 
	}

	for(auto j = dirs.begin(); j != dirs.end(); j++) {
		find(*j, missingFiles, missingSFV, missingNFO, extrasFound, dupesFound, emptyFolders, isBundleScan);
	}
}


void ShareScannerManager::findDupes(const string& path, int& dupesFound) throw(FileException) {
	if(path.empty())
		return;
	
	string dirName = Util::getLastDir(path);
	string listfolder;

	//only match release names here
	if (!regex_match(dirName, releaseReg))
		return;
	
	auto dupes = dupeDirs.equal_range(dirName);
	if (dupes.first != dupes.second) {
		dupesFound++;

		//list all dupes here
		for(auto k = dupes.first; k != dupes.second; ++k) {
			LogManager::getInstance()->message(STRING_F(X_IS_SAME_THAN, path % k->second), LogManager::LOG_INFO);
		}
	}

	dupeDirs.insert(make_pair(dirName, path));
}

StringList ShareScannerManager::findFiles(const string& path, const string& pattern, bool dirs /*false*/, bool aMatchSkipList) {
	StringList ret;

	WIN32_FIND_DATA data;
	HANDLE hFind;

	hFind = ::FindFirstFile(Text::toT(Util::FormatPath(path + pattern)).c_str(), &data);
	if(hFind != INVALID_HANDLE_VALUE) {
		do {
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) && !(data.dwFileAttributes &FILE_ATTRIBUTE_SYSTEM) && !(data.dwFileAttributes &FILE_ATTRIBUTE_SYSTEM)) {
				if (aMatchSkipList && matchSkipList(Text::fromT(data.cFileName))) {
					continue;
				}
				if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					if (dirs && Text::fromT(data.cFileName)[0] != '.') {
						ret.push_back(Text::fromT(data.cFileName));
					}
				} else if (!dirs) {
					if (SETTING(CHECK_IGNORE_ZERO_BYTE)) {
						if (File::getSize(path + Text::fromT(data.cFileName)) <= 0) {
							continue;
						}
					}
					ret.push_back(Text::toLower(Text::fromT(data.cFileName)));
				}
			}
		} while(::FindNextFile(hFind, &data));

		::FindClose(hFind);
	}
	return ret;
}

void ShareScannerManager::scanDir(const string& path, int& missingFiles, int& missingSFV, int& missingNFO, int& extrasFound, int& emptyFolders) throw(FileException) {
	if(path.empty())
		return;

	StringList sfvFileList, fileList = findFiles(path, "*", false, false);

	if (fileList.empty()) {
		//check if there are folders
		StringList folderList = findFiles(path, "*", true, true);
		if (folderList.empty()) {
			if (SETTING(CHECK_EMPTY_DIRS)) {
				LogManager::getInstance()->message(STRING(DIR_EMPTY) + " " + path, LogManager::LOG_INFO);
				emptyFolders++;
			}
			return;
		}
	}

	int nfoFiles=0, sfvFiles=0;
	bool isSample=false, isRelease=false, isZipRls=false, found=false, extrasInFolder = false;

	string dirName = Util::getLastDir(path);

	// Find NFO and SFV files
	for(auto i = fileList.begin(); i != fileList.end(); ++i) {
		if (Util::getFileExt(*i) == ".nfo") {
			nfoFiles++;
		} else if (Util::getFileExt(*i) == ".sfv") {
			sfvFileList.push_back(path + *i);
			sfvFiles++;
		}
	}

	/* No release files at all? */
	if (!fileList.empty() && ((nfoFiles + sfvFiles) == (int)fileList.size()) && (SETTING(CHECK_EMPTY_RELEASES))) {
		if (!regex_match(dirName, emptyDirReg)) {
			StringList folderList = findFiles(path, "*", true, true);
			if (folderList.empty()) {
				LogManager::getInstance()->message(STRING(RELEASE_FILES_MISSING) + " " + path, LogManager::LOG_WARNING);
				return;
			}
		}
	}

	if(SETTING(CHECK_NFO) || SETTING(CHECK_SFV) || SETTING(CHECK_EXTRA_FILES) || SETTING(CHECK_EXTRA_SFV_NFO)) {
		//Check for multiple NFO or SFV files
		if (SETTING(CHECK_EXTRA_SFV_NFO)) {
			if (nfoFiles > 1) {
				LogManager::getInstance()->message(STRING(MULTIPLE_NFO) + path, LogManager::LOG_INFO);
				extrasFound++;
				extrasInFolder = true;
			}
			if (sfvFiles > 1) {
				LogManager::getInstance()->message(STRING(MULTIPLE_SFV) + path, LogManager::LOG_INFO);
				if (!extrasInFolder) {
					extrasInFolder = true;
					extrasFound++;
				}
			}
		}

		//Check if it's a sample folder
		isSample = (strcmp(Text::toLower(dirName).c_str(), "sample") == 0);

		if (nfoFiles == 0 || sfvFiles == 0 || isSample || SETTING(CHECK_EXTRA_FILES)) {
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
				if (isZipRls && SETTING(CHECK_EXTRA_FILES) && sfvFiles == 0) {
					AirUtil::listRegexSubtract(fileList, zipFolderReg);
					if (!fileList.empty()) {
						LogManager::getInstance()->message(STRING_F(EXTRA_FILES_RLSDIR_X, path.c_str() % Util::toString(", ", fileList)), LogManager::LOG_INFO);
						extrasFound++;
					}
				}
			}

			//Report extra files in sample folder
			if (SETTING(CHECK_EXTRA_FILES) && isSample) {
				found = false;
				if (fileList.size() > 1) {
					//check that all files have the same extension.. otherwise there are extras
					string extension;
					for(auto i = fileList.begin(); i != fileList.end(); ++i) {
						//ignore image files
						if (boost::regex_match(Util::getFileExt(*i), proofImageReg))
							continue;
						
						string loopExt = Util::getFileExt(*i);
						if (!extension.empty() && loopExt != extension) {
							found = true;
							break;
						}
						extension = loopExt;
					}
				}

				if (nfoFiles > 0 || sfvFiles > 0 || isRelease || found) {
					LogManager::getInstance()->message(STRING_F(EXTRA_FILES_SAMPLEDIR_X, path), LogManager::LOG_INFO);
					extrasFound++;
				}
			}

			if (isSample)
				return;

			//Report missing NFO
			if (SETTING(CHECK_NFO) && nfoFiles == 0 && regex_match(dirName, simpleReleaseReg)) {
				found = false;
				if (fileList.empty()) {
					found = true;
					StringList folderList = findFiles(path, "*", true, true);
					//check if there are multiple disks and nfo inside them
					for(auto i = folderList.begin(); i != folderList.end(); ++i) {
						if (regex_match(*i, subDirReg)) {
							found = false;
							StringList filesListSub = findFiles(path + *i + "\\", "*.nfo", false, true);
							if (!filesListSub.empty()) {
								found = true;
								break;
							}
						}
					}
				}

				if (!found) {
					LogManager::getInstance()->message(STRING(NFO_MISSING) + path, LogManager::LOG_INFO);
					missingNFO++;
				}
			}

			//Report missing SFV
			if (sfvFiles == 0 && isRelease) {
				//avoid extra matches
				if (!regex_match(dirName,subReg) && SETTING(CHECK_SFV)) {
					LogManager::getInstance()->message(STRING(SFV_MISSING) + path, LogManager::LOG_INFO);
					missingSFV++;
				}
				return;
			}
		}
	}

	if (sfvFiles == 0)
		return;


	/* Check for missing files */
	string fileName;
	bool hasValidSFV = false;

	int releaseFiles=0, loopMissing=0;

	DirSFVReader sfv = DirSFVReader(path, sfvFileList);
	while (sfv.read(fileName)) {
		hasValidSFV = true;
		releaseFiles++;

		auto s = std::find(fileList.begin(), fileList.end(), fileName);
		if(s == fileList.end()) { 
			loopMissing++;
			if (SETTING(CHECK_MISSING))
				LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + path + fileName, LogManager::LOG_WARNING);
		} else {
			fileList.erase(s);
		}
	}

	if (SETTING(CHECK_MISSING))
		missingFiles += loopMissing;

	/* Extras in folder? */
	releaseFiles = releaseFiles - loopMissing;

	if(SETTING(CHECK_EXTRA_FILES) && ((int)fileList.size() > nfoFiles + sfvFiles) && hasValidSFV) {
		//Find allowed extra files from the release folder
		int8_t extrasType = NORMAL;
		if (regex_match(dirName, audioBookReg)) {
			extrasType = AUDIOBOOK;
		} else if (regex_match(dirName, flacReg)) {
			extrasType = FLAC;
		}

		AirUtil::listRegexSubtract(fileList, extraRegs[extrasType]);
		if (!fileList.empty()) {
			LogManager::getInstance()->message(CSTRING_F(EXTRA_FILES_RLSDIR_X, path % Util::toString(", ", fileList)), LogManager::LOG_INFO);
			if (!extrasInFolder)
				extrasFound++;
		}
	}
}

void ShareScannerManager::prepareSFVScanDir(const string& aPath, SFVScanList& dirs) throw(FileException) {
	DirSFVReader sfv = DirSFVReader(aPath);

	/* Get the size and see if all files in the sfv exists */
	if (sfv.hasSFV()) {
		string fileName;
		while (sfv.read(fileName)) {
			if (Util::fileExists(aPath + fileName)) {
				scanFolderSize = scanFolderSize + File::getSize(aPath + fileName);
			} else {
				LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + aPath + fileName, LogManager::LOG_WARNING);
				checkFailed++;
			}
		}
		dirs.push_back(make_pair(aPath, sfv));
	}

	/* Recursively scan subfolders */
	for(FileFindIter i(aPath + "*"); i != FileFindIter(); ++i) {
		try {
			if (!i->isHidden()) {
				if (i->isDirectory()) {
					prepareSFVScanDir(aPath + i->getFileName() + PATH_SEPARATOR, dirs);
				}
			}
		} catch(const FileException&) { } 
	}
}

void ShareScannerManager::prepareSFVScanFile(const string& aPath, StringList& files) {
	if (Util::fileExists(aPath)) {
		scanFolderSize += File::getSize(aPath);
		files.push_back(Text::toLower(Util::getFileName(aPath)));
	} else {
		LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + aPath, LogManager::LOG_WARNING);
		checkFailed++;
	}
}

void ShareScannerManager::checkFileSFV(const string& aFileName, DirSFVReader& sfv, bool isDirScan) throw(FileException) {
 
	uint64_t checkStart = 0;
	uint64_t checkEnd = 0;

	if(sfv.hasFile(aFileName)) {
		bool crcMatch = false;
		try {
			checkStart = GET_TICK();
			crcMatch = sfv.isCrcValid(aFileName);
			checkEnd = GET_TICK();
		} catch(const FileException& ) {
			// Couldn't read the file to get the CRC(!!!)
			LogManager::getInstance()->message(STRING(CRC_FILE_ERROR) + sfv.getPath() + aFileName, LogManager::LOG_ERROR);
		}

		int64_t size = File::getSize(sfv.getPath() + aFileName);
		int64_t speed = 0;
		if(checkEnd > checkStart) {
			speed = size * _LL(1000) / (checkEnd - checkStart);
		}

		string message;

		if(crcMatch) {
			message = STRING(CRC_OK);
			crcOk++;
		} else {
			message = STRING(CRC_FAILED);
			crcInvalid++;
		}

		message += sfv.getPath() + aFileName + " (" + Util::formatBytes(speed) + "/s)";

		scanFolderSize = scanFolderSize - size;
		message += ", " + STRING(CRC_REMAINING) + Util::formatBytes(scanFolderSize);
		LogManager::getInstance()->message(message, (crcMatch ? LogManager::LOG_INFO : LogManager::LOG_ERROR));


	} else if (!isDirScan || regex_match(aFileName, rarMp3Reg)) {
		LogManager::getInstance()->message(STRING(NO_CRC32) + " " + sfv.getPath() + aFileName, LogManager::LOG_WARNING);
		checkFailed++;
	}
}

bool ShareScannerManager::scanBundle(BundlePtr aBundle) noexcept {
	if (SETTING(SCAN_DL_BUNDLES) && !aBundle->isFileBundle()) {
		int missingFiles = 0, dupesFound = 0, extrasFound = 0, missingNFO = 0, missingSFV = 0, emptyFolders = 0;

		scanDir(aBundle->getTarget(), missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
		find(aBundle->getTarget(), missingFiles, missingSFV, missingNFO, extrasFound, dupesFound, emptyFolders, true);

		reportResults(aBundle->getName(), aBundle->isSet(Bundle::FLAG_SHARING_FAILED) ? TYPE_FAILED_FINISHED : TYPE_FINISHED, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
		return (missingFiles == 0 && extrasFound == 0 && missingNFO == 0 && missingSFV == 0); //allow choosing the level when it shouldn't be added?
	}
	return true;
}

void ShareScannerManager::reportResults(const string& dir, ScanType scanType, int missingFiles, int missingSFV, int missingNFO, int extrasFound, int emptyFolders, int dupesFound) {
	string tmp;
	bool clean = (missingFiles == 0 && extrasFound == 0 && missingNFO == 0 && missingSFV == 0);
	if (scanType == TYPE_FULL) {
		tmp = CSTRING(SCAN_SHARE_FINISHED);
	} else if (scanType == TYPE_PARTIAL) {
		tmp = CSTRING(SCAN_FOLDER_FINISHED);
	} else if (scanType == TYPE_FINISHED) {
		tmp = STRING_F(SCAN_BUNDLE_FINISHED, dir.c_str());
	} else if (scanType == TYPE_FAILED_FINISHED) {
		if (clean)
			return; //no report for clean bundles
		tmp = STRING_F(SCAN_FAILED_BUNDLE_FINISHED, dir.c_str());
	}

	if (clean) {
		tmp += ", ";
		tmp += CSTRING(SCAN_NO_PROBLEMS);
	} else {
		if (scanType != TYPE_FAILED_FINISHED) {
			tmp += " ";
			tmp += CSTRING(SCAN_PROBLEMS_FOUND);
			tmp += ":  ";
		}

		bool first = true;
		if (missingFiles > 0) {
			first = false;
			tmp += STRING_F(X_MISSING_RELEASE_FILES, missingFiles);
		}

		if (missingSFV > 0) {
			if (!first) {
				tmp += ", ";
			}
			first = false;
			tmp += STRING_F(X_MISSING_SFV_FILES, missingSFV);
		}

		if (missingNFO > 0) {
			if (!first) {
				tmp += ", ";
			}
			first = false;
			tmp += STRING_F(X_MISSING_NFO_FILES, missingNFO);
		}

		if (extrasFound > 0) {
			if (!first) {
				tmp += ", ";
			}
			first = false;
			tmp += STRING_F(X_FOLDERS_EXTRAS, extrasFound);
		}

		if (emptyFolders > 0) {
			if (!first) {
				tmp += ", ";
			}
			first = false;
			tmp += STRING_F(X_EMPTY_FOLDERS, emptyFolders);
		}

		if (dupesFound > 0) {
			if (!first) {
				tmp += ", ";
			}
			tmp += STRING_F(X_DUPE_FOLDERS, dupesFound);
		}

		if ((scanType == TYPE_FINISHED || scanType == TYPE_FAILED_FINISHED) && BOOLSETTING(ADD_FINISHED_INSTANTLY)) {
			tmp += str(boost::format(". " + STRING(FORCE_HASH_NOTIFICATION)) % dir);
		}
	}

	LogManager::getInstance()->message(tmp, (!clean && (scanType == TYPE_FAILED_FINISHED || scanType == TYPE_FINISHED)) ? LogManager::LOG_ERROR : LogManager::LOG_INFO);
}

} // namespace dcpp
