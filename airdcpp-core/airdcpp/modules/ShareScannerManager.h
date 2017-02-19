/*
 * Copyright (C) 2011-2017 AirDC++ Project
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

#include <airdcpp/CriticalSection.h>
#include <airdcpp/DispatcherQueue.h>
#include <airdcpp/SFVReader.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Thread.h>

#include <boost/regex.hpp>

namespace dcpp {

class ScannerManagerListener {
public:
	virtual ~ScannerManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> ScanFinished;

	virtual void on(ScanFinished, const string&, const string&) noexcept { }
};

class FileFindIter;

class ShareScannerManager: public Singleton<ShareScannerManager>, public Thread, public Speaker<ScannerManagerListener> {

#define SHARE_SCANNER_HOOK_ID "share_scanner"
#define SHARE_SCANNER_ERROR_MISSING "error_missing"
#define SHARE_SCANNER_ERROR_INVALID_CONTENT "error_invalid"

#define SFV_CHECKER_HOOK_ID "sfv_checker"
#define SFV_CHECKER_ERROR_CRC "error_crc"

public:
	enum ScanType {
		TYPE_FULL,
		TYPE_PARTIAL,
		TYPE_FINISHED,
		TYPE_FAILED_FINISHED,
	};

	void scanShare(const StringList& paths = StringList()) noexcept;
	void checkSfv(const StringList& paths) noexcept;
	bool onScanSharedDir(const string& aDir, bool report) noexcept;

	void checkFileSFV(const string& path, DirSFVReader& sfv, bool isDirScan) noexcept;
	void Stop();

private:
	friend class Singleton<ShareScannerManager>;
	typedef vector<pair<string, DirSFVReader>> SFVScanList;

	ShareScannerManager();
	~ShareScannerManager();
	
	int run();
	bool validateShare(FileFindIter& aIter, const string& aPath);
	void runSfvCheck(const StringList& paths);
	void runShareScan(const StringList& paths);

	ActionHookRejectionPtr bundleCompletionHook(const BundlePtr& aBundle, const HookRejectionGetter& aErrorGetter) noexcept;
	ActionHookRejectionPtr fileCompletionHook(const QueueItemPtr& aFile, const HookRejectionGetter& aErrorGetter) noexcept;

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
	boost::regex sampleExtrasReg;
	boost::regex subDirReg;
	boost::regex subReg;
	boost::regex extraRegs[3];
	boost::regex diskReg;

	static atomic_flag scanning;

	int crcOk;
	int crcInvalid;
	int checkFailed;
	int64_t scanFolderSize;


	volatile bool stop;
	unordered_multimap<string, string> dupeDirs;
	void prepareSFVScanDir(const string& path, SFVScanList& dirs) noexcept;
	void prepareSFVScanFile(const string& path, StringList& files) noexcept;

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
		int disksMissing = 0;
		int invalidSFVFiles = 0;

		string scanMessage;

		string getResults() const;
		bool hasMissing() const;
		bool hasExtras() const;
		void merge(ScanInfo& collect) const;
	};

	typedef vector<ScanInfo> ScanInfoList;

	void find(const string& aPath, ScanInfo& aScan) noexcept;
	void scanDir(const string& aPath, ScanInfo& aScan) noexcept;
	void findDupes(const string& aPath, ScanInfo& aScan) noexcept;

	void reportMessage(const string& aMessage, ScanInfo& aScan, bool warning = true) noexcept;

	SharedMutex cs;
	DispatcherQueue tasks;
};

} // namespace dcpp

#endif // !defined(SHARE_SCANNER_MANAGER_H)
