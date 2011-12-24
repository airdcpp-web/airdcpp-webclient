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

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>

#ifndef _WIN32
#include <dirent.h>
#include <fnmatch.h>
#endif

namespace dcpp {

int ShareScannerManager::scan(StringList paths, bool sfv /*false*/) {
	stop = false;
	//initiate the thread always here for now.
	if(scanning.test_and_set()){
		LogManager::getInstance()->message(STRING(SCAN_RUNNING));
		return 1;
	}
	isCheckSFV = false;
	isDirScan = false;
	//skipListReg.Init(SETTING(SKIPLIST_SHARE));
	//skipListReg.study();

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
				StringList FileList = findFiles(dir, "*"); // find all files in dir

				if(FileList.size() == 0) {
					LogManager::getInstance()->message(STRING(NO_FILES_IN_FOLDER));
				} else {

					for(;;) { // loop until no files Listed
			
						if(FileList.empty() || stop)
							break;

						StringIterC i = FileList.begin();
						sfvFile = dir + *i; 
						FileList.erase(FileList.begin());
						if((sfvFile.find(".nfo") == string::npos) && sfvFile.find(".sfv") == string::npos) // just srip out the nfo and sfv file, others are ok or extra anyways?
							checkSFV(sfvFile);
					}
					FileList.clear();
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
				find(dir, missingFiles, missingSFV, missingNFO, extrasFound, dupesFound, emptyFolders, true);
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
		if (BOOLSETTING(SHARE_SKIPLIST_USE_REGEXP)) {
			return AirUtil::matchSkiplist(dir);
		} else {
			try {
				if (Wildcard::patternMatch(Text::utf8ToAcp(dir), Text::utf8ToAcp(SETTING(SKIPLIST_SHARE)), '|')) {
					return true;
				} else {
					return false;
				}
			} catch(...) { }
		}
	}
	return false;
}

void ShareScannerManager::find(const string& path, int& missingFiles, int& missingSFV, int& missingNFO, int& extrasFound, int& dupesFound, int& emptyFolders, bool checkBundles) {
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
					if (checkBundles) {
						if (std::binary_search(bundleDirs.begin(), bundleDirs.end(), dir)) {
							continue;
						}
					}
					DWORD attrib = GetFileAttributes(Text::toT(dir).c_str());
					if(attrib != INVALID_FILE_ATTRIBUTES && attrib != FILE_ATTRIBUTE_HIDDEN && attrib != FILE_ATTRIBUTE_SYSTEM && attrib != FILE_ATTRIBUTE_OFFLINE) {
						scanDir(dir, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
						if(SETTING(CHECK_DUPES))
							findDupes(dir, dupesFound);
						dirs.push_back(dir);
					}
				}
			}
		} catch(const FileException&) { } 
	 }

	if(!dirs.empty()) {
		for(StringIterC j = dirs.begin(); j != dirs.end(); j++) {
			find(*j, missingFiles, missingSFV, missingNFO, extrasFound, dupesFound, emptyFolders, checkBundles);
		}	
	}
}


