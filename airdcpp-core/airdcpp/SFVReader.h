/*
 * Copyright (C) 2001-2018 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SFV_READER_H
#define DCPLUSPLUS_DCPP_SFV_READER_H

#include <stdint.h>
#include <string>

#include "typedefs.h"
#include "GetSet.h"

namespace dcpp {

using std::string;

class DirSFVReader {
public:
	DirSFVReader();
	DirSFVReader(const string& aPath);
	DirSFVReader(const string& aPath, const StringList& aSfvFiles);

	/**
	 * Search for a CRC32 file in all .sfv files in the directory of fileName.
	 * Each SFV file has a number of lines containing a filename and its CRC32 value
	 * in the form:
	 * filename.ext xxxxxxxx
	 * where the x's represent the file's crc32 value. Lines starting with ';' are
	 * considered comments, and we throw away lines with ' ' or '#' as well
	 * (pdSFV 1.2 does this...).
	 */
	optional<uint32_t> hasFile(const string& fileName) const noexcept;

	bool hasSFV() const { return !sfvFiles.empty(); }
	bool isCrcValid(const string& aFile) const;

	/* Loops through the file names */
	void read(std::function<void (const string&)> aReadF) const;

	void loadPath(const string& aPath);
	string getPath() const noexcept { return path; }
	void unload() noexcept;

	const StringList& getFailedFiles() const noexcept {
		return failedFiles;
	}
private:
	bool loaded = false;

	StringList sfvFiles;
	StringList failedFiles;
	string path;

	/* File name + crc */
	unordered_map<string, uint32_t> content;

	void load() noexcept;
	bool loadFile(const string& aContent) noexcept;
};

} // namespace dcpp

#endif // !defined(SFV_READER_H)
