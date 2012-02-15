/*
 * Copyright (C) 2011 AirDC++ Project
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

#include "LogManager.h"
#include "ShareManager.h"
#include "StringTokenizer.h"
#include "FilteredFile.h"
#include "File.h"
#include "Wildcards.h"
#include "SFVReader.h"
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
	crcReg.assign("(.{5,200}\\s(\\w{8})$)");
	rarReg.assign("(.+\\.((r\\w{2})|(0\\d{2})))");
	rarMp3Reg.assign("(.+\\.((r\\w{2})|(0\\d{2})|(mp3)|(flac)))");
	audioBookReg.assign(".+(-|\\()AUDIOBOOK(-|\\)).+", boost::regex_constants::icase);
	flacReg.assign(".+(-|\\()(LOSSLESS|FLAC)((-|\\)).+)?", boost::regex_constants::icase);
	zipReg.assign("(.+\\.zip)");
	longReleaseReg.assign(AirUtil::getReleaseRegLong(false));
	mvidReg.assign("(.+\\.(m2v|avi|mkv|mp(e)?g))");
	proofImageReg.assign("(.*(jp(e)?g|png))", boost::regex_constants::icase);
	subDirReg.assign("((((DVD)|(CD)|(DIS(K|C))).?([0-9](0-9)?))|(Sample)|(Cover(s)?)|(.{0,5}Sub(s)?))", boost::regex_constants::icase);
	extraRegs[AUDIOBOOK].assign("(.+\\.(jp(e)?g|png|m3u|cue|zip))");
	extraRegs[FLAC].assign("(.+\\.(jp(e)?g|png|m3u|cue|log))");
	extraRegs[NORMAL].assign("(.+\\.(jp(e)?g|png|m3u|cue|diz))");
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
		LogManager::getInstance()->message(STRING(SCAN_RUNNING));
		return 1;
	}
	isCheckSFV = false;
	isDirScan = false;

	if(sfv) {
		isCheckSFV = true;
		Paths = paths;
	} else if(!paths.empty())  {
		isDirScan = true;
		Paths = paths;
	} else {
		StringPairList dirs = ShareManager::getInstance()->getDirectories(ShareManager::REFRESH_ALL);
		for(StringPairIter i = dirs.begin(); i != dirs.end();   i++) {
			Paths.push_back(i->second);
		}
	}

	start();
	

	if(sfv) {
		LogManager::getInstance()->message(STRING(CRC_STARTED));
		crcOk = 0;
		crcInvalid = 0;
		checkFailed = 0;
	} else {
		LogManager::getInstance()->message(STRING(SCAN_STARTED));
	}
	return 0;
}

void ShareScannerManager::Stop() {
		Paths.clear();
		stop = true;
}

int ShareScannerManager::run() {
	string dir;

	if (isCheckSFV) {
		scanFolderSize = 0;
		for(StringIterC i = Paths.begin(); i != Paths.end();    i++) {
			getScanSize(*i);
		}
	} else {
		QueueManager::getInstance()->getUnfinishedPaths(bundleDirs);
		sort(bundleDirs.begin(), bundleDirs.end());
	}
	
	string dirName;
	int missingFiles = 0;
	int dupesFound = 0;
	int extrasFound = 0;
	int missingNFO = 0;
	int missingSFV = 0;
	int emptyFolders = 0;

	for(;;) { // endless loop
		
		if(Paths.empty() || stop)
			break;

		StringIterC j = Paths.begin();
		dir = *j;
		Paths.erase(Paths.begin());

		if(isCheckSFV) {
			if(dir[dir.size() -1] == '\\') {
				string sfvFile;
				StringList dirFiles = findFiles(dir, "*"); // find all files in dir

				if(dirFiles.size() == 0) {
					LogManager::getInstance()->message(STRING(NO_FILES_IN_FOLDER));
				} else {

					for(;;) { // loop until no files Listed
			
						if(dirFiles.empty() || stop)
							break;

						StringIterC i = dirFiles.begin();
						sfvFile = dir + *i; 
						dirFiles.erase(dirFiles.begin());
						if((sfvFile.find(".nfo") == string::npos) && sfvFile.find(".sfv") == string::npos) // just srip out the nfo and sfv file, others are ok or extra anyways?
							checkSFV(sfvFile);
					}
					dirFiles.clear();
				}
			} else {
				checkSFV(dir); 
			}
		} else {
			if(dir[dir.size() -1] != '\\')
				dir += "\\";

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
			//LogManager::getInstance()->message("Scanned " + dir);
		}
	} //end for
	
	if(!isCheckSFV){
		reportResults(Util::emptyString, isDirScan ? 1 : 0, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders, dupesFound);
	} else if(stop) {
		LogManager::getInstance()->message(STRING(CRC_STOPPED));
	}
	
	bundleDirs.clear();
	scanning.clear();
	dupeDirs.clear();
	Paths.clear();
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
			if(i->isDirectory()){
				if (strcmpi(i->getFileName().c_str(), ".") != 0 && strcmpi(i->getFileName().c_str(), "..") != 0) {
					if (matchSkipList(i->getFileName())) {
						continue;
					}
					dir = path + i->getFileName() + "\\";
					if (!isBundleScan && std::binary_search(bundleDirs.begin(), bundleDirs.end(), dir)) {
						continue;
					}
					DWORD attrib = GetFileAttributes(Text::toT(dir).c_str());
					if(attrib != INVALID_FILE_ATTRIBUTES && attrib != FILE_ATTRIBUTE_HIDDEN && attrib != FILE_ATTRIBUTE_SYSTEM && attrib != FILE_ATTRIBUTE_OFFLINE) {
						scanDir(dir, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
						if(SETTING(CHECK_DUPES) && !isBundleScan)
							findDupes(dir, dupesFound);
						dirs.push_back(dir);
					}
				}
			}
		} catch(const FileException&) { } 
	 }

	if(!dirs.empty()) {
		for(StringIterC j = dirs.begin(); j != dirs.end(); j++) {
			find(*j, missingFiles, missingSFV, missingNFO, extrasFound, dupesFound, emptyFolders, isBundleScan);
		}	
	}
}


void ShareScannerManager::findDupes(const string& path, int& dupesFound) throw(FileException) {
	if(path.empty())
		return;
	
	string dirName = Util::getDir(path, false, true);
	string listfolder;

	if (!regex_match(dirName, releaseReg))
		return;
	
	if (!dupeDirs.empty()) {
		for(auto i = dupeDirs.begin(); i != dupeDirs.end();    i++) {
			if (stricmp(dirName, i->first) == 0) {
				dupesFound++;
				LogManager::getInstance()->message(STRING(DUPE_FOUND) + path + " " + STRING(DUPE_IS_SAME) + " " + (i->second));
			}
		}
	}
	dupeDirs.push_back(make_pair(dirName, path));
}

StringList ShareScannerManager::findFiles(const string& path, const string& pattern, bool dirs /*false*/) {
	StringList ret;

	WIN32_FIND_DATA data;
	HANDLE hFind;

	hFind = ::FindFirstFile(Text::toT(Util::FormatPath(path + pattern)).c_str(), &data);
	if(hFind != INVALID_HANDLE_VALUE) {
		do {
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) && !(data.dwFileAttributes &FILE_ATTRIBUTE_SYSTEM) && !(data.dwFileAttributes &FILE_ATTRIBUTE_SYSTEM)) {
				if (matchSkipList(Text::fromT(data.cFileName))) {
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

	StringList sfvFileList, folderList, fileList = findFiles(path, "*");

	if (fileList.empty()) {
		//check if there are folders
		folderList = findFiles(path, "*", true);
		if (folderList.empty()) {
			if (SETTING(CHECK_EMPTY_DIRS)) {
				LogManager::getInstance()->message(STRING(DIR_EMPTY) + " " + path);
				emptyFolders++;
			}
			return;
		}
	}

	int nfoFiles=0, sfvFiles=0;
	bool isSample=false, isRelease=false, isZipRls=false, found=false, extrasInFolder = false;

	string dirName = Util::getDir(path, false, true);

	// Find NFO and SFV files
	for(auto i = fileList.begin(); i != fileList.end(); ++i) {
		if (Util::getFileExt(*i) == ".nfo") {
			nfoFiles++;
		} else if (Util::getFileExt(*i) == ".sfv") {
			sfvFileList.push_back(path + *i);
			sfvFiles++;
		}
	}

	if (!fileList.empty() && ((nfoFiles + sfvFiles) == (int)fileList.size()) && (SETTING(CHECK_EMPTY_RELEASES))) {
		if (!regex_match(dirName, emptyDirReg)) {
			folderList = findFiles(path, "*", true);
			if (folderList.empty()) {
				LogManager::getInstance()->message(STRING(RELEASE_FILES_MISSING) + " " + path);
				return;
			}
		}
	}

	if(SETTING(CHECK_NFO) || SETTING(CHECK_SFV) || SETTING(CHECK_EXTRA_FILES) || SETTING(CHECK_EXTRA_SFV_NFO)) {
		//Check for multiple NFO or SFV files
		if (SETTING(CHECK_EXTRA_SFV_NFO)) {
			if (nfoFiles > 1) {
				LogManager::getInstance()->message(STRING(MULTIPLE_NFO) + path);
				extrasFound++;
				extrasInFolder = true;
			}
			if (sfvFiles > 1) {
				LogManager::getInstance()->message(STRING(MULTIPLE_SFV) + path);
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
				if (isZipRls && SETTING(CHECK_EXTRA_FILES)) {
					extrasInFolder = !AirUtil::listRegexMatch(fileList, zipFolderReg);
					if (extrasInFolder) {
						LogManager::getInstance()->message(STRING(EXTRA_FILES_RLSDIR) + path);
						extrasFound++;
					}
				}
			}

			//Report extra files in sample folder
			if (SETTING(CHECK_EXTRA_FILES) && isSample) {
				found = false;
				if (fileList.size() > 1) {
					//check that all files have the same extension.. otherwise there are extras
					string extensionFirst, extensionLoop;
					int extPos;
					for(auto i = fileList.begin(); i != fileList.end(); ++i) {
						if (!regex_match(*i, proofImageReg)) {
							extensionFirst = *i;
							extPos = extensionFirst.find_last_of(".");
							if (extPos != string::npos)
								extensionFirst = Text::toLower(extensionFirst.substr(extPos, extensionFirst.length()));
							break;
						}
					}
					if (!extensionFirst.empty()) {
						for(auto i = fileList.begin(); i != fileList.end(); ++i) {
							extensionLoop = *i;
							extPos = extensionLoop.find_last_of(".");
							if (extPos != string::npos) {
								extensionLoop = Text::toLower(extensionLoop.substr(extPos, extensionLoop.length()));
								if (regex_match(extensionLoop, proofImageReg))
									continue;
							}
							if (strcmp(extensionLoop.c_str(), extensionFirst.c_str())) {
								found = true;
								break;
							}
						}
					}
				}
				if (nfoFiles > 0 || sfvFiles > 0 || isRelease || found) {
					LogManager::getInstance()->message(STRING(EXTRA_FILES_SAMPLEDIR) + path);
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
					folderList = findFiles(path, "*", true);
					//check if there are multiple disks and nfo inside them
					for(auto i = folderList.begin(); i != folderList.end(); ++i) {
						if (regex_match(*i, subDirReg)) {
							found = false;
							StringList filesListSub = findFiles(path + *i + "\\", "*.nfo");
							if (!filesListSub.empty()) {
								found = true;
								break;
							}
						}
					}
				}

				if (!found) {
					LogManager::getInstance()->message(STRING(NFO_MISSING) + path);
					missingNFO++;
				}
			}

			//Report missing SFV
			if (sfvFiles == 0 && isRelease) {
				//avoid extra matches
				if (!regex_match(dirName,subReg) && SETTING(CHECK_SFV)) {
					LogManager::getInstance()->message(STRING(SFV_MISSING) + path);
					missingSFV++;
				}
				return;
			}
		}
	}

	if (sfvFiles == 0)
		return;

	ifstream sfv;
	string line;
	string sfvFile;
	bool hasValidSFV = false;

	int releaseFiles=0;
	int loopMissing=0;

	for(auto i = sfvFileList.begin(); i != sfvFileList.end(); ++i) {
		sfvFile = *i;
		try {
			openSFV(sfvFile, sfv);
		} catch(const FileException&) {
			LogManager::getInstance()->message(STRING(CANT_OPEN_SFV) + sfvFile);
			continue;
		}

		while( getline( sfv, line ) ) {
			//make sure that the line is valid
			bool invalidFile=false;
			if (!getFileSFV(line, invalidFile)) {
				if (invalidFile) {
					LogManager::getInstance()->message(STRING(CANT_OPEN_SFV) + sfvFile);
					break;
				}
				continue;
			}
			hasValidSFV = true;
			releaseFiles++;

			auto k = std::find(fileList.begin(), fileList.end(), line);
			if(k == fileList.end()) { 
				loopMissing++;
				if (SETTING(CHECK_MISSING))
					LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + path + line);
			}
		}
		sfv.close();
	}

	missingFiles += loopMissing;
	releaseFiles = releaseFiles - loopMissing;

	if(SETTING(CHECK_EXTRA_FILES) && ((int)fileList.size() != releaseFiles + nfoFiles + sfvFiles) && hasValidSFV) {
		//Find allowed extra files from the release folder
		int8_t extrasType = NORMAL;
		if (regex_match(dirName, audioBookReg)) {
			extrasType = AUDIOBOOK;
		} else if (regex_match(dirName, flacReg)) {
			extrasType = FLAC;
		}

		if ((int)fileList.size() > (releaseFiles + nfoFiles + sfvFiles + AirUtil::listRegexCount(fileList, extraRegs[extrasType]))) {
			LogManager::getInstance()->message(STRING(EXTRA_FILES_RLSDIR) + path);
			if (!extrasInFolder)
				extrasFound++;
		}
	}
}

void ShareScannerManager::openSFV(const string& path, ifstream& stream) {
	if (File::getSize(Text::utf8ToAcp(path)) > 1000000) {
		throw FileException();
	}

	//incase we have some extended characters in the path
	stream.open(Text::utf8ToAcp(Util::FormatPath(path)));

	if(!stream.is_open()) {
		throw FileException();
	}
}

bool ShareScannerManager::getFileSFV(string& line, bool& invalidFile) {
	if(regex_search(line, crcReg) && (line.find("\\") == string::npos) && (line.find(";") == string::npos)) {
		//only keep the filename
		size_t pos = line.rfind(" ");
		if (pos == string::npos) {
			return false;
		}
		line = Text::toLower(line.substr(0,pos));

		//extra checks to ignore invalid lines
		if (line.length() < 5)
			return false;
		if (line.length() > 150) {
			//can't most likely to detect the line breaks
			invalidFile = true;
			return false;
		}

		//quoted line?
		if (line[0] == '\"' && line[line.length()-1] == '\"') {
			line = line.substr(1,line.length()-2);
		}
		return true;
	}
	return false;
}

void ShareScannerManager::getScanSize(const string& path) throw(FileException) {
	if(path[path.size() -1] == '\\') {
		StringList sfvFileList = findFiles(path, "*.sfv");
		string line;
		ifstream sfv;
		for(;;) {
			if(sfvFileList.empty())
				break;

			StringIterC i = sfvFileList.begin();
			string sfvFile = path + *i;
			sfvFileList.erase(sfvFileList.begin());
		
			try {
				openSFV(sfvFile, sfv);
			} catch(const FileException&) {
				LogManager::getInstance()->message(STRING(CANT_OPEN_SFV) + sfvFile);
				continue;
			}

			while( getline( sfv, line ) ) {
				bool invalidFile = false;
				if (!getFileSFV(line, invalidFile)) {
					if (invalidFile) {
						LogManager::getInstance()->message(STRING(CANT_OPEN_SFV) + path);
						break;
					}
					continue;
				}

				ifstream ifile(path + line);
				if (ifile) {
					scanFolderSize = scanFolderSize + File::getSize(path + line);
				} else {
					LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + path + line);
					checkFailed++;
				}
			}
		}
	} else {
		ifstream ifile(path);
		if (ifile) {
			scanFolderSize = scanFolderSize + File::getSize(path);
		} else {
			LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + path);
			checkFailed++;
		}
	}
}

