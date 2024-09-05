/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#include "SFVReader.h"

#include "File.h"
#include "FileReader.h"
#include "FilteredFile.h"
#include "LogManager.h"
#include "StringTokenizer.h"
#include "Text.h"
#include "ZUtils.h"

#include <boost/algorithm/string/trim.hpp>

#include <fstream>


namespace dcpp {

using ranges::find_if;

boost::regex DirSFVReader::crcReg = boost::regex(R"(.{5,200}\s(\w{8})$)");
boost::regex DirSFVReader::lineBreakRegex = boost::regex(R"(\n|\r)");

DirSFVReader::DirSFVReader() : loaded(false) { }

DirSFVReader::DirSFVReader(const string& aPath) {
	loadPath(aPath);
}

DirSFVReader::DirSFVReader(const string& /*aPath*/, const StringList& aSfvFiles) : sfvFiles(aSfvFiles) {
	load();
}

void DirSFVReader::loadPath(const string& aPath) noexcept {
	content.clear();
	path = aPath;

	try {
		sfvFiles = File::findFiles(path, "*.sfv", File::TYPE_FILE);
	} catch (const FileException& e) {
		dcdebug("SFV reader: failed to load path %s (%s)", aPath.c_str(), e.getError().c_str());
		return;
	}

	load();
}

void DirSFVReader::unload() noexcept {
	failedFiles.clear();
	content.clear();
	loaded = false;
}

optional<uint32_t> DirSFVReader::hasFile(const string& aFileName) const noexcept {
	dcassert(Text::isLower(aFileName));
	auto p = content.find(aFileName);
	if (p != content.end()) {
		return p->second;
	}

	return nullopt;
}

bool DirSFVReader::isCrcValid(const string& aFileName) const {
	dcassert(Text::isLower(aFileName));
	auto p = content.find(aFileName);
	if (p != content.end()) {
		CRC32Filter crc32;
		FileReader(FileReader::ASYNC).read(path + aFileName, [&](const void* x, size_t n) {
			return crc32(x, n), true;
		});
		return crc32.getValue() == p->second;
	}

	return true;
}

bool DirSFVReader::loadFile(const string& aContent) noexcept {
	/* Get the filename and crc */
	bool hasValidLines = false;
	string line;

	StringTokenizer<string> tokenizer(aContent, lineBreakRegex);
	for (const auto& rawLine: tokenizer.getTokens()) {
		line = Text::toUtf8(rawLine);

		// Make sure that the line is valid
		if (!regex_search(line, crcReg) || line.find(';') == 0) {
			continue;
		}

		//We cant handle sfv with files in subdirectories currently.
		if (line.find('\\') != string::npos) {
			hasValidLines = true;
			continue;
		}

		//only keep the filename
		auto pos = line.rfind(' ');
		if (pos == string::npos) {
			continue;
		}

		uint32_t crc32;
		sscanf(line.substr(pos + 1, 8).c_str(), "%x", &crc32);

		line = Text::toLower(line.substr(0, pos));
		boost::trim(line);

		//quoted filename?
		if (line[0] == '\"' && line[line.length() - 1] == '\"') {
			line = line.substr(1, line.length() - 2);
		}

		content[line] = crc32;
		hasValidLines = true;
	}

	return hasValidLines;
}

void DirSFVReader::load() noexcept {
	for (const auto& curPath: sfvFiles) {
		string error;
		try {
			File f(curPath, File::READ, File::OPEN);
			if (f.getSize() > Util::convertSize(1, Util::MB)) {
				// This isn't a proper sfv file
				error = STRING_F(SFV_TOO_LARGE, Util::formatBytes(f.getSize()));
			} else if (!loadFile(f.read())) {
				error = STRING(NO_VALID_LINES);
			}
		} catch(const FileException& e) {
			error = e.getError();
		}

		if (!error.empty()) {
			LogManager::getInstance()->message(curPath + ": " + error, LogMessage::SEV_ERROR, STRING(SFV_READER));
			failedFiles.push_back(curPath);
		}
	}

	loaded = true;
}

void DirSFVReader::read(std::function<void (const string&)> aReadF) const {
	for (const auto& p: content | views::keys) {
		aReadF(p);
	}
}

} // namespace dcpp
