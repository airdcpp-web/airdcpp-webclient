/*
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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
#include "DCPlusPlus.h"
#include "HashManager.h"

#include "SFVReader.h"
#include "LogManager.h"
#include "ShareManager.h"
#include "StringTokenizer.h"
#include "FilteredFile.h"

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>

#ifndef _WIN32
#include <dirent.h>
#include <fnmatch.h>
#endif

namespace dcpp {

int SFVReaderManager::scan(StringList paths, bool sfv /*false*/) {
	stop = false;
	//initiate the thread always here for now.
	if(scanning.test_and_set()){
		LogManager::getInstance()->message(STRING(SCAN_RUNNING)); //translate
		return 1;
	}
	isCheckSFV = false;

	if(sfv) {
			isCheckSFV = true;
			Paths = paths;
	} else if(!paths.empty())  {
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
		missingFiles = 0;
		dupesFound = 0;
		extrasFound = 0;
		missingNFO = 0;
		missingSFV = 0;
		LogManager::getInstance()->message(STRING(SCAN_STARTED));
	}
	return 0;
}
void SFVReaderManager::Stop() {
		Paths.clear();
		stop = true;
}
int SFVReaderManager::run() {
	string dir;

	if (isCheckSFV) {
		scanFolderSize = 0;
		for(StringIterC i = Paths.begin(); i != Paths.end();    i++) {
			getScanSize(*i);
		}
	}
	
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

		findMissing(dir);
		find(dir);
		//LogManager::getInstance()->message("Scanned " + dir);
		
		}
	
	} //end for
	
	if(!isCheckSFV){
	string tmp;	 
    tmp.resize(STRING(MISSING_FINISHED).size() + 64);	 
    tmp.resize(snprintf(&tmp[0], tmp.size(), CSTRING(MISSING_FINISHED), missingFiles, dupesFound, missingSFV, missingNFO, extrasFound));	 
    LogManager::getInstance()->message(tmp);
	} else if(stop)
		LogManager::getInstance()->message(STRING(CRC_STOPPED));
	

	scanning.clear();
	dupeDirs.clear();
	Paths.clear();
	return 0;
}

void SFVReaderManager::find(const string& path) {
	if(path.empty())
		return;

	string dir;
	StringList dirs;
	
	for(FileFindIter i(path + "*"); i != FileFindIter(); ++i) {
		try {
			if(i->isDirectory()){
				if (strcmpi(i->getFileName().c_str(), ".") != 0 && strcmpi(i->getFileName().c_str(), "..") != 0) {
					dir = path + i->getFileName() + "\\";
					findMissing(dir);
					if(SETTING(CHECK_DUPES))
						findDupes(dir);
					dirs.push_back(dir);
				}
			}
		} catch(const FileException&) { } 
	 }

		if(!dirs.empty()) {
		for(StringIterC j = dirs.begin(); j != dirs.end(); j++) {
			find(*j);
			}	
		}
	}


void SFVReaderManager::findDupes(const string& path) throw(FileException) {
	if(path.empty())
		return;
	
	tstring dirName = Util::getDir(Text::toT(path));
	string listfolder;

	boost::wregex reg;
	reg.assign(_T("(((?=\\S*[A-Za-z]\\S*)[A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))"));
	if (!regex_match(dirName, reg))
		return;
	

	if (!dupeDirs.empty()) {
		for(StringPairIter i = dupeDirs.begin(); i != dupeDirs.end();    i++) {
			std::string listfolder = i->first;
			if (!stricmp(Text::fromT(dirName), listfolder)) {
				dupesFound++;
				LogManager::getInstance()->message(STRING(DUPE_FOUND) + path + " " + STRING(DUPE_IS_SAME) + " " + (i->second));
			}
		}
	}
	dupeDirs.push_back(make_pair(Text::fromT(dirName), path));
}

StringList SFVReaderManager::findFiles(const string& path, const string& pattern, bool dirs /*false*/) {
	StringList ret;

	WIN32_FIND_DATA data;
	HANDLE hFind;

	hFind = ::FindFirstFile(Text::toT(path + pattern).c_str(), &data);
	if(hFind != INVALID_HANDLE_VALUE) {
		do {
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) {
				if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					if (dirs && Text::fromT(data.cFileName)[0] != '.') {
						ret.push_back(Text::fromT(data.cFileName));
					}
				} else {
					ret.push_back(Text::toLower(Text::fromT(data.cFileName)));
				}
			}
		} while(::FindNextFile(hFind, &data));

		::FindClose(hFind);
	}
	return ret;
}

