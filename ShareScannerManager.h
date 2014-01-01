/*
 * Copyright (C) 2011-2014 AirDC++ Project
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

#include <string>

#include "noexcept.h"
#include "atomic.h"

#include "Bundle.h"
#include "CriticalSection.h"
#include "SFVReader.h"
#include "Speaker.h"
#include "Singleton.h"
#include "Thread.h"

#include <boost/regex.hpp>

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

	int scan(const StringList& paths = StringList(), bool sfv = false) noexcept;
	Bundle::Status onScanBundle(const BundlePtr& aBundle, string& error_) noexcept;
	bool onScanSharedDir(const string& aDir, bool report) noexcept;

	void checkFileSFV(const string& path, DirSFVReader& sfv, bool isDirScan) noexcept;
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
	unordered_multimap<string, string> dupeDirs;
	void prepareSFVScanDir(const string& path, SFVScanList& dirs) noexcept;
	void prepareSFVScanFile(const string& path, StringList& files) noexcept;
	StringList bundleDirs;

	struct ScanInfo {
		enum ReportType {
			TYPE_COLLECT_LOG,
			TYPE_SYSLOG,
			TYPE_NOREPORT
		};

		ScanInfo(const string& aRootPath, ReportType aReportType, bool aIsManualShareScan) : rootPath(aRootPath), reportType(aReportType), isManualShareScan(aIsManualShareScan) {}

		string rootPath;
		ReportType reportType;
		bool isManualShareScan;

		int missingFiles = 0;
		int missingSFV = 0;
		int missingNFO = 0;
		int extrasFound = 0;
		int noReleaseFiles = 0;
		int emptyFolders = 0;
		int dupesFound = 0;

		string scanMessage;

		string getResults() const;
		bool hasMissing() const;
		bool hasExtras() const;
		void merge(ScanInfo& collect) const;
	};

	typedef vector<ScanInfo> ScanInfoList;

	void find(const string& path, const string& aPathLower, ScanInfo& aScan) noexcept;
	void scanDir(const string& path, ScanInfo& aScan) noexcept;
	void findDupes(const string& path, ScanInfo& aScan) noexcept;

	void reportMessage(const string& aMessage, ScanInfo& aScan, bool warning = true) noexcept;

	SharedMutex cs;
};

} // namespace dcpp

#endif // !defined(SHARE_SCANNER_MANAGER_H)