void ShareScannerManager::findDupes(const string& path, int& dupesFound) throw(FileException) {
	if(path.empty())
		return;
	
	string dirName = Util::getDir(path, false, true);
	string listfolder;

	boost::regex reg;
	reg.assign("(((?=\\S*[A-Za-z]\\S*)[A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))");
	if (!regex_match(dirName, reg))
		return;
	

	if (!dupeDirs.empty()) {
		for(StringPairIter i = dupeDirs.begin(); i != dupeDirs.end();    i++) {
			std::string listfolder = i->first;
			if (!stricmp(dirName, listfolder)) {
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

	hFind = ::FindFirstFile(Text::toT(path + pattern).c_str(), &data);
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

bool ShareScannerManager::scanDir(const string& path, int& missingFiles, int& missingSFV, int& missingNFO, int& extrasFound, int& emptyFolders) throw(FileException) {
	if(path.empty())
		return false;

	StringList sfvFileList, folderList, fileList = findFiles(path, "*");

	if (fileList.empty()) {
		//check if there are folders
		folderList = findFiles(path, "*", true);
		if (folderList.empty()) {
			if (SETTING(CHECK_EMPTY_DIRS))
				LogManager::getInstance()->message(STRING(DIR_EMPTY) + " " + path);
				emptyFolders++;
			return false;
		}
	}

	boost::regex reg, reg2;
	int nfoFiles=0, sfvFiles=0, pos, pos2;
	bool isSample=false, isRelease=false, isZipRls=false, found=false, extrasInFolder = false;

	StringIterC i;
	string dirName = Util::getDir(path, false, true);

	reg.assign("(.+\\.nfo)");
	reg2.assign("(.+\\.sfv)");

	// Find NFO and SFV files
	for(i = fileList.begin(); i != fileList.end(); ++i) {
		if (regex_match(*i, reg)) {
			nfoFiles++;
		} else if (regex_match(*i, reg2)) {
			sfvFileList.push_back(path + *i);
			sfvFiles++;
		}
	}

	if (!fileList.empty() && ((nfoFiles + sfvFiles) == fileList.size()) && (SETTING(CHECK_EMPTY_RELEASES))) {
		reg.assign("(\\S*(((nfo|dir).?fix)|nfo.only)\\S*)", boost::regex_constants::icase);
		if (!regex_match(dirName,reg)) {
			folderList = findFiles(path, "*", true);
			if (folderList.empty()) {
				LogManager::getInstance()->message(STRING(RELEASE_FILES_MISSING) + " " + path);
				return false;
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
		if (!strcmp(Text::toLower(dirName).c_str(), "sample")) {
			isSample=true;
		}

		if (nfoFiles == 0 || sfvFiles == 0 || isSample || SETTING(CHECK_EXTRA_FILES)) {

			//Check if it's a RAR/Music release folder
			if (!SETTING(CHECK_MP3_DIR))
				reg.assign("(.+\\.((r\\w{2})|(0\\d{2})))");
			else
				reg.assign("(.+\\.((r\\w{2})|(0\\d{2})|(mp3)|(flac)))");

			for(i = fileList.begin(); i != fileList.end(); ++i) {
				if (regex_match(*i, reg)) {
					isRelease=true;
					break;
				}
			}

			if (!isRelease) {

				//Check if it's a zip release folder
				reg.assign("(([A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))");
				if (regex_match(dirName, reg)) {
					reg.assign("(.+\\.zip)");
					for(i = fileList.begin(); i != fileList.end(); ++i) {
						if (regex_match(*i, reg)) {
							isZipRls=true;
							break;
						}
					}
				}

				//Check if it's a Mvid release folder
				reg.assign("(?=\\S*[A-Z]\\S*)(([A-Z0-9]|\\w[A-Z0-9])[A-Za-z0-9-]*)(\\.|_|(-(?=\\S*\\d{4}\\S+)))(\\S+)-(\\w{2,})");
				if (!isZipRls && regex_match(dirName, reg)) {
					reg.assign("(.+\\.(m2v|avi|mkv|mp(e)?g))");
					for(i = fileList.begin(); i != fileList.end(); ++i) {
						if (regex_match(*i, reg)) {
							isRelease=true;
							break;
						}
					}
				}

				//Report extra files in a zip folder
				if (isZipRls && SETTING(CHECK_EXTRA_FILES)) {
					reg.assign("(.+\\.(jp(e)?g|png|diz|zip|nfo|sfv))");
					for(i = fileList.begin(); i != fileList.end(); ++i) {
						if (!regex_match(*i, reg)) {
							LogManager::getInstance()->message(STRING(EXTRA_FILES_RLSDIR) + path + *i);
							extrasInFolder = true;
						}
					}
					if (extrasInFolder)
						extrasFound++;
				}
			}

			//Report extra files in sample folder
			if (SETTING(CHECK_EXTRA_FILES) && isSample) {
				found = false;
				if (fileList.size() > 1) {
					//check that all files have the same extension.. otherwise there are extras
					reg.assign("(.*(jp(e)?g|png))", boost::regex_constants::icase);
					string extensionFirst, extensionLoop;
					int extPos;
					for(i = fileList.begin(); i != fileList.end(); ++i) {
						if (!regex_match(*i, reg)) {
							extensionFirst = *i;
							extPos = extensionFirst.find_last_of(".");
							if (extPos != string::npos)
								extensionFirst = Text::toLower(extensionFirst.substr(extPos, extensionFirst.length()));
							break;
						}
					}
					if (!extensionFirst.empty()) {
						for(i = fileList.begin(); i != fileList.end(); ++i) {
							extensionLoop = *i;
							extPos = extensionLoop.find_last_of(".");
							if (extPos != string::npos) {
								extensionLoop = Text::toLower(extensionLoop.substr(extPos, extensionLoop.length()));
								if (regex_match(extensionLoop, reg))
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
				return false;

			//Report missing NFO
			reg.assign("(([A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))"); //Simple regex for release names
			if (SETTING(CHECK_NFO) && nfoFiles == 0 && regex_match(dirName,reg)) {
				StringList filesListSub;
				found = false;
				if (fileList.empty()) {
					found = true;
					reg.assign("((((DVD)|(CD)|(DIS(K|C))).?([0-9](0-9)?))|(Sample)|(Cover(s)?)|(.{0,5}Sub(s)?))", boost::regex_constants::icase);
					folderList = findFiles(path, "*", true);
					//check if there are multiple disks and nfo inside them
					for(i = folderList.begin(); i != folderList.end(); ++i) {
						if (regex_match(*i,reg)) {
							found = false;
							filesListSub = findFiles(path + *i + "\\", "*.nfo");
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
				reg.assign("(.{0,8}[Ss]ub(s|pack)?)"); //avoid extra matches
				if (!regex_match(dirName,reg) && SETTING(CHECK_SFV)) {
					LogManager::getInstance()->message(STRING(SFV_MISSING) + path);
					missingSFV++;
				}
				return false;
			}
		}
	}

	if (sfvFiles == 0)
		return false;

	ifstream sfv;
	string line;
	string sfvFile;

	reg.assign("(.{5,200}\\s(\\w{8})$)");
	reg2.assign("(\".+\")");
	int releaseFiles=0;
	int loopMissing=0;
	bool invalidFile=false;

	for(i = sfvFileList.begin(); i != sfvFileList.end(); ++i) {
		sfvFile = *i;

		if (File::getSize(Text::utf8ToAcp(sfvFile)) > 1000000) {
			LogManager::getInstance()->message(STRING(CANT_OPEN_SFV) + sfvFile);
			invalidFile = true;
			continue;
		}

		//incase we have some extended characters in the path
		sfv.open(Text::utf8ToAcp(Util::FormatPath(sfvFile)));

		if(!sfv.is_open()) {
			LogManager::getInstance()->message(STRING(CANT_OPEN_SFV) + sfvFile);
			invalidFile = true;
			continue;
		}

		while( getline( sfv, line ) ) {
			//make sure that the line is valid
			pos = line.find(";");
			pos2 = line.find("\\");
			if(regex_search(line, reg) && !(std::string::npos != pos) && !(std::string::npos != pos2)) {
				releaseFiles++;
				//only keep the filename
				pos = line.rfind(" ");
				line = Text::toLower(line.substr(0,pos));
				if (line.length() < 5)
					continue;
				if (line.length() > 150) {
					LogManager::getInstance()->message(STRING(CANT_OPEN_SFV) + sfvFile);
					invalidFile = true;
					break;
				}

				if (regex_match(line, reg2)) {
					line = line.substr(1,line.length()-2);
				}

				StringIterC k = std::find(fileList.begin(), fileList.end(), line);
					if(k == fileList.end()) { 
						loopMissing++;
						if (SETTING(CHECK_MISSING))
							LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + path + line);
				}
			}
		}
		sfv.close();
	}

	missingFiles += loopMissing;
	releaseFiles = releaseFiles - loopMissing;

	if(SETTING(CHECK_EXTRA_FILES) && ((int)fileList.size() != releaseFiles + nfoFiles + sfvFiles) && !invalidFile) {
		//Find allowed extra files from the release folder
		int otherAllowed = 0;
		reg.assign(".+(-|\\()AUDIOBOOK(-|\\)).+", boost::regex_constants::icase);
		reg2.assign(".+(-|\\()(LOSSLESS|FLAC)((-|\\)).+)?", boost::regex_constants::icase);
		if (regex_match(dirName, reg)) {
			reg.assign("(.+\\.(jp(e)?g|png|m3u|cue|zip))");
		}
		else if (regex_match(dirName, reg2))
			reg.assign("(.+\\.(jp(e)?g|png|m3u|cue|log))");
		else
			reg.assign("(.+\\.(jp(e)?g|png|m3u|cue|diz))");

		for(i = fileList.begin(); i != fileList.end(); ++i) {
			if (regex_match(*i, reg))
				otherAllowed++;
		}

		if ((int)fileList.size() > (releaseFiles + nfoFiles + sfvFiles + otherAllowed)) {
			LogManager::getInstance()->message(STRING(EXTRA_FILES_RLSDIR) + path);
			if (!extrasInFolder)
				extrasFound++;
		}
	}
	if (loopMissing > 0)
		return true;
	else
		return false;
}

void ShareScannerManager::getScanSize(const string& path) throw(FileException) {
	if(path[path.size() -1] == '\\') {
		StringList sfvFileList = findFiles(path, "*.sfv");

		int pos;
		int pos2;
		string line;
		boost::wregex reg;
		ifstream sfv;
		reg.assign(_T("(\\s(\\w{8})$)"));
		for(;;) {
			if(sfvFileList.empty())
				break;

			StringIterC i = sfvFileList.begin();
			string sfvFile = *i;
			sfvFileList.erase(sfvFileList.begin());
		
			//incase we have some extended characters in the path
			sfv.open(Text::utf8ToAcp(path + sfvFile));

			if(!sfv.is_open())
				LogManager::getInstance()->message(STRING(CANT_OPEN_SFV) + sfvFile);

			while( getline( sfv, line ) ) {
				//make sure that the line is valid
				pos = line.find(";");
				pos2 = line.find("\\");
				if(regex_search(Text::toT(line), reg) && !(std::string::npos != pos) && !(std::string::npos != pos2)) {
					pos = line.rfind(" ");
					line = line.substr(0,pos);
					ifstream ifile(path + line);
					if (ifile) {
						scanFolderSize = scanFolderSize + File::getSize(path + line);
					} else {
						LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + path + line);
						checkFailed++;
					}
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
		string tmp;	 
		tmp.resize(STRING(CRC_FINISHED).size() + 64);	 
		tmp.resize(snprintf(&tmp[0], tmp.size(), CSTRING(CRC_FINISHED), crcOk, crcInvalid, checkFailed));	 
		LogManager::getInstance()->message(tmp);
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
	if (SETTING(SCAN_DL_BUNDLES) && !aBundle->getFileBundle()) {
		string dir = aBundle->getTarget();
		int missingFiles = 0;
		int dupesFound = 0;
		int extrasFound = 0;
		int missingNFO = 0;
		int missingSFV = 0;
		int emptyFolders = 0;

		scanDir(dir, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
		find(dir, missingFiles, missingSFV, missingNFO, extrasFound, dupesFound, emptyFolders, false);

		reportResults(aBundle->getName(), !aBundle->isSet(Bundle::FLAG_SCAN_FAILED) ? 2 : 3, missingFiles, missingSFV, missingNFO, extrasFound, emptyFolders);
		return (missingFiles == 0 && extrasFound == 0 && missingNFO == 0 && missingSFV == 0); //allow choosing the level when it shouldn't be added?
	}
	return true;
}

void ShareScannerManager::reportResults(const string& dir, int scanType, int missingFiles, int missingSFV, int missingNFO, int extrasFound, int emptyFolders, int dupesFound) {
	//LogManager::getInstance()->message("missing: " + Util::toString(missingFiles) + ", missingSFV: " + Util::toString(missingSFV) + ", missingNFO: " + Util::toString(missingNFO) + ", extrasFound" 
	//	+ Util::toString(extrasFound) + ", emptyFolders: " + Util::toString(emptyFolders) + ", dupesFound: " + Util::toString(dupesFound));
		string tmp, tmp2;
		if (scanType == 0) {
			tmp = CSTRING(SCAN_SHARE_FINISHED);
		} else if (scanType == 1) {
			tmp = CSTRING(SCAN_FOLDER_FINISHED);
		} else if (scanType == 2) {
			tmp2.resize(STRING(SCAN_BUNDLE_FINISHED).size() + dir.size());
			tmp2.resize(snprintf(&tmp2[0], tmp2.size(), CSTRING(SCAN_BUNDLE_FINISHED), dir.c_str()));
			tmp += tmp2;
			tmp2.clear();
		} else {
			tmp2.resize(STRING(SCAN_FAILED_BUNDLE_FINISHED).size() + dir.size());
			tmp2.resize(snprintf(&tmp2[0], tmp2.size(), CSTRING(SCAN_FAILED_BUNDLE_FINISHED), dir.c_str()));
			tmp += tmp2;
			tmp2.clear();
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
				tmp2.resize(tmp.size() + STRING(X_MISSING_RELEASE_FILES).size() + 256);
				tmp2.resize(snprintf(&tmp2[0], tmp2.size(), CSTRING(X_MISSING_RELEASE_FILES), missingFiles));
				tmp += tmp2;
				tmp2.clear();
			}

			if (SETTING(CHECK_SFV) && missingSFV > 0) {
				if (!first) {
					tmp += ", ";
				}
				first = false;
				tmp2.resize(tmp.size() + STRING(X_MISSING_RELEASE_FILES).size() + 256);
				tmp2.resize(snprintf(&tmp2[0], tmp2.size(), CSTRING(X_MISSING_SFV_FILES), missingSFV));
				tmp += tmp2;
				tmp2.clear();
			}

			if (SETTING(CHECK_NFO) && missingNFO > 0) {
				if (!first) {
					tmp += ", ";
				}
				first = false;
				tmp2.resize(tmp.size() + STRING(X_MISSING_RELEASE_FILES).size() + 256);
				tmp2.resize(snprintf(&tmp2[0], tmp2.size(), CSTRING(X_MISSING_NFO_FILES), missingNFO));
				tmp += tmp2;
				tmp2.clear();
			}

			if (SETTING(CHECK_EXTRA_FILES) && extrasFound > 0) {
				if (!first) {
					tmp += ", ";
				}
				first = false;
				tmp2.resize(tmp.size() + STRING(X_MISSING_RELEASE_FILES).size() + 256);
				tmp2.resize(snprintf(&tmp2[0], tmp2.size(), CSTRING(X_FOLDERS_EXTRAS), extrasFound));
				tmp += tmp2;
				tmp2.clear();
			}

			if (SETTING(CHECK_EMPTY_DIRS) && emptyFolders > 0) {
				if (!first) {
					tmp += ", ";
				}
				first = false;
				tmp2.resize(tmp.size() + STRING(X_MISSING_RELEASE_FILES).size() + 256);
				tmp2.resize(snprintf(&tmp2[0], tmp2.size(), CSTRING(X_EMPTY_FOLDERS), emptyFolders));
				tmp += tmp2;
				tmp2.clear();
			}

			if (SETTING(CHECK_DUPES) && dupesFound > 0) {
				if (!first) {
					tmp += ", ";
				}
				tmp2.resize(tmp.size() + STRING(X_MISSING_RELEASE_FILES).size() + 256);
				tmp2.resize(snprintf(&tmp2[0], tmp2.size(), CSTRING(X_DUPE_FOLDERS), dupesFound));
				tmp += tmp2;
				tmp2.clear();
			}
		}
		LogManager::getInstance()->message(tmp);
}

} // namespace dcpp