bool SFVReaderManager::findMissing(const string& path) throw(FileException) {
	if(path.empty())
		return false;

	StringList fileList = findFiles(path, "*");
	StringList folderList;

	if (fileList.empty()) {
		//check if there are folders
		folderList = findFiles(path, "*", true);
		if (folderList.empty()) {
			LogManager::getInstance()->message(STRING(DIR_EMPTY) + " " + path);
			return false;
		}
	}

	bool extrasInFolder = false;
	int pos;
	int pos2;
	boost::wregex reg;
	boost::wregex reg2;
	int nfoFiles=0;
	int sfvFiles=0;
	StringList sfvFileList;
	StringIterC i;
	tstring dirName = Util::getDir(Text::toT(path));

	bool isSample=false;
	bool isRelease=false;
	bool found = false;

	reg.assign(_T("(.+\\.nfo)"));
	reg2.assign(_T("(.+\\.sfv)"));

	// Find NFO and SFV files
	for(i = fileList.begin(); i != fileList.end(); ++i) {
		if (regex_match(Text::toT(*i), reg)) {
			nfoFiles++;
		} else if (regex_match(Text::toT(*i), reg2)) {
			sfvFileList.push_back(path + *i);
			sfvFiles++;
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
		if (!strcmp(Text::fromT(Text::toLower(dirName)).c_str(), "sample")) {
			isSample=true;
		}

		if (nfoFiles == 0 || sfvFiles == 0 || isSample) {

			//Check if it's a release folder
			if (!SETTING(CHECK_MP3_DIR))
				reg.assign(_T("(.+\\.((r\\w{2})|(0\\d{2})))"));
			else
				reg.assign(_T("(.+\\.((r\\w{2})|(0\\d{2})|(mp3)))"));

			for(i = fileList.begin(); i != fileList.end() && !(isRelease); ++i) {
				if (regex_match(Text::toT(*i), reg))
					isRelease=true;
			}

			//Report extra files in sample folder
			if (SETTING(CHECK_EXTRA_FILES) && isSample) {
				found = false;
				if (fileList.size() > 1 && !fileList.empty()) {
					//check that all files have the same extension.. otherwise there are extras
					string extensionFirst = fileList[0], extensionLoop;
					int extPos = extensionFirst.find_last_of(".");
					if (extPos != string::npos) {
						extensionFirst = Text::toLower(extensionFirst.substr(extPos, extensionFirst.length()));
						for(i = fileList.begin(); i != fileList.end(); ++i) {
							extensionLoop = *i;
							int extPos = extensionLoop.find_last_of(".");
							if (extPos != string::npos)
								extensionLoop = Text::toLower(extensionLoop.substr(extPos, extensionLoop.length()));
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
			reg.assign(_T("(([A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))")); //Simple regex for release names
			if (SETTING(CHECK_NFO) && nfoFiles == 0 && regex_match(dirName,reg)) {
				found = false;
				if (fileList.empty()) {
					//check if there are other releases inside
					folderList = findFiles(path, "*", true);
					for(i = folderList.begin(); i != folderList.end(); ++i) {
						if (regex_match(Text::toT(*i),reg))
							found = true;
					}
				}

				if (!found) {
					LogManager::getInstance()->message(STRING(NFO_MISSING) + path);
					missingNFO++;
				}
			}

			//Report missing SFV
			if (sfvFiles == 0 && isRelease) {
				reg.assign(_T("(.{0,5}[Ss]ub(s)?)")); //avoid extra matches
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
		
		//regex to match crc32
		reg.assign(_T("(\\s(\\w{8})$)"));
		int releaseFiles=0;
		int loopMissing=0;

		for(i = sfvFileList.begin(); i != sfvFileList.end(); ++i) {
			sfvFile = *i;
			
			//incase we have some extended characters in the path
			sfv.open(Text::utf8ToAcp(sfvFile));

			if(!sfv.is_open())
				LogManager::getInstance()->message(STRING(CANT_OPEN_SFV) + sfvFile);

			while( getline( sfv, line ) ) {
				//make sure that the line is valid
				pos = line.find(";");
				pos2 = line.find("\\");
				if(regex_search(Text::toT(line), reg) && !(std::string::npos != pos) && !(std::string::npos != pos2)) {
					releaseFiles++;
					//only keep the filename
					pos = line.rfind(" ");
					line = Text::toLower(line.substr(0,pos));

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

		if(SETTING(CHECK_EXTRA_FILES)) {
			//Find allowed extra files from the release folder
			int otherAllowed = 0;
			reg.assign(_T("(.+\\.(jpg|jpeg|m3u|cue|diz))"));
			for(i = fileList.begin(); i != fileList.end(); ++i) {
				if (regex_match(Text::toT(*i), reg))
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

void SFVReaderManager::getScanSize(const string& path) throw(FileException) {
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
			string sfvFile = *i; //in map i->first
			//path = i->second
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

void SFVReaderManager::checkSFV(const string& path) throw(FileException) {
 
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

uint32_t SFVReaderManager::calcCrc32(const string& file) {
	File ff(file, File::READ, File::OPEN);
	CalcInputStream<CRC32Filter, false> f(&ff);

	const size_t BUF_SIZE = 1024*1024;
	boost::scoped_array<uint8_t> b(new uint8_t[BUF_SIZE]);
	size_t n = BUF_SIZE;
	while(f.read(&b[0], n) > 0)
		;		// Keep on looping...
	return f.getFilter().getValue();
}

bool SFVReader::tryFile(const string& sfvFile, const string& fileName) throw(FileException) {

	string sfv = File(sfvFile, File::READ, File::OPEN).read();

	string::size_type i = 0;
	while( (i = Util::findSubString(sfv, fileName, i)) != string::npos) {
		// Either we're at the beginning of the file or the line...otherwise skip...
		if( (i == 0) || (sfv[i-1] == '\n') ) {
			string::size_type j = i + fileName.length() + 1;
			if(j < sfv.length() - 8) {
				sscanf(sfv.c_str() + j, "%x", &crc32);
				crcFound = true;
				return true;
			}
		}
		i += fileName.length();
	}

	return false;
}

void SFVReader::load(const string& fileName) throw() {
	string path = Util::getFilePath(fileName);
	string fname = Util::getFileName(fileName);
	StringList files = File::findFiles(path, "*.sfv");

	for(StringIterC i = files.begin(); i != files.end(); ++i) {
		try {
			if (tryFile(*i, fname)) {
				return;
			}
		} catch(const FileException&) {
			// Ignore...
		}
	}
}



} // namespace dcpp
