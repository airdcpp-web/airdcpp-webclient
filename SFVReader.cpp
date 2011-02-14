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

#include "SFVReader.h"
#include "LogManager.h"
#include "ShareManager.h"
#include "StringTokenizer.h"

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cstring>

#ifndef _WIN32
#include <dirent.h>
#include <fnmatch.h>
#endif

namespace dcpp {

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


int SFVReaderManager::scan(StringList paths) {
	
	if(scanning.test_and_set()){
		LogManager::getInstance()->message("Scan in Progress"); //translate
		return 1;
	}
	partialScan = false;

	if(!paths.empty()) {
		partialScan = true;
		Paths = paths;
	}

	start();
	missingFiles = 0;
	dupesFound = 0;
	extrasFound = 0;
	missingNFO = 0;
	missingSFV = 0;
	LogManager::getInstance()->message(STRING(SCAN_STARTED)); 
	return 0;
}
int SFVReaderManager::run() {
	
	if(partialScan) {
		for(StringIterC j = Paths.begin(); j != Paths.end(); j++) {
		findMissing(*j);
		find(*j);
		}
	} else {

	StringPairList dirs = ShareManager::getInstance()->getDirectories(ShareManager::REFRESH_ALL);

	for(StringPairIter i = dirs.begin(); i != dirs.end();    i++) {
		findMissing(i->second);
		find(i->second);
		//LogManager::getInstance()->message("Scanned " + i->second);
		}
	}

	string tmp;	 
    tmp.resize(STRING(MISSING_FINISHED).size() + 64);	 
    tmp.resize(snprintf(&tmp[0], tmp.size(), CSTRING(MISSING_FINISHED), missingFiles, dupesFound, missingSFV, missingNFO, extrasFound));	 
    LogManager::getInstance()->message(tmp);
	
	scanning.clear();
	dupeDirs.clear();
	Paths.clear();
	partialScan = false;
	return 0;
}

//Test if this works, have another way of doing it but it would need some work
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
	
	tstring dirName = getDir(Text::toT(path));
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
				LogManager::getInstance()->message(STRING(DUPE_FOUND) + path + " " + STRING(DUPE_IS_SAME) + " " + (i->second) + ")");
			}
		}
	}
	dupeDirs.push_back(make_pair(Text::fromT(dirName), path));
}

StringList SFVReaderManager::findFiles(const string& path, const string& pattern) {
	StringList ret;

	WIN32_FIND_DATA data;
	HANDLE hFind;

	hFind = ::FindFirstFile(Text::toT(path + pattern).c_str(), &data);
	if(hFind != INVALID_HANDLE_VALUE) {
		do {
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
				ret.push_back(Text::toLower(Text::fromT(data.cFileName)));
		} while(::FindNextFile(hFind, &data));

		::FindClose(hFind);
	}
	return ret;
}