void ShareScannerManager::checkSFV(const string& path) throw(FileException) {
 
	SFVReader sfv(path);
	uint64_t checkStart = 0;
	uint64_t checkEnd = 0;

	if(sfv.hasCRC()) {
		bool crcMatch = false;
		try {
			checkStart = GET_TICK();
			crcMatch = (calcCrc32(path) == sfv.getCRC());
			checkEnd = GET_TICK();
		} catch(const FileException& ) {
			// Couldn't read the file to get the CRC(!!!)
			LogManager::getInstance()->message(STRING(CRC_FILE_ERROR) + path);
		}

		int64_t size = File::getSize(path);
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

		message += path + " (" + Util::formatBytes(speed) + "/s)";

		scanFolderSize = scanFolderSize - size;
		message += ", " + STRING(CRC_REMAINING) + Util::formatBytes(scanFolderSize);
		LogManager::getInstance()->message(message);


	} else {
		LogManager::getInstance()->message(STRING(NO_CRC32) + " " + path);
		checkFailed++;
	}

	if (scanFolderSize <= 0) {
		LogManager::getInstance()->message(str(boost::format(STRING(CRC_FINISHED)) % crcOk % crcInvalid % checkFailed));
	}

}

uint32_t ShareScannerManager::calcCrc32(const string& file) {
	File ff(file, File::READ, File::OPEN);
	CalcInputStream<CRC32Filter, false> f(&ff);

	const size_t BUF_SIZE = 1024*1024;
	boost::scoped_array<uint8_t> b(new uint8_t[BUF_SIZE]);
	size_t n = BUF_SIZE;
	while(f.read(&b[0], n) > 0)
		;		// Keep on looping...

	return f.getFilter().getValue();
}

