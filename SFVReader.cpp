/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#include "SFVReader.h"

#include "AirUtil.h"
#include "File.h"
#include "FileReader.h"
#include "FilteredFile.h"
#include "LogManager.h"
#include "Text.h"
#include "ZUtils.h"

#include <boost/algorithm/string/trim.hpp>

//#include <iostream>
#include <fstream>

namespace dcpp {

using boost::range::find_if;

DirSFVReader::DirSFVReader() : loaded(false) { }

DirSFVReader::DirSFVReader(const string& aPath) : loaded(false) {
	loadPath(aPath);
}

DirSFVReader::DirSFVReader(const string& /*aPath*/, const StringList& aSfvFiles, StringList& invalidSFV) : loaded(false) {
	sfvFiles = aSfvFiles;
	load(invalidSFV);
}

void DirSFVReader::loadPath(const string& aPath) {
	content.clear();
	path = aPath;
	sfvFiles = File::findFiles(path, "*.sfv", File::TYPE_FILE);
	StringList tmp;
	load(tmp);
}

void DirSFVReader::unload() {
	if (loaded) {
		content.clear();
		loaded = false;
	}
}

optional<uint32_t> DirSFVReader::hasFile(const string& fileName) const {
	if (loaded) {
		auto p = content.find(fileName);
		if (p != content.end()) {
			return p->second;
		}
	}
	return nullptr;
}

bool DirSFVReader::isCrcValid(const string& fileName) const {
	auto p = content.find(fileName);
	if (p != content.end()) {
		CRC32Filter crc32;
		FileReader(true).read(path + fileName, [&](const void* x, size_t n) {
			return crc32(x, n), true;
		});
		return crc32.getValue() == p->second;
	}

	return true;
}

//use a custom implementation in order to detect line breaks used by other operating systems
std::istream& getline(std::istream &is, std::string &s) { 
    char ch;

    s.clear();

	while (is.get(ch) && ch != '\n' && ch != '\r')
        s += ch;
    return is;
}

void DirSFVReader::load(StringList& invalidSFV) noexcept {
	string line;

	for(auto path: sfvFiles) {
		ifstream sfv;
		
		/* Try to open the sfv */
		try {
			auto loadPath = Text::utf8ToAcp(Util::FormatPath(path));
			auto size = File::getSize(loadPath);
			if (size > Util::convertSize(1, Util::MB)) {
				//this isn't a proper sfv file
				throw FileException(STRING_F(SFV_TOO_LARGE, Util::formatBytes(size)));
			}

			//incase we have some extended characters in the path
			sfv.open(loadPath);

			if(!sfv.is_open()) {
				throw FileException(STRING(CANT_OPEN_SFV));
			}
		} catch(const FileException& e) {
			invalidSFV.push_back(path);
			LogManager::getInstance()->message(path + ": " + e.getError(), LogManager::LOG_ERROR);
			continue;
		}

		/* Get the filename and crc */
		bool hasValidLines = false;
		while(getline(sfv, line) || !line.empty()) {
			line = Text::toUtf8(line);
			//make sure that the line is valid
			if(regex_search(line, AirUtil::crcReg) && (line.find(";") == string::npos)) {
				//We cant handle sfv with files in subdirectories currently.
				if (line.find("\\") != string::npos) {
					hasValidLines = true;
					continue;
				}

				//only keep the filename
				size_t pos = line.rfind(" ");
				if (pos == string::npos) {
					continue;
				}

				uint32_t crc32;
				sscanf(line.substr(pos+1, 8).c_str(), "%x", &crc32);

				line = Text::toLower(line.substr(0,pos));

				boost::trim(line);

				//quoted filename?
				if (line[0] == '\"' && line[line.length()-1] == '\"') {
					line = line.substr(1,line.length()-2);
				}

				content[line] = crc32;
				hasValidLines = true;
			}
		}
		sfv.close();
		if (!hasValidLines)
			invalidSFV.push_back(path);
	}

	loaded = true;
}

void DirSFVReader::read(std::function<void (const string&)> readF) const {
	for (auto& p: content | map_keys) {
		readF(p);
	}
}

} // namespace dcpp
