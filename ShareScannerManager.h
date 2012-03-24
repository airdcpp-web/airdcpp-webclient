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

#ifndef DCPLUSPLUS_DCPP_SHARE_SCANNER_MANAGER_H_
#define DCPLUSPLUS_DCPP_SHARE_SCANNER_MANAGER_H_

#include "File.h"
#include "Thread.h"
#include "Singleton.h"
#include <string>
#include "pme.h"

#include "noexcept.h"
#include "atomic.h"
#include "boost/unordered_map.hpp"

#include "SFVReader.h"

namespace dcpp {

using std::string;


class ShareScannerManager: public Singleton<ShareScannerManager>, public Thread {
 
public:

	void find (const string& path, int& missingFiles, int& missingSFV, int& missingNFO, int& extrasFound, int& dupesFound, int& emptyFolders, bool isBundleScan);
	void scanDir(const string& path, int& missingFiles, int& missingSFV, int& missingNFO, int& extrasFound, int& emptyFolders);
	int scan(StringList paths = StringList(), bool sfv = false);
	bool scanBundle(BundlePtr aBundle);
	void checkFileSFV(const string& path, DirSFVReader& sfv, bool isDirScan);
	void Stop();

private:
	friend class Singleton<ShareScannerManager>;
	typedef vector<pair<string, DirSFVReader>> SFVScanList;

	ShareScannerManager();
	~ShareScannerManager();
	
	int run();
	PME skipListReg;
	bool matchSkipList(const string& dir);


	enum extraTypes {
		AUDIOBOOK,
		FLAC,
		NORMAL,
	};

	boost::regex rarReg;
	boost::regex rarMp3Reg;
	boost::regex longReleaseReg;
	boost::regex releaseReg;
	boost::regex simpleReleaseReg;
	boost::regex audioBookReg;
	boost::regex flacReg;
	boost::regex emptyDirReg;
	boost::regex mvidReg;
	boost::regex zipReg;
	boost::regex zipFolderReg;
	boost::regex proofImageReg;
	boost::regex subDirReg;
	boost::regex subReg;
	boost::regex extraRegs[3];

	StringList rootPaths;
	bool isCheckSFV;
	bool isDirScan;

	atomic_flag scanning;

	int crcOk;
	int crcInvalid;
	int checkFailed;

	int64_t scanFolderSize;
	bool stop;
	void findDupes(const string& path, int& dupesFound);
	boost::unordered_multimap<string, string, noCaseStringHash, noCaseStringEq> dupeDirs;
	StringList findFiles(const string& path, const string& pattern, bool dirs, bool matchSkipList);
	void prepareSFVScanDir(const string& path, SFVScanList& dirs);
	void prepareSFVScanFile(const string& path, StringList& files);
	StringList bundleDirs;

	void reportResults(const string& path, int scanType, int missingFiles, int missingSFV, int missingNFO, int extrasFound, int emptyFolders, int dupesFound = 0);
};

} // namespace dcpp

#endif // !defined(SHARE_SCANNER_MANAGER_H)
