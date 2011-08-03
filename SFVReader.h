/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#include "File.h"
#include "Thread.h"
#include "Singleton.h"
#include <atomic>
#include <string>
#include "pme.h"

#include "noexcept.h"

namespace dcpp {

using std::string;

class SFVReaderManager;

class SFVReader  {
public:
	/** @see load */
	SFVReader(const string& aFileName) : crc32(0), crcFound(false) { load(aFileName); }

	/**
	 * Search for a CRC32 file in all .sfv files in the directory of fileName.
	 * Each SFV file has a number of lines containing a filename and its CRC32 value
	 * in the form:
	 * filename.ext xxxxxxxx
	 * where the x's represent the file's crc32 value. Lines starting with ';' are
	 * considered comments, and we throw away lines with ' ' or '#' as well
	 * (pdSFV 1.2 does this...).
	 */
	void load(const string& fileName) noexcept;
	void loadFromFolder(const string& fullPath) noexcept;
	
	bool hasCRC() const noexcept { return crcFound; }

	uint32_t getCRC() const noexcept { return crc32; }
	

private:
	friend class SFVReaderManager;
	uint32_t crc32;
	bool crcFound;

	bool tryFile(const string& sfvFile, const string& fileName);
	
};
 

class SFVReaderManager: public Singleton<SFVReaderManager>, public Thread
{
 
public:

 void find (const string& path);
 bool findMissing(const string& path);
 int scan(StringList paths = StringList(), bool sfv = false);
 void checkSFV(const string& path);
 void Stop();

private:
friend class Singleton<SFVReaderManager>;

	SFVReaderManager() : scanning(false){ }
	~SFVReaderManager() { 
		Stop();
		join();
	}
	
 int run();
 PME skipListReg;
 bool matchSkipList(const string& dir);

 StringList Paths;
 bool isCheckSFV;

 atomic_flag scanning;
 int extrasFound;
 int missingNFO;
 int missingSFV;
 int dupesFound;
 int missingFiles;

 int crcOk;
 int crcInvalid;
 int checkFailed;

 int64_t scanFolderSize;
 bool stop;
 void findDupes(const string& path);
 StringPairList dupeDirs;
 StringList findFiles(const string& path, const string& pattern, bool dirs = false);
 uint32_t calcCrc32(const string& file);
 void getScanSize(const string& path);
};

} // namespace dcpp

#endif // !defined(SFV_READER_H)
