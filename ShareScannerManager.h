/*
 * Copyright (C) 2011-2013 AirDC++ Project
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

#include "Speaker.h"
#include "Thread.h"
#include "Singleton.h"
#include <string>

#include "noexcept.h"
#include "atomic.h"
#include "boost/unordered_map.hpp"

#include "SFVReader.h"

namespace dcpp {

using std::string;


class ScannerManagerListener {
public:
	virtual ~ScannerManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> ScanFinished;

	virtual void on(ScanFinished, const string&, const string&) noexcept { }
};


class ShareScannerManager: public Singleton<ShareScannerManager>, public Thread, public Speaker<ScannerManagerListener> {
 
public:
	enum ScanType {
		TYPE_FULL,
		TYPE_PARTIAL,
		TYPE_FINISHED,
		TYPE_FAILED_FINISHED,
	};

	int scan(StringList paths = StringList(), bool sfv = false);
	void scanBundle(BundlePtr aBundle, bool& hasMissing, bool& hasExtras);
	void checkFileSFV(const string& path, DirSFVReader& sfv, bool isDirScan);
	void Stop();

private:
	friend class Singleton<ShareScannerManager>;
	typedef vector<pair<string, DirSFVReader>> SFVScanList;

	ShareScannerManager();
	~ShareScannerManager();
	
	int run();
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

	static atomic_flag scanning;

	int crcOk;
	int crcInvalid;
	int checkFailed;

	int64_t scanFolderSize;
	volatile bool stop;
	boost::unordered_multimap<string, string> dupeDirs;
	StringList findFiles(const string& path, const string& pattern, bool dirs, bool matchSkipList);
	void prepareSFVScanDir(const string& path, SFVScanList& dirs);
	void prepareSFVScanFile(const string& path, StringList& files);
	StringList bundleDirs;

	struct ScanInfo {
		ScanInfo(const string& aRootPath, ScanType aScanType) : rootPath(aRootPath), scanType(aScanType), missingFiles(0), missingSFV(0), missingNFO(0), extrasFound(0), noReleaseFiles(0), emptyFolders(0), dupesFound(0) {}

		int missingFiles;
		int missingSFV;
		int missingNFO;
		int extrasFound;
		int noReleaseFiles;
		int emptyFolders;
		int dupesFound;

		ScanType scanType;
		string rootPath;
		string scanMessage;

		void reportResults() const;
		bool hasMissing() const;
		bool hasExtras() const;
		void merge(ScanInfo& collect) const;
	};

	typedef vector<ScanInfo> ScanInfoList;

	void find(const string& path, ScanInfo& aScan);
	void scanDir(const string& path, ScanInfo& aScan);
	void findDupes(const string& path, ScanInfo& aScan);

	void reportMessage(const string& aMessage, ScanInfo& aScan, bool warning = true);

	SharedMutex cs;
};

} // namespace dcpp

#endif // !defined(SHARE_SCANNER_MANAGER_H)