void SFVReaderManager::findMissing(const string& path) throw(FileException) {
	if(path.empty())
		return;

	StringList fileList = findFiles(path, "*");

	int pos;
	boost::wregex reg;
	int nfoFiles=0;
	int sfvFiles=0;
	StringList sfvFileList;
	StringIterC i;
	tstring dirName = getDir(Text::toT(path));

	bool isSample=false;
	bool isRelease=false;

	//NfoFileList
	for(i = fileList.begin(); i != fileList.end(); ++i) {
		reg.assign(_T("(.+\\.nfo)"));
		if (regex_match(Text::toT(*i), reg))
			nfoFiles++;
	}
	//SFVFileList
	for(i = fileList.begin(); i != fileList.end(); ++i) {
		reg.assign(_T("(.+\\.sfv)"));
		if (regex_match(Text::toT(*i), reg)) {
			sfvFileList.push_back(path + *i);
			sfvFiles++;
		}
	}



	if(SETTING(CHECK_NFO) || SETTING(CHECK_SFV) || SETTING(CHECK_EXTRA_FILES) || SETTING(CHECK_EXTRA_SFV_NFO)) {

		//Check if it's a release folder
		if (!SETTING(CHECK_MP3_DIR))
			reg.assign(_T("(.+\\.((r\\w{2})|(0\\d{2})))"));
		else
			reg.assign(_T("(.+\\.((r\\w{2})|(0\\d{2})|(mp3)))"));
		for(i = fileList.begin(); i != fileList.end() && !(isRelease); ++i) {
			if (regex_match(Text::toT(*i), reg))
				isRelease=true;
		}

		//Check for multiple NFO or SFV files
		if (SETTING(CHECK_EXTRA_SFV_NFO)) {
			if (nfoFiles > 1) {
				LogManager::getInstance()->message(STRING(MULTIPLE_NFO) + path);
				extrasFound++;
			}
			if (sfvFiles > 1) {
				LogManager::getInstance()->message(STRING(MULTIPLE_SFV) + path);
				extrasFound++;
			}
		}

		//Check if it's sample folder
		if (!strcmp(Text::fromT(Text::toLower(dirName)).c_str(), "sample") && SETTING(CHECK_EXTRA_FILES)) {
			isSample=true;
		}

		if (nfoFiles == 0 || sfvFiles == 0 || isSample) {

			//Check if it's a release folder
			if (!SETTING(CHECK_MP3_DIR))
				reg.assign(_T("(.+\\.((r\\w{2})|(0\\d{2})))"));
			else
				reg.assign(_T("(.+\\.((r\\w{2})|(0\\d{2})|(mp3)))"));
			for(StringIter i = fileList.begin(); i != fileList.end() && !(isRelease); ++i) {
				if (regex_match(Text::toT(*i), reg))
					isRelease=true;
			}

			//Report extra files in sample folder
			if (isSample && (nfoFiles > 0 || sfvFiles > 0 || isRelease)) {
				LogManager::getInstance()->message(STRING(EXTRA_FILES_SAMPLEDIR) + path);
				extrasFound++;
			}

			if (isSample)
				return;

			//Report missing SFV or NFO
			if (isRelease) {
				//Simple regex for release names
				reg.assign(_T("(([A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))"));
				if (SETTING(CHECK_NFO) && nfoFiles == 0 && regex_match(dirName,reg)) {
					LogManager::getInstance()->message(STRING(NFO_MISSING) + path);
					missingNFO++;
				}
				if (sfvFiles == 0) {
					reg.assign(_T("(.{0,5}[Ss]ub(s)?)")); //avoid extra matches
					if (!regex_match(dirName,reg) && SETTING(CHECK_SFV)) {
						LogManager::getInstance()->message(STRING(SFV_MISSING) + path);
						missingSFV++;
					}
					return;
				}
			}
		}
	}

	if (sfvFiles == 0)
		return;


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
				LogManager::getInstance()->message("failed to open " + sfvFile);

			while( getline( sfv, line ) ) {
				//make sure that the line is valid
				pos = line.find(";");
				if(regex_search(Text::toT(line), reg) && !(std::string::npos != pos)) {
					releaseFiles++;
					//only keep the filename
					pos = line.rfind(" ");
					line = Text::toLower(line.substr(0,pos));

					StringIter k = std::find(fileList.begin(), fileList.end(), line);
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
			int otherAllowed = 0;
			//Find extra files from the release folder
			for(i = fileList.begin(); i != fileList.end(); ++i) {
				reg.assign(_T("(.+\\.(jpg|jpeg|m3u|cue|diz))"));
				if (regex_match(Text::toT(*i), reg))
					otherAllowed++;
			}
			int allowed = releaseFiles + nfoFiles + sfvFiles + otherAllowed;
			if ((int)fileList.size() > allowed) {
				LogManager::getInstance()->message(STRING(EXTRA_FILES_RLSDIR) + path);
				extrasFound++;
			}
		}
	
}

tstring SFVReaderManager::getDir(tstring dir) {
		string directory = Text::fromT(dir);
		if (dir != Util::emptyStringT) {
			directory = directory.substr(0, directory.size()-1);

			int dpos = directory.rfind("\\");
			if(dpos != wstring::npos) {
				directory = directory.substr(dpos+1,directory.size());
			}
		}
		return Text::toT(directory);
}


void SFVReader::load(const string& fileName) throw() {
	string path = Util::getFilePath(fileName);
	string fname = Util::getFileName(fileName);
	StringList files = File::findFiles(path, "*.sfv");

	for(StringIter i = files.begin(); i != files.end(); ++i) {
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