bool ShareScannerManager::scanBundle(BundlePtr aBundle) noexcept {
	if (SETTING(SCAN_DL_BUNDLES) && !aBundle->isFileBundle()) {
		string dir = aBundle->getTarget();
		int missingFiles = 0;
		int dupesFound = 0;
		int extrasFound = 0;
		int missingNFO = 0;
		int missingSFV = 0;
		int emptyFolders = 0;

		scanDir(dir, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
		find(dir, missingFiles, missingSFV, missingNFO, extrasFound, dupesFound, emptyFolders, true);

		reportResults(aBundle->getName(), !aBundle->isSet(Bundle::FLAG_SCAN_FAILED) ? 2 : 3, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
		return (missingFiles == 0 && extrasFound == 0 && missingNFO == 0 && missingSFV == 0); //allow choosing the level when it shouldn't be added?
	}
	return true;
}

void ShareScannerManager::reportResults(const string& dir, int scanType, int missingFiles, int missingSFV, int missingNFO, int extrasFound, int emptyFolders, int dupesFound) {
	string tmp, tmp2;
	if (scanType == 0) {
		tmp = CSTRING(SCAN_SHARE_FINISHED);
	} else if (scanType == 1) {
		tmp = CSTRING(SCAN_FOLDER_FINISHED);
	} else if (scanType == 2) {
		tmp = str(boost::format(STRING(SCAN_BUNDLE_FINISHED)) % dir.c_str());
	} else {
		tmp = str(boost::format(STRING(SCAN_FAILED_BUNDLE_FINISHED)) % dir.c_str());
	}

	if (missingFiles == 0 && extrasFound == 0 && missingNFO == 0 && missingSFV == 0) {
		if (scanType == 3) {
			//no report for clean bundles
			return;
		}
		tmp += ", ";
		tmp += CSTRING(SCAN_NO_PROBLEMS);
	} else {
		if (scanType != 3) {
			tmp += " ";
			tmp += CSTRING(SCAN_PROBLEMS_FOUND);
			tmp += ":  ";
		}

		bool first = true;
		if (SETTING(CHECK_MISSING) && missingFiles > 0) {
			first = false;
			tmp += str(boost::format(STRING(X_MISSING_RELEASE_FILES)) % missingFiles);
		}

		if (SETTING(CHECK_SFV) && missingSFV > 0) {
			if (!first) {
				tmp += ", ";
			}
			first = false;
			tmp += str(boost::format(STRING(X_MISSING_SFV_FILES)) % missingSFV);
		}

		if (SETTING(CHECK_NFO) && missingNFO > 0) {
			if (!first) {
				tmp += ", ";
			}
			first = false;
			tmp += str(boost::format(STRING(X_MISSING_NFO_FILES)) % missingNFO);
		}

		if (SETTING(CHECK_EXTRA_FILES) && extrasFound > 0) {
			if (!first) {
				tmp += ", ";
			}
			first = false;
			tmp += str(boost::format(STRING(X_FOLDERS_EXTRAS)) % extrasFound);
		}

		if (SETTING(CHECK_EMPTY_DIRS) && emptyFolders > 0) {
			if (!first) {
				tmp += ", ";
			}
			first = false;
			tmp += str(boost::format(STRING(X_EMPTY_FOLDERS)) % emptyFolders);
		}

		if (SETTING(CHECK_DUPES) && dupesFound > 0) {
			if (!first) {
				tmp += ", ";
			}
			tmp += str(boost::format(STRING(X_DUPE_FOLDERS)) % dupesFound);
		}
	}
	LogManager::getInstance()->message(tmp);
}

} // namespace dcpp
