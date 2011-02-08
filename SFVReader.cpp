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

int SFVReaderManager::scan() {
	if(scanning.test_and_set())
		LogManager::getInstance()->message("Scan in Progress");

	start();
	
	return 0;
}
int SFVReaderManager::run() {
	StringPairList dirs = ShareManager::getInstance()->getDirectories(ShareManager::REFRESH_ALL);

	for(StringPairIter i = dirs.begin(); i != dirs.end();    i++) {
		find(i->second);
		findMissing(i->second);
		LogManager::getInstance()->message("Scanned " + i->second);
	}
	
	scanning.clear();
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
				LogManager::getInstance()->message("Scanned " + dir);
				dirs.push_back(dir);
				}
			}
	 }
		for(StringIterC j = dirs.begin(); j != dirs.end(); j++) {
			find(*j);
		}	}

int SFVReaderManager::findMissing(const string& path) throw(FileException) {
	StringList files;
	string sfvFile;
	StringList sfvFiles = File::findFiles(path, "*.sfv");
	int missingFiles=0;
	int pos;

	//regex to match crc32
	boost::wregex reg;
	reg.assign(_T("(\\s(\\w{8})$)"));

	ifstream sfv;
	string line;


	if (SETTING(SETTINGS_PROFILE) == SettingsManager::PROFILE_RAR) {
		StringList nfoFiles = File::findFiles(path, "*.nfo");

		//Check for multiple NFO or SFV files
		if (nfoFiles.size() > 1)
			LogManager::getInstance()->message(STRING(MULTIPLE_NFO) + path);
		if (sfvFiles.size() > 1)
			LogManager::getInstance()->message(STRING(MULTIPLE_SFV) + path);

		if (nfoFiles.empty() || sfvFiles.empty()) {

			//Check that it's a release folder
			StringList releases = File::findFiles(path, "*.rar");
			if (releases.empty()) {
				releases = File::findFiles(path, "*.000");
			}

			//Report missing SFV or NFO for release folders
			if (!releases.empty()) {
				if (nfoFiles.empty())
					LogManager::getInstance()->message(STRING(NFO_MISSING) + path);
				if (sfvFiles.empty()) 
					LogManager::getInstance()->message(STRING(SFV_MISSING) + path);
			}
		}
	}
	for(StringIter i = sfvFiles.begin(); i != sfvFiles.end(); ++i) {
			sfvFile = *i;

			sfv.open(sfvFile);
			while( getline( sfv, line ) ) {
				//make sure that the line is valid
				if(regex_search(Text::toT(line), reg)) {
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
	
	return missingFiles;
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
