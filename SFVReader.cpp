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
	partialScan = false;

	if(scanning.test_and_set()){
		LogManager::getInstance()->message("Scan in Progress"); //translate
		return 1;
	}
		
	if(!paths.empty()) {
		partialScan = true;
		Paths = paths;
	}

	start();
	missingFiles = 0;
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

	//toString would work too but int MissingFiles needs to be initialized, otherwise to string will crash it
	string tmp;	 
    tmp.resize(STRING(MISSING_FINISHED).size() + 16);	 
    tmp.resize(snprintf(&tmp[0], tmp.size(), CSTRING(MISSING_FINISHED), missingFiles));	 
    LogManager::getInstance()->message(tmp);
	
	scanning.clear();
	dupeDirs.clear();
	missingFiles = 0;
	Paths.clear();
	partialScan = false;
	return 0;
}

//Test if this works, have another way of doing it but it would need some work
void SFVReaderManager::find(const string& path) {
	
	string dir;
	StringList dirs;
	 for(FileFindIter i(path + "*"); i != FileFindIter(); ++i) {
			if(i->isDirectory()){
		if (strcmpi (i->getFileName().c_str(), ".") != 0 && strcmpi (i->getFileName().c_str(), "..") != 0) {
				dir = path + i->getFileName() + "\\";
				findMissing(dir);
				findDupes(dir);
				dirs.push_back(dir);
				}
			}
	 }
		for(StringIterC j = dirs.begin(); j != dirs.end(); j++) {
			find(*j);
		}	
	}

void SFVReaderManager::findDupes(const string& path) throw(FileException) {
	boost::wregex reg;
	int dupes=0;
	reg.assign(_T("([A-Z0-9][A-Za-z0-9]\\S{3,})-(\\w{2,})"));
	tstring dirName = getDir(Text::toT(path));
	string listfolder;
	if (!regex_match(dirName, reg))
		return;


	if (dupeDirs.size() != NULL) {
		for(StringPairIter i = dupeDirs.begin(); i != dupeDirs.end();    i++) {
			std::string listfolder = i->first;
			if (!stricmp(Text::fromT(dirName), listfolder)) {
				dupes++;
				LogManager::getInstance()->message(STRING(DUPE_FOUND) + path + " " + STRING(DUPE_IS_SAME) + " " + (i->second) + ")");
			}
		}
	}
	dupeDirs.push_back(make_pair(Text::fromT(dirName), path));
	return;
}

void SFVReaderManager::findMissing(const string& path) throw(FileException) {
	StringList files;
	StringList sfvFiles = File::findFiles(path, "*.sfv");
	int pos;
	boost::wregex reg;


	if (SETTING(SETTINGS_PROFILE) == SettingsManager::PROFILE_RAR) {
		tstring dirName = getDir(Text::toT(path));
		StringList nfoFiles = File::findFiles(path, "*.nfo");
		bool isSample=false;
		bool sampleExtra=false;
		//Check for multiple NFO or SFV files
		if (nfoFiles.size() > 1)
			LogManager::getInstance()->message(STRING(MULTIPLE_NFO) + path);
		if (sfvFiles.size() > 1)
			LogManager::getInstance()->message(STRING(MULTIPLE_SFV) + path);

		//Check if it's sample folder
		if (!strcmp(Text::fromT(Text::toLower(dirName)).c_str(), "sample")) {
			isSample=true;
		}

		if (nfoFiles.empty() || sfvFiles.empty() || isSample) {

			//Check if it's a release folder
			StringList releases = File::findFiles(path, "*.rar");
			if (releases.empty()) {
				releases = File::findFiles(path, "*.000");
			} 	if (releases.empty() && isSample) {
				files = File::findFiles(path, "*");
				reg.assign(_T("(.+\\.r\\w{2})"));
				for(StringIter i = sfvFiles.begin(); i != sfvFiles.end() && !(sampleExtra); ++i) {
					if (regex_match(Text::toT(*i), reg))
						sampleExtra=true;
				}
			}

			//Report extra files in sample folder
			if (isSample && (!releases.empty() || !nfoFiles.empty() || !sfvFiles.empty() || sampleExtra)) {
				LogManager::getInstance()->message(STRING(EXTRA_FILES_SAMPLEDIR) + path);
			}

			if (isSample)
				return;

			//Report missing SFV or NFO
			if (!releases.empty()) {
				//Simple regex for release names
				reg.assign(_T("([A-Z0-9][A-Za-z0-9]\\S{3,})-(\\w{2,})"));
				if (nfoFiles.empty() && regex_match(dirName,reg))
					LogManager::getInstance()->message(STRING(NFO_MISSING) + path);
				if (sfvFiles.empty()) {
					reg.assign(_T("(.{2,5}[Ss]ub(s)?)")); //avoid extra matches
					if (!regex_match(dirName,reg))
						LogManager::getInstance()->message(STRING(SFV_MISSING) + path);
					return;
				}
			}
		}
	}

	
	ifstream sfv;
	string line;
	string sfvFile;

	//regex to match crc32
	reg.assign(_T("(\\s(\\w{8})$)"));

	for(StringIter i = sfvFiles.begin(); i != sfvFiles.end(); ++i) {
			sfvFile = *i;

			sfv.open(sfvFile);
			while( getline( sfv, line ) ) {
				//make sure that the line is valid
				pos = line.find(";");
				if(regex_search(Text::toT(line), reg) && !(std::string::npos != pos)) {
					//only keep the filename
					pos = line.rfind(" ");
					line = line.substr(0,pos+1);
					files = File::findFiles(path, line);
					if (files.size() == NULL) {
						LogManager::getInstance()->message(STRING(FILE_MISSING) + " " + path + line);
						missingFiles++;
					} else {
						files.clear();
					}
				}
			}
			sfv.close();
		}
	
	return;
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
