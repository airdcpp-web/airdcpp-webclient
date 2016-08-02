/* 
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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
#include "QueueManager.h"

#include "AirUtil.h"
#include "Bundle.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "DebugManager.h"
#include "DirectoryListing.h"
#include "DirectoryListingManager.h"
#include "Download.h"
#include "DownloadManager.h"
#include "FileReader.h"
#include "HashManager.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "ScopedFunctor.h"
#include "SearchManager.h"
#include "SearchResult.h"
#include "ShareManager.h"
#include "ShareScannerManager.h"
#include "SimpleXMLReader.h"
#include "Transfer.h"
#include "UploadManager.h"
#include "UserConnection.h"
#include "ZUtils.h"
#include "version.h"

#include <boost/range/algorithm/copy.hpp>

#ifdef _WIN32
#include <mmsystem.h>
#include <limits>
#endif

#if !defined(_WIN32) && !defined(PATH_MAX) // Extra PATH_MAX check for Mac OS X
#include <sys/syslimits.h>
#endif

#ifdef ff
#undef ff
#endif

namespace dcpp {

using boost::range::for_each;

QueueManager::QueueManager() : 
	udp(Socket::TYPE_UDP),
	tasks(true)
{ 
	//add listeners in loadQueue
	File::ensureDirectory(Util::getListPath());
	File::ensureDirectory(Util::getBundlePath());
}

QueueManager::~QueueManager() { 

}

void QueueManager::shutdown() noexcept {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);
	HashManager::getInstance()->removeListener(this);
	ShareManager::getInstance()->removeListener(this);

	if (SETTING(REMOVE_FINISHED_BUNDLES)){
		WLock l(cs);
		BundleList bl;
		for (auto& b : bundleQueue.getBundles() | map_values) {
			if (b->isFinished())
				bl.push_back(b);
		}
		for_each(bl.begin(), bl.end(), [=](BundlePtr& b) { bundleQueue.removeBundle(b); });
	}

	saveQueue(false);

	if (!SETTING(KEEP_LISTS)) {
		string path = Util::getListPath();

		std::sort(protectedFileLists.begin(), protectedFileLists.end());

		StringList filelists = File::findFiles(path, "*.xml.bz2", File::TYPE_FILE);
		std::sort(filelists.begin(), filelists.end());
		std::for_each(filelists.begin(), std::set_difference(filelists.begin(), filelists.end(),
			protectedFileLists.begin(), protectedFileLists.end(), filelists.begin()), &File::deleteFile);
	}
}

void QueueManager::recheckBundle(QueueToken aBundleToken) noexcept {
	QueueItemList ql;
	BundlePtr b;
	bool isFinished;

	{
		RLock l(cs);
		b = bundleQueue.findBundle(aBundleToken);
		if (!b) {
			return;
		}

		isFinished = b->isFinished();
		copy(b->getQueueItems().begin(), b->getQueueItems().end(), back_inserter(ql));
		copy(b->getFinishedFiles().begin(), b->getFinishedFiles().end(), back_inserter(ql));
	}

	int64_t finishedSegmentsBegin = accumulate(ql.begin(), ql.end(), (int64_t)0, [&](int64_t old, const QueueItemPtr& qi) {
		auto size = File::getSize(qi->getTarget());
		if (size == -1) {
			size = File::getSize(qi->getTempTarget());
		}
		return size > 0 ? old + size : old;
	});

	LogManager::getInstance()->message(STRING_F(INTEGRITY_CHECK_START_BUNDLE, b->getName() %
		Util::formatBytes(finishedSegmentsBegin)), LogMessage::SEV_INFO);
	

	// prepare for checking
	auto oldPrio = b->getPriority();
	auto oldStatus = b->getStatus();

	setBundlePriority(b, QueueItemBase::PAUSED_FORCE);
	Thread::sleep(1000);

	setBundleStatus(b, Bundle::STATUS_RECHECK);

	// check the files
	int64_t failedBytes = 0;
	
	QueueItemList failedItems;
	for (auto& q : ql) {
		if (recheckFileImpl(q->getTarget(), true, failedBytes)) {
			failedItems.push_back(q);
		}
	}

	// finish
	LogManager::getInstance()->message(STRING_F(INTEGRITY_CHECK_FINISHED_BUNDLE, b->getName() %
		Util::formatBytes(failedBytes)), LogMessage::SEV_INFO);

	b->setStatus(oldStatus);
	handleFailedRecheckItems(failedItems);
	setBundlePriority(b, oldPrio);
}

void QueueManager::recheckFiles(QueueItemList aQL) noexcept {
	LogManager::getInstance()->message(STRING_F(INTEGRITY_CHECK_START_FILES, aQL.size()), LogMessage::SEV_INFO);

	QueueItemList failedItems;
	int64_t failedBytes = 0;
	for (auto& q : aQL) {
		bool running;

		{
			RLock l(cs);
			running = q->isRunning();
		}

		auto oldPrio = q->getPriority();
		setQIPriority(q, QueueItemBase::PAUSED_FORCE);
		if (running) {
			Thread::sleep(1000);
		}

		if (recheckFileImpl(q->getTarget(), false, failedBytes)) {
			failedItems.push_back(q);
		}

		setQIPriority(q, oldPrio);
	}

	handleFailedRecheckItems(failedItems);
	LogManager::getInstance()->message(STRING_F(INTEGRITY_CHECK_FINISHED_FILES, Util::formatBytes(failedBytes)), LogMessage::SEV_INFO);
}

void QueueManager::handleFailedRecheckItems(const QueueItemList& ql) noexcept {
	if (ql.empty())
		return;

	auto b = ql.front()->getBundle();
	dcassert(b);

	{
		WLock l(cs);
		for (auto q : ql) {
			bundleQueue.removeBundleItem(q, false);

			q->unsetFlag(QueueItem::FLAG_MOVED);
			q->unsetFlag(QueueItem::FLAG_FINISHED);
			q->unsetFlag(QueueItem::FLAG_HASHED);
			q->setBundle(nullptr);
			q->setTimeFinished(0);

			bundleQueue.addBundleItem(q, b);

			userQueue.addQI(q);
		}

		readdBundle(b);
	}

	fire(QueueManagerListener::BundleStatusChanged(), b);
}

bool QueueManager::recheckFileImpl(const string& aPath, bool isBundleCheck, int64_t& failedBytes_) noexcept{
	QueueItemPtr q;
	int64_t tempSize;
	TTHValue tth;
	string checkTarget;

	auto failFile = [&](const string& aError) {
		fire(QueueManagerListener::FileRecheckFailed(), q, aError);
		LogManager::getInstance()->message(STRING_F(INTEGRITY_CHECK, aError % q->getTarget()), LogMessage::SEV_ERROR);
	};

	{
		RLock l(cs);

		q = fileQueue.findFile(aPath);
	}

	if (!q || q->isSet(QueueItem::FLAG_USER_LIST))
		return false;

	fire(QueueManagerListener::FileRecheckStarted(), q->getTarget());

	{
		RLock l(cs);
		dcdebug("Rechecking %s\n", aPath.c_str());

		// always check the final target in case of files added from other sources
		checkTarget = Util::fileExists(q->getTarget()) ? q->getTarget() : q->getTempTarget();
		tempSize = File::getSize(checkTarget);

		if (tempSize == -1) {
			if (q->getDownloadedBytes() > 0)
				failFile(STRING(UNFINISHED_FILE_NOT_FOUND));
			return false;
		}

		if (tempSize < Util::convertSize(64, Util::KB)) {
			if (!isBundleCheck)
				failFile(STRING(UNFINISHED_FILE_TOO_SMALL));
			return false;
		}

		if (tempSize != q->getSize()) {
			if (checkTarget == q->getTarget()) {
				failFile(STRING(SIZE_MISMATCH));
				return false;
			}

			try {
				File(checkTarget, File::WRITE, File::OPEN).setSize(q->getSize());
			} catch (const FileException& e) {
				failFile(e.getError());
				return false;
			}
		}

		if (q->isRunning()) {
			failFile(STRING(DOWNLOADS_RUNNING));
			return false;
		}

		tth = q->getTTH();
	}

	TigerTree tt;
	bool gotTree = HashManager::getInstance()->getTree(tth, tt);
	QueueItem::SegmentSet done;

	{
		RLock l(cs);

		// get q again in case it has been (re)moved
		q = fileQueue.findFile(aPath);
		if (!q)
			return false;

		if (!gotTree) {
			failFile(STRING(NO_FULL_TREE));
			return false;
		}

		//Clear segments
		done = q->getDone();
		q->resetDownloaded();
	}

	TigerTree ttFile(tt.getBlockSize());
	DirSFVReader sfv(q->getFilePath());
	auto fileCRC = sfv.hasFile(Text::toLower(q->getTargetFileName()));
	CRC32Filter crc32;

	try {
		FileReader().read(checkTarget, [&](const void* x, size_t n) {
			if (fileCRC)
				crc32(x, n);
			return ttFile.update(x, n), true;
		});
	} catch (const FileException & e) {
		dcdebug("Error while reading file: %s\n", e.what());
		failFile(e.getError());
		return false;
	}

	{
		RLock l(cs);
		// get q again in case it has been (re)moved
		q = fileQueue.findFile(aPath);
	}

	if (!q)
		return false;

	ttFile.finalize();

	int64_t pos = 0, failedBytes = 0;

	{
		WLock l(cs);
		boost::for_each(tt.getLeaves(), ttFile.getLeaves(), [&](const TTHValue& our, const TTHValue& file) {
			// avoid going over the file size (would happen especially with finished items)
			auto blockSegment = Segment(pos, min(q->getSize() - pos, tt.getBlockSize()));

			if (our == file) {
				q->addFinishedSegment(blockSegment);
			} else {
				// undownloaded segments aren't corrupted...
				if (!blockSegment.inSet(done))
					return;

				dcdebug("Integrity check failed for the block at pos " I64_FMT "\n", pos);
				failedBytes += tt.getBlockSize();
			}

			pos += tt.getBlockSize();
		});

	}

	if (failedBytes > 0) {
		failedBytes_ += failedBytes;
		LogManager::getInstance()->message(STRING_F(INTEGRITY_CHECK,
			STRING_F(FILE_CORRUPTION_FOUND, Util::formatBytes(failedBytes)) % q->getTarget()),
			LogMessage::SEV_WARNING);
	} else if (fileCRC && ttFile.getRoot() == tth && *fileCRC != crc32.getValue()) {
		LogManager::getInstance()->message(q->getTarget() + ": " + STRING(ERROR_HASHING_CRC32), LogMessage::SEV_ERROR);
	}

	if (ttFile.getRoot() == tth && !q->isSet(QueueItem::FLAG_FINISHED)) {
		q->setTimeFinished(GET_TIME());
		q->setFlag(QueueItem::FLAG_FINISHED);

		{
			WLock l(cs);
			userQueue.removeQI(q);
		}

		removeBundleItem(q, true);

		//If no bad blocks then the file probably got stuck in the temp folder for some reason
		if (checkTarget != q->getTarget()) {
			renameDownloadedFile(q->getTempTarget(), q->getTarget(), q);
		} else {
			q->setFlag(QueueItem::FLAG_MOVED);
		}

		//fire(QueueManagerListener::ItemStatusUpdated(), q);

		//failFile(STRING(FILE_ALREADY_FINISHED));
		return false;
	}

	// we will also resume files that are added in the destination directory from other sources
	if (!q->isFinished() && (q->isSet(QueueItem::FLAG_FINISHED) || q->getTarget() == checkTarget)) {
		try {
			File::renameFile(q->getTarget(), q->getTempTarget());
		} catch (const FileException& e) {
			LogManager::getInstance()->message(STRING_F(UNABLE_TO_RENAME, q->getTarget() % e.getError()), LogMessage::SEV_ERROR);
		}
	}

	if (q->isSet(QueueItem::FLAG_FINISHED) && !q->isFinished()) {
		return true;
	}

	fire(QueueManagerListener::FileRecheckDone(), q->getTarget());
	fire(QueueManagerListener::ItemStatusUpdated(), q);
	return false;
}

void QueueManager::getBloom(HashBloom& bloom) const noexcept {
	RLock l(cs);
	fileQueue.getBloom(bloom);
}

size_t QueueManager::getQueuedBundleFiles() const noexcept {
	RLock l(cs);
	return bundleQueue.getTotalFiles();
}

bool QueueManager::getSearchInfo(const string& aTarget, TTHValue& tth_, int64_t size_) noexcept {
	RLock l(cs);
	QueueItemPtr qi = fileQueue.findFile(aTarget);
	if(qi) {
		tth_ = qi->getTTH();
		size_ = qi->getSize();
		return true;
	}
	return false;
}

struct PartsInfoReqParam{
	PartsInfo	parts;
	string		tth;
	string		myNick;
	string		hubIpPort;
	string		ip;
	string		udpPort;
};

void QueueManager::requestPartialSourceInfo(uint64_t aTick) noexcept {
	BundlePtr bundle;
	vector<const PartsInfoReqParam*> params;

	{
		RLock l(cs);

		//find max 10 pfs sources to exchange parts
		//the source basis interval is 5 minutes
		FileQueue::PFSSourceList sl;
		fileQueue.findPFSSources(sl);

		for (auto& i : sl) {
			auto source = i.first->getPartialSource();
			const QueueItemPtr qi = i.second;

			PartsInfoReqParam* param = new PartsInfoReqParam;

			qi->getPartialInfo(param->parts, qi->getBlockSize());

			param->tth = qi->getTTH().toBase32();
			param->ip = source->getIp();
			param->udpPort = source->getUdpPort();
			param->myNick = source->getMyNick();
			param->hubIpPort = source->getHubIpPort();

			params.push_back(param);

			source->setPendingQueryCount((uint8_t)(source->getPendingQueryCount() + 1));
			source->setNextQueryTime(aTick + 300000);		// 5 minutes
		}
	}

	// Request parts info from partial file sharing sources
	for (auto& param : params) {
		//dcassert(param->udpPort > 0);

		try {
			AdcCommand cmd = SearchManager::getInstance()->toPSR(true, param->myNick, param->hubIpPort, param->tth, param->parts);
			COMMAND_DEBUG(cmd.toString(), DebugManager::TYPE_CLIENT_UDP, DebugManager::OUTGOING, param->ip);
			udp.writeTo(param->ip, param->udpPort, cmd.toString(ClientManager::getInstance()->getMyCID()));
		} catch (...) {
			dcdebug("Partial search caught error\n");
		}

		delete param;
	}
}

void QueueManager::searchAlternates(uint64_t aTick) noexcept {
	if (!SETTING(AUTO_SEARCH) || !SETTING(AUTO_ADD_SOURCE)) {
		return;
	}

	BundlePtr bundle;

	{
		RLock l(cs);
		if (SETTING(AUTO_SEARCH) && SETTING(AUTO_ADD_SOURCE))
			bundle = bundleQueue.findSearchItem(aTick); //may modify the recent search queue
	}

	if (bundle) {
		searchBundleAlternates(bundle, false, aTick);
	}
}

void QueueManager::getBundleContent(const BundlePtr& aBundle, size_t& files_, size_t& directories_) const noexcept {
	RLock l(cs);
	files_ = aBundle->getQueueItems().size() + aBundle->getFinishedFiles().size();
	directories_ = aBundle->isFileBundle() ? 0 : bundleQueue.getDirectoryCount(aBundle) - 1;
}

bool QueueManager::hasDownloadedBytes(const string& aTarget) throw(QueueException) {
	RLock l(cs);
	auto q = fileQueue.findFile(aTarget);
	if (!q)
		throw QueueException(STRING(TARGET_REMOVED));

	return q->getDownloadedBytes() > 0;
}

QueueItemPtr QueueManager::addList(const HintedUser& aUser, Flags::MaskType aFlags, const string& aInitialDir /* = Util::emptyString */, BundlePtr aBundle /*nullptr*/) throw(QueueException, FileException) {
	//check the source
	checkSource(aUser);
	//dcassert(!aUser.hint.empty());
	if (aUser.hint.empty())
		throw QueueException(STRING(HUB_UNKNOWN));

	//format the target
	string target;
	if((aFlags & QueueItem::FLAG_PARTIAL_LIST) && !aInitialDir.empty()) {
		StringList nicks = ClientManager::getInstance()->getNicks(aUser);
		if (nicks.empty())
			throw QueueException(STRING(INVALID_TARGET_FILE));
		target = Util::getListPath() + nicks[0] + ".partial[" + Util::validateFileName(aInitialDir) + "]";
	} else {
		target = getListPath(aUser);
	}


	//add in queue

	QueueItemPtr q = nullptr;
	{
		WLock l(cs);
		auto ret = fileQueue.add(target, -1, (Flags::MaskType)(QueueItem::FLAG_USER_LIST | aFlags), QueueItem::HIGHEST, aInitialDir, GET_TIME(), TTHValue());
		if (!ret.second) {
			//exists already
			throw QueueException(STRING(LIST_ALREADY_QUEUED));
		}

		q = std::move(ret.first);
		addSource(q, aUser, true, false);
		if (aBundle) {
			q->setFlag(QueueItem::FLAG_MATCH_BUNDLE);
			matchLists.insert(TokenStringMultiBiMap::value_type(aBundle->getToken(), q->getTarget()));
		}
	}

	fire(QueueManagerListener::ItemAdded(), q);

	//connect
	ConnectionManager::getInstance()->getDownloadConnection(aUser, (aFlags & QueueItem::FLAG_PARTIAL_LIST) || (aFlags & QueueItem::FLAG_TTHLIST_BUNDLE));
	return q;
}

string QueueManager::getListPath(const HintedUser& user) const noexcept {
	StringList nicks = ClientManager::getInstance()->getNicks(user);
	string nick = nicks.empty() ? Util::emptyString : Util::validateFileName(nicks[0]) + ".";
	return Util::getListPath() + nick + user.user->getCID().toBase32();
}

bool QueueManager::replaceItem(QueueItemPtr& q, int64_t aSize, const TTHValue& aTTH) throw(FileException, QueueException) {
	if(q->isFinished()) {
		/* The target file doesn't exist, add our item. Also recheck the existance in case of finished files being moved on the same time. */
		dcassert(q->getBundle());
		if (!Util::fileExists(q->getTarget()) && q->getBundle() && q->isSet(QueueItem::FLAG_MOVED)) {
			bundleQueue.removeBundleItem(q, false);
			fileQueue.remove(q);
			return true;
		} else {
			throw FileException(STRING(FILE_ALREADY_FINISHED));
		}
	} else {
		/* try to add the source for the existing item */
		if(q->getSize() != aSize) {
			throw QueueException(STRING(FILE_WITH_DIFFERENT_SIZE));
		}

		if(aTTH != q->getTTH()) {
			throw QueueException(STRING(FILE_WITH_DIFFERENT_TTH));
		}
	}

	return false;
}

void QueueManager::setMatchers() noexcept {
	auto sl = SETTING(SKIPLIST_DOWNLOAD);
	skipList.pattern = SETTING(SKIPLIST_DOWNLOAD);
	skipList.setMethod(SETTING(DOWNLOAD_SKIPLIST_USE_REGEXP) ? StringMatch::REGEX : StringMatch::WILDCARD);
	skipList.prepare();

	highPrioFiles.pattern = SETTING(HIGH_PRIO_FILES);
	highPrioFiles.setMethod(SETTING(HIGHEST_PRIORITY_USE_REGEXP) ? StringMatch::REGEX : StringMatch::WILDCARD);
	highPrioFiles.prepare();
}

void QueueManager::checkSource(const HintedUser& aUser) const throw(QueueException) {
	// Check that we're not downloading from ourselves...
	if(aUser.user == ClientManager::getInstance()->getMe()) {
		throw QueueException(STRING(NO_DOWNLOADS_FROM_SELF));
	}

	// Check the encryption
	if (aUser.user && aUser.user->isOnline() && !aUser.user->isNMDC() && !aUser.user->isSet(User::TLS) && SETTING(TLS_MODE) == SettingsManager::TLS_FORCED) {
		throw QueueException(ClientManager::getInstance()->getFormatedNicks(aUser) + ": " + STRING(SOURCE_NO_ENCRYPTION));
	}
}

void QueueManager::validateBundleFile(const string& aBundleDir, string& bundleFile_, const TTHValue& aTTH, QueueItemBase::Priority& priority_) const throw(QueueException, FileException, DupeException) {

	//check the skiplist

	auto matchSkipList = [&] (string&& aName) -> void {
		if(skipList.match(aName)) {
			throw QueueException(STRING(DOWNLOAD_SKIPLIST_MATCH));
		}
	};

	//match the file name
	matchSkipList(Util::getFileName(bundleFile_));

	//match all dirs (if any)
	string::size_type i = 0, j = 0;
	while ((i = bundleFile_.find(PATH_SEPARATOR, j)) != string::npos) {
		matchSkipList(bundleFile_.substr(j, i - j));
		j = i + 1;
	}


	//validate the target and check the existance
	bundleFile_ = checkTarget(bundleFile_, aBundleDir);

	//check share dupes
	if (SETTING(DONT_DL_ALREADY_SHARED) && ShareManager::getInstance()->isFileShared(aTTH)) {
		auto paths = ShareManager::getInstance()->getRealPaths(aTTH);
		if (!paths.empty()) {
			auto path = AirUtil::subtractCommonDirs(aBundleDir, Util::getFilePath(paths.front()), PATH_SEPARATOR);
			throw DupeException(STRING_F(TTH_ALREADY_SHARED, path));
		}
	}

	//check queue dupes
	if (SETTING(DONT_DL_ALREADY_QUEUED)) {
		RLock l(cs);
		auto q = fileQueue.getQueuedFile(aTTH);
		if (q && q->getTarget() != aBundleDir + bundleFile_) {
			auto path = AirUtil::subtractCommonDirs(aBundleDir, q->getFilePath(), PATH_SEPARATOR);
			throw DupeException(STRING_F(FILE_ALREADY_QUEUED, path));
		}
	}

	if(SETTING(USE_FTP_LOGGER)) {
		AirUtil::fileEvent(aBundleDir + bundleFile_);
	}


	//valid file

	//set the prio
	if (highPrioFiles.match(Util::getFileName(bundleFile_))) {
		priority_ = SETTING(PRIO_LIST_HIGHEST) ? QueueItem::HIGHEST : QueueItem::HIGH;
	}
}

QueueItemPtr QueueManager::addOpenedItem(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, bool aIsClientView, bool aIsText) throw(QueueException, FileException) {
	//check the source
	if (aUser.user)
		checkSource(aUser);

	//check the size
	if (aSize == 0) {
		//can't view this...
		throw QueueException(STRING(CANT_OPEN_EMPTY_FILE));
	} else if (aIsClientView && aIsText && aSize > Util::convertSize(1, Util::MB)) {
		auto msg = STRING_F(VIEWED_FILE_TOO_BIG, aFileName % Util::formatBytes(aSize));
		LogManager::getInstance()->message(msg, LogMessage::SEV_ERROR);
		throw QueueException(msg);
	}

	//check the target
	auto target = Util::getOpenPath() + AirUtil::toOpenFileName(aFileName, aTTH);

	//add in queue
	QueueItemPtr qi = nullptr;
	bool wantConnection = false, added = false;

	Flags::MaskType flags;
	if (aIsClientView) {
		flags = (aIsText ? QueueItem::FLAG_TEXT : 0) | QueueItem::FLAG_CLIENT_VIEW;
	} else {
		flags = QueueItem::FLAG_OPEN;
	}

	{
		WLock l(cs);
		auto ret = fileQueue.add(target, aSize, flags, QueueItem::HIGHEST, Util::emptyString, GET_TIME(), aTTH);

		qi = std::move(ret.first);
		added = ret.second;

		wantConnection = addSource(qi, aUser, true, false);
	}

	if (added) {
		fire(QueueManagerListener::ItemAdded(), qi);
	}

	//connect
	if(wantConnection || qi->usesSmallSlot()) {
		ConnectionManager::getInstance()->getDownloadConnection(aUser, qi->usesSmallSlot());
	}

	return qi;
}

BundlePtr QueueManager::getBundle(const string& aTarget, QueueItemBase::Priority aPrio, time_t aDate, bool isFileBundle) noexcept {
	auto b = bundleQueue.getMergeBundle(aTarget);
	if (!b) {
		// create a new bundle
		b = BundlePtr(new Bundle(aTarget, GET_TIME(), aPrio, aDate, 0, true, isFileBundle));
	} else {
		// use an existing one
		dcassert(!AirUtil::isSubLocal(b->getTarget(), aTarget));
	}

	return b;
}

class ErrorReporter {
public:
	struct Error {
		Error(const string& aFile, bool aIsMinor) : file(aFile), isMinor(aIsMinor) { }

		string file;
		bool isMinor;
	};

	ErrorReporter(int totalFileCount) : fileCount(totalFileCount) { }

	void add(const string& aError, const string& aFile, bool aIsMinor) {
		errors.emplace(aError, Error(aFile, aIsMinor));

	}
	
	void clearMinor() {
		errors.erase(boost::remove_if(errors | map_values, [](const Error& e) { return e.isMinor; }).base(), errors.end());
	}

	string getMessage() {
		if (errors.empty()) {
			return Util::emptyString;
		}

		StringList msg;

		//get individual errors
		StringSet errorNames;
		for (const auto& p: errors | map_keys) {
			errorNames.insert(p);
		}

		for (const auto& e: errorNames) {
			auto c = errors.count(e);
			if (c <= 3) {
				//report each file
				StringList paths;
				auto k = errors.equal_range(e);
				for (auto i = k.first; i != k.second; ++i)
					paths.push_back(i->second.file);

				string r = Util::toString(", ", paths);
				msg.push_back(STRING_F(X_FILE_NAMES, e % r));
			} else {
				//too many errors, report the total failed count
				msg.push_back(STRING_F(X_FILE_COUNT, e % c % fileCount));
			}
		}

		// long errors will crash the debug build.. fix maybe
		return Util::toString(", ", msg);
	}

private:
	int fileCount;
	unordered_multimap<string, Error> errors;
};

BundlePtr QueueManager::createDirectoryBundle(const string& aTarget, const HintedUser& aUser, BundleFileInfo::List& aFiles, QueueItemBase::Priority aPrio, time_t aDate, string& errorMsg_) throw(QueueException) {
	string target = formatBundleTarget(aTarget, aDate);

	// Check if the target is allowed
	{
		BundleList subBundles;
		bundleQueue.getSubBundles(target, subBundles);
		if (!subBundles.empty()) {
			StringList subPaths;
			for (const auto& b : subBundles) {
				subPaths.push_back(b->getTarget());
			}

			throw QueueException(STRING_F(BUNDLE_ERROR_SUBBUNDLES, subBundles.size() % target % AirUtil::subtractCommonParents(target, subPaths)));
		}
	}

	int fileCount = aFiles.size();

	//check the source
	if (aUser.user) {
		//it's up to the caller to catch this one
		checkSource(aUser);
	}

	ErrorReporter errors(fileCount);

	int existingFiles = 0, smallDupes=0;

	//check the files
	aFiles.erase(boost::remove_if(aFiles, [&](BundleFileInfo& bfi) {
		try {
			validateBundleFile(target, bfi.file, bfi.tth, bfi.prio);
			return false; // valid
		} catch(QueueException& e) {
			errors.add(e.getError(), bfi.file, false);
		} catch(FileException& /*e*/) {
			existingFiles++;
		} catch(DupeException& e) {
			bool isSmall = bfi.size < Util::convertSize(SETTING(MIN_DUPE_CHECK_SIZE), Util::KB);
			errors.add(e.getError(), bfi.file, isSmall);
			if (isSmall) {
				smallDupes++;
				return false;
			}
		}

		return true;
	}), aFiles.end());


	//check the errors
	if (aFiles.empty()) {
		//report existing files only if all files exist to prevent useless spamming
		if (existingFiles == fileCount) {
			errorMsg_ = STRING_F(ALL_BUNDLE_FILES_EXIST, existingFiles);
			return nullptr;
		}

		errorMsg_ = errors.getMessage();
		return nullptr;
	} else if (smallDupes > 0) {
		if (smallDupes == static_cast<int>(aFiles.size())) {
			//no reason to continue if all remaining files are dupes
			errorMsg_ = errors.getMessage();
			return nullptr;
		} else {
			//those will get queued, don't report
			errors.clearMinor();
		}
	}



	BundlePtr b = nullptr;
	bool wantConnection = false;
	int newItems = 0;
	Bundle::Status oldStatus;

	QueueItem::ItemBoolList items;
	{
		WLock l(cs);
		b = getBundle(target, aPrio, aDate, false);
		oldStatus = b->getStatus();

		//add the files
		for (auto& bfi: aFiles) {
			try {
				auto addInfo = addBundleFile(target + bfi.file, bfi.size, bfi.tth, aUser, 0, true, bfi.prio, wantConnection, b);
				if (!addInfo) {
					continue;
				}

				if ((*addInfo).second) {
					newItems++;
				}

				items.push_back(*addInfo);
			} catch(QueueException& e) {
				errors.add(e.getError(), bfi.file, false);
			} catch(FileException& /*e*/) {
				//the file has finished after we made the initial target check, don't add error for those
				existingFiles++;
			}
		}

		if (!addBundle(b, newItems)) {
			b = nullptr;
		}
	}

	if (existingFiles == fileCount) {
		errorMsg_ = STRING_F(ALL_BUNDLE_FILES_EXIST, existingFiles);
		return nullptr;
	}

	if (b) {
		// Those don't need to be reported to the user
		errors.clearMinor();

		onBundleAdded(b, oldStatus, items, aUser, wantConnection);

		if (newItems > 0) {
			// Report
			if (oldStatus == Bundle::STATUS_NEW) {
				LogManager::getInstance()->message(STRING_F(BUNDLE_CREATED, b->getName() % newItems) + " (" + CSTRING_F(TOTAL_SIZE, Util::formatBytes(b->getSize())) + ")", LogMessage::SEV_INFO);
			} else if (b->getTarget() == target) {
				LogManager::getInstance()->message(STRING_F(X_BUNDLE_ITEMS_ADDED, newItems % b->getName().c_str()), LogMessage::SEV_INFO);
			} else {
				LogManager::getInstance()->message(STRING_F(BUNDLE_MERGED, Util::getLastDir(target) % b->getName() % newItems), LogMessage::SEV_INFO);
			}
		}
	}

	errorMsg_ = errors.getMessage();
	return b;
}

void QueueManager::addLoadedBundle(BundlePtr& aBundle) noexcept {
	WLock l(cs);
	if (aBundle->isEmpty())
		return;

	if (bundleQueue.getMergeBundle(aBundle->getTarget()))
		return;

	bundleQueue.addBundle(aBundle);
}

bool QueueManager::addBundle(BundlePtr& aBundle, int aItemsAdded) noexcept {
	if (aBundle->getQueueItems().empty() && aItemsAdded > 0) {
		// it finished already? (only 0 byte files were added)
		tasks.addTask([=] {
			BundlePtr b = aBundle;
			checkBundleFinished(b);
		});

		return false;
	}

	if (aItemsAdded == 0)
		return true;

	if (aBundle->getStatus() == Bundle::STATUS_NEW) {
		bundleQueue.addBundle(aBundle);
	} else {
		if (static_cast<int>(aBundle->getQueueItems().size()) == aItemsAdded) {
			// Finished bundle but failed hashing/scanning?
			readdBundle(aBundle);
		} else {
			aBundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
			addBundleUpdate(aBundle);
			aBundle->setDirty();
		}
	}

	return true;
}

void QueueManager::onBundleAdded(const BundlePtr& aBundle, Bundle::Status aOldStatus, const QueueItem::ItemBoolList& aItemsAdded, const HintedUser& aUser, bool aWantConnection) noexcept {
	if (aOldStatus == Bundle::STATUS_NEW) {
		fire(QueueManagerListener::BundleAdded(), aBundle);

		if (SETTING(AUTO_SEARCH) && SETTING(AUTO_ADD_SOURCE) && !aBundle->isPausedPrio()) {
			aBundle->setFlag(Bundle::FLAG_SCHEDULE_SEARCH);
			addBundleUpdate(aBundle);
		}
	} else {
		if (aOldStatus > Bundle::STATUS_DOWNLOADED) {
			fire(QueueManagerListener::BundleStatusChanged(), aBundle);
		}

		fire(QueueManagerListener::BundleSources(), aBundle);

		for (const auto& itemInfo : aItemsAdded) {
			if (itemInfo.second) {
				fire(QueueManagerListener::ItemAdded(), itemInfo.first);
			} else {
				fire(QueueManagerListener::ItemSourcesUpdated(), itemInfo.first);
			}
		}
	}

	fire(QueueManagerListener::SourceFilesUpdated(), aUser);

	if (aWantConnection && aUser.user->isOnline()) {
		//connect to the source (we must have an user in this case)
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
	}
}

string QueueManager::formatBundleTarget(const string& aPath, time_t aRemoteDate) noexcept {
	ParamMap params;
	params["username"] = [] { return Util::getSystemUsername(); };
	
	auto time = (SETTING(FORMAT_DIR_REMOTE_TIME) && aRemoteDate > 0) ? aRemoteDate : GET_TIME();
	auto formatedPath = Util::formatParams(aPath, params, nullptr, time);
	return Util::validatePath(formatedPath);
}

BundlePtr QueueManager::createFileBundle(const string& aTarget, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, time_t aDate, 
		Flags::MaskType aFlags, QueueItemBase::Priority aPrio) throw(QueueException, FileException, DupeException) {

	string filePath = formatBundleTarget(Util::getFilePath(aTarget), aDate);
	string fileName = Util::getFileName(aTarget);

	//check the source
	if (aUser.user) {
		checkSource(aUser);
	}

	validateBundleFile(filePath, fileName, aTTH, aPrio);

	BundlePtr b = nullptr;
	bool wantConnection = false;

	auto target = filePath + fileName;

	Bundle::Status oldStatus;
	FileAddInfo fileAddInfo;

	{
		WLock l(cs);
		b = getBundle(target, aPrio, aDate, true);
		oldStatus = b->getStatus();

		fileAddInfo = addBundleFile(target, aSize, aTTH, aUser, aFlags, true, aPrio, wantConnection, b);

		if (!addBundle(b, fileAddInfo && (*fileAddInfo).second ? 1 : 0)) {
			b = nullptr;
		}
	}

	if (fileAddInfo && b) {
		onBundleAdded(b, oldStatus, { *fileAddInfo }, aUser, wantConnection);

		if ((*fileAddInfo).second) {
			if (oldStatus == Bundle::STATUS_NEW) {
				LogManager::getInstance()->message(STRING_F(FILE_X_QUEUED, b->getName() % Util::formatBytes(b->getSize())), LogMessage::SEV_INFO);
			} else {
				LogManager::getInstance()->message(STRING_F(BUNDLE_ITEM_ADDED, Util::getFileName(target) % b->getName()), LogMessage::SEV_INFO);
			}
		}
	}

	return b;
}

QueueManager::FileAddInfo QueueManager::addBundleFile(const string& aTarget, int64_t aSize, const TTHValue& aRoot, const HintedUser& aUser, Flags::MaskType aFlags /* = 0 */,
								   bool addBad /* = true */, QueueItemBase::Priority aPrio, bool& wantConnection_, BundlePtr& aBundle) throw(QueueException, FileException)
{
	// Handle zero byte items
	if(aSize == 0) {
		if(!SETTING(SKIP_ZERO_BYTE)) {
			File::ensureDirectory(aTarget);
			File f(aTarget, File::WRITE, File::CREATE);
		}

		return boost::none;
	}

	// Add the file
	auto ret = fileQueue.add(aTarget, aSize, aFlags, aPrio, Util::emptyString, GET_TIME(), aRoot);

	if(!ret.second) {
		// Exists already
		if (replaceItem(ret.first, aSize, aRoot)) {
			ret = std::move(fileQueue.add(aTarget, aSize, aFlags, aPrio, Util::emptyString, GET_TIME(), aRoot));
		}
	}
	
	// New item? Add in the bundle
	if (ret.second) {
		// Highest wouldn't be started if the bundle is forced paused
		if (aBundle->getPriority() == Bundle::PAUSED && ret.first->getPriority() == QueueItem::HIGHEST) {
			ret.first->setPriority(QueueItem::HIGH);
		}

		bundleQueue.addBundleItem(ret.first, aBundle);
	}

	// Add the source
	if (aUser.user) {
		try {
			if (addSource(ret.first, aUser, (Flags::MaskType)(addBad ? QueueItem::Source::FLAG_MASK : 0), false)) {
				wantConnection_ = true;
			}
		} catch(const Exception&) {
			dcassert(!ret.second);
			//This should never fail for new items, and for existing items it doesn't matter (useless spam)
		}
	}

	return ret;
}

bool QueueManager::readdQISource(const string& target, const HintedUser& aUser) noexcept {
	QueueItemPtr qi = nullptr;

	{
		WLock l(cs);
		qi = fileQueue.findFile(target);
		if (!qi || !qi->isBadSource(aUser)) {
			return false;
		}
	}

	auto added = addSources(aUser, { qi }, QueueItem::Source::FLAG_MASK);
	return added > 0;
}

void QueueManager::readdBundleSource(BundlePtr aBundle, const HintedUser& aUser) noexcept {
	QueueItemList items;

	{
		WLock l(cs);
		for (const auto& q: aBundle->getQueueItems()) {
			dcassert(!q->isSource(aUser));
			if (q->isBadSource(aUser.user)) {
				items.push_back(q);
			}
		}
	}

	addSources(aUser, items, QueueItem::Source::FLAG_MASK);
}

string QueueManager::checkTarget(const string& toValidate, const string& aParentDir /*empty*/) throw(QueueException, FileException) {
#ifdef _WIN32
	if(toValidate.length()+aParentDir.length() > UNC_MAX_PATH) {
		throw QueueException(STRING(TARGET_FILENAME_TOO_LONG));
	}

	if (aParentDir.empty()) {
		// Check that target starts with a drive or is an UNC path
		if( (toValidate[1] != ':' || toValidate[2] != '\\') &&
			(toValidate[0] != '\\' && toValidate[1] != '\\') ) {
			throw QueueException(STRING(INVALID_TARGET_FILE));
		}
	}
#else
	if(toValidate.length()+aParentDir.length() > PATH_MAX) {
		throw QueueException(STRING(TARGET_FILENAME_TOO_LONG));
	}

	if (aParentDir.empty()) {
		// Check that target contains at least one directory...we don't want headless files...
		if(toValidate[0] != '/') {
			throw QueueException(STRING(INVALID_TARGET_FILE));
		}
	}
#endif

	string target = Util::validatePath(toValidate);

	// Check that the file doesn't already exist...
	if (Util::fileExists(aParentDir + target)) {
		/* TODO: add for recheck */
		throw FileException(STRING(TARGET_FILE_EXISTS));
	}
	return target;	
}

/** Add a source to an existing queue item */
bool QueueManager::addSource(QueueItemPtr& qi, const HintedUser& aUser, Flags::MaskType addBad, bool checkTLS /*true*/) throw(QueueException, FileException) {
	if (!aUser.user) //atleast magnet links can cause this to happen.
		throw QueueException("Can't find Source user to add For Target: " + qi->getTargetFileName());

	if (checkTLS && !aUser.user->isSet(User::NMDC) && !aUser.user->isSet(User::TLS) && SETTING(TLS_MODE) == SettingsManager::TLS_FORCED) {
		throw QueueException(STRING(SOURCE_NO_ENCRYPTION));
	}

	if(qi->isFinished()) //no need to add source to finished item.
		throw QueueException("Already Finished: " + Util::getFileName(qi->getTarget()));
	
	bool wantConnection = !qi->isPausedPrio();
	dcassert(qi->getBundle() || qi->getPriority() == QueueItem::HIGHEST);

	if(qi->isSource(aUser)) {
		if(qi->isSet(QueueItem::FLAG_USER_LIST)) {
			return wantConnection;
		}
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + Util::getFileName(qi->getTarget()));
	}

	bool isBad = false;
	if(qi->isBadSourceExcept(aUser, addBad, isBad)) {
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + Util::getFileName(qi->getTarget()));
	}

	qi->addSource(aUser);
	userQueue.addQI(qi, aUser, isBad);

#ifdef _WIN32
	if ((!SETTING(SOURCEFILE).empty()) && (!SETTING(SOUNDS_DISABLED)))
		PlaySound(Text::toT(SETTING(SOURCEFILE)).c_str(), NULL, SND_FILENAME | SND_ASYNC);
#endif
	if (qi->getBundle()) {
		qi->getBundle()->setDirty();
	}

	return wantConnection;
	
}

Download* QueueManager::getDownload(UserConnection& aSource, const QueueTokenSet& runningBundles, const OrderedStringSet& onlineHubs, string& lastError_, string& newUrl, QueueItemBase::DownloadType aType) noexcept{
	QueueItemPtr q = nullptr;
	Download* d = nullptr;

	{
		WLock l(cs);
		dcdebug("Getting download for %s...", aSource.getUser()->getCID().toBase32().c_str());

		const UserPtr& u = aSource.getUser();
		bool hasDownload = false;

		q = userQueue.getNext(aSource.getUser(), runningBundles, onlineHubs, lastError_, hasDownload, QueueItem::LOWEST, aSource.getChunkSize(), aSource.getSpeed(), aType);
		if (!q) {
			dcdebug("none\n");
			return nullptr;
		}

		auto source = q->getSource(aSource.getUser());

		//update the hub hint
		newUrl = aSource.getHubUrl();
		source->updateHubUrl(onlineHubs, newUrl, (q->isSet(QueueItem::FLAG_USER_LIST) && !q->isSet(QueueItem::FLAG_TTHLIST_BUNDLE)));

		//check partial sources
		if (source->isSet(QueueItem::Source::FLAG_PARTIAL)) {
			Segment segment = q->getNextSegment(q->getBlockSize(), aSource.getChunkSize(), aSource.getSpeed(), source->getPartialSource(), false);
			if (segment.getStart() != -1 && segment.getSize() == 0) {
				// no other partial chunk from this user, remove him from queue
				userQueue.removeQI(q, u);
				q->removeSource(u, QueueItem::Source::FLAG_NO_NEED_PARTS);
				lastError_ = STRING(NO_NEEDED_PART);
				return nullptr;
			}
		}

		// Check that the file we will be downloading to exists
		if (q->getDownloadedBytes() > 0) {
			if (!Util::fileExists(q->getTempTarget())) {
				// Temp target gone?
				q->resetDownloaded();
			}
		}

		d = new Download(aSource, *q);
		userQueue.addDownload(q, d);
	}

	fire(QueueManagerListener::ItemSourcesUpdated(), q);
	dcdebug("found %s for %s (" I64_FMT ", " I64_FMT ")\n", q->getTarget().c_str(), d->getToken().c_str(), d->getSegment().getStart(), d->getSegment().getEnd());
	return d;
}

bool QueueManager::allowStartQI(const QueueItemPtr& aQI, const QueueTokenSet& runningBundles, string& lastError_, bool mcn /*false*/) noexcept{
	// nothing to download?
	if (!aQI)
		return false;

	// override the slot settings for partial lists and small files
	if (aQI->usesSmallSlot())
		return true;


	// paused?
	if (aQI->isPausedPrio())
		return false;

	//Download was failed for writing errors, check if we have enough free space to continue the downloading now...
	if (aQI->getBundle() && aQI->getBundle()->getStatus() == Bundle::STATUS_DOWNLOAD_FAILED) {
		if (File::getFreeSpace(aQI->getBundle()->getTarget()) >= static_cast<int64_t>(aQI->getSize() - aQI->getDownloadedBytes())) {
			setBundleStatus(aQI->getBundle(), Bundle::STATUS_QUEUED);
		} else {
			lastError_ = aQI->getBundle()->getLastError();
			return false;
		}
	}

	size_t downloadCount = DownloadManager::getInstance()->getDownloadCount();
	bool slotsFull = (AirUtil::getSlots(true) != 0) && (downloadCount >= static_cast<size_t>(AirUtil::getSlots(true)));
	bool speedFull = (AirUtil::getSpeedLimit(true) != 0) && (DownloadManager::getInstance()->getRunningAverage() >= Util::convertSize(AirUtil::getSpeedLimit(true), Util::KB));
	//LogManager::getInstance()->message("Speedlimit: " + Util::toString(Util::getSpeedLimit(true)*1024) + " slots: " + Util::toString(Util::getSlots(true)) + " (avg: " + Util::toString(getRunningAverage()) + ")");

	if (slotsFull || speedFull) {
		bool extraFull = (AirUtil::getSlots(true) != 0) && (downloadCount >= static_cast<size_t>(AirUtil::getSlots(true) + SETTING(EXTRA_DOWNLOAD_SLOTS)));
		if (extraFull || mcn || aQI->getPriority() != QueueItem::HIGHEST) {
			lastError_ = slotsFull ? STRING(ALL_DOWNLOAD_SLOTS_TAKEN) : STRING(MAX_DL_SPEED_REACHED);
			return false;
		}
		return true;
	}

	// bundle with the lowest prio? don't start if there are other bundle running
	if (aQI->getBundle() && aQI->getBundle()->getPriority() == QueueItemBase::LOWEST && !runningBundles.empty() && runningBundles.find(aQI->getBundle()->getToken()) == runningBundles.end()) {
		lastError_ = STRING(LOWEST_PRIO_ERR_BUNDLES);
		return false;
	}

	if (aQI->getPriority() == QueueItem::LOWEST) {
		if (aQI->getBundle()) {
			// start only if there are no other downloads running in this bundle (or the downloads belong to this file)
			auto bundleDownloads = DownloadManager::getInstance()->getDownloadCount(aQI->getBundle());

			RLock l(cs);
			bool start = bundleDownloads == 0 || bundleDownloads == aQI->getDownloads().size();
			if (!start) {
				lastError_ = STRING(LOWEST_PRIO_ERR_FILES);
			}

			return start;
		} else {
			// shouldn't happen at the moment
			dcassert(0);
			return downloadCount == 0;
		}
	}

	return true;
}

bool QueueManager::startDownload(const UserPtr& aUser, const QueueTokenSet& runningBundles, const OrderedStringSet& onlineHubs,
	QueueItemBase::DownloadType aType, int64_t aLastSpeed, string& lastError_) noexcept{

	bool hasDownload = false;
	QueueItemPtr qi = nullptr;
	{
		RLock l(cs);
		qi = userQueue.getNext(aUser, runningBundles, onlineHubs, lastError_, hasDownload, QueueItem::LOWEST, 0, aLastSpeed, aType);
	}

	return allowStartQI(qi, runningBundles, lastError_);
}

pair<QueueItem::DownloadType, bool> QueueManager::startDownload(const UserPtr& aUser, string& hubHint, QueueItemBase::DownloadType aType, 
	QueueToken& bundleToken, bool& allowUrlChange, bool& hasDownload, string& lastError_) noexcept{

	QueueTokenSet runningBundles;
	DownloadManager::getInstance()->getRunningBundles(runningBundles);

	auto hubs = ClientManager::getInstance()->getHubSet(aUser->getCID());
	if (!hubs.empty()) {
		QueueItemPtr qi = nullptr;
		{
			RLock l(cs);
			qi = userQueue.getNext(aUser, runningBundles, hubs, lastError_, hasDownload, QueueItem::LOWEST, 0, 0, aType);

			if (qi) {
				if (qi->getBundle()) {
					bundleToken = qi->getBundle()->getToken();
				}

				if (hubs.find(hubHint) == hubs.end()) {
					//we can't connect via a hub that is offline...
					hubHint = *hubs.begin();
				}

				allowUrlChange = !qi->isSet(QueueItem::FLAG_USER_LIST);
				qi->getSource(aUser)->updateHubUrl(hubs, hubHint, (qi->isSet(QueueItem::FLAG_USER_LIST) && !qi->isSet(QueueItem::FLAG_TTHLIST_BUNDLE)));
			}
		}

		if (qi) {
			bool start = allowStartQI(qi, runningBundles, lastError_);
			return { qi->usesSmallSlot() ? QueueItem::TYPE_SMALL : QueueItem::TYPE_ANY, start };
		}
	} else {
		lastError_ = STRING(USER_OFFLINE);
	}

	return { QueueItem::TYPE_NONE, false };
}

void QueueManager::matchListing(const DirectoryListing& dl, int& matchingFiles_, int& newFiles_, BundleList& bundles_) noexcept {
	if (dl.getUser() == ClientManager::getInstance()->getMe())
		return;

	QueueItemList matchingItems;

	{
		RLock l(cs);
		fileQueue.matchListing(dl, matchingItems);
	}

	matchingFiles_ = static_cast<int>(matchingItems.size());

	newFiles_ = addSources(dl.getHintedUser(), matchingItems, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE, bundles_);
}

bool QueueManager::getQueueInfo(const HintedUser& aUser, string& aTarget, int64_t& aSize, int& aFlags, QueueToken& bundleToken) noexcept {
	OrderedStringSet hubs;
	hubs.insert(aUser.hint);

	QueueTokenSet runningBundles;
	DownloadManager::getInstance()->getRunningBundles(runningBundles);

	QueueItemPtr qi = nullptr;
	string lastError_;
	bool hasDownload = false;

	{
		RLock l(cs);
		qi = userQueue.getNext(aUser, runningBundles, hubs, lastError_, hasDownload);
	}

	if (!qi)
		return false;

	aTarget = qi->getTarget();
	aSize = qi->getSize();
	aFlags = qi->getFlags();
	if (qi->getBundle()) {
		bundleToken = qi->getBundle()->getToken();
	}

	return true;
}

void QueueManager::toggleSlowDisconnectBundle(QueueToken aBundleToken) noexcept {
	RLock l(cs);
	auto b = bundleQueue.findBundle(aBundleToken);
	if(b) {
		if(b->isSet(Bundle::FLAG_AUTODROP)) {
			b->unsetFlag(Bundle::FLAG_AUTODROP);
		} else {
			b->setFlag(Bundle::FLAG_AUTODROP);
		}
	}
}

string QueueManager::getTempTarget(const string& aTarget) noexcept {
	RLock l(cs);
	auto qi = fileQueue.findFile(aTarget);
	if(qi) {
		return qi->getTempTarget();
	}
	return Util::emptyString;
}

StringList QueueManager::getTargets(const TTHValue& tth) noexcept {
	QueueItemList ql;
	StringList sl;

	{
		RLock l(cs);
		fileQueue.findFiles(tth, ql);
	}

	for(auto& q: ql)
		sl.push_back(q->getTarget());

	return sl;
}

void QueueManager::renameDownloadedFile(const string& source, const string& target, QueueItemPtr& aQI) noexcept {
	try {
		File::ensureDirectory(target);
		UploadManager::getInstance()->abortUpload(source);
		File::renameFile(source, target);
	} catch(const FileException& e1) {
		// Try to just rename it to the correct name at least
		string newTarget = Util::getFilePath(source) + Util::getFileName(target);
		try {
			File::renameFile(source, newTarget);
			LogManager::getInstance()->message(STRING_F(MOVE_FILE_FAILED, newTarget % Util::getFilePath(target) % e1.getError()), LogMessage::SEV_ERROR);
		} catch(const FileException& e2) {
			LogManager::getInstance()->message(STRING_F(UNABLE_TO_RENAME, source % e2.getError()), LogMessage::SEV_ERROR);
		}
	}
	if(SETTING(USE_FTP_LOGGER))
		AirUtil::fileEvent(target, true);

	if (aQI && aQI->getBundle()) {
		handleMovedBundleItem(aQI);
	}
}

void QueueManager::handleMovedBundleItem(QueueItemPtr& qi) noexcept {
	BundlePtr b = qi->getBundle();

	HintedUserList notified;

	{
		RLock l (cs);

		//collect the users that don't have this file yet
		for (auto& fn: qi->getBundle()->getFinishedNotifications()) {
			if (!qi->isSource(fn.first.user)) {
				notified.push_back(fn.first);
			}
		}
	}

	//send the notifications
	for(auto& u: notified) {
		AdcCommand cmd(AdcCommand::CMD_PBD, AdcCommand::TYPE_UDP);

		cmd.addParam("UP1");
		cmd.addParam("HI", u.hint);
		cmd.addParam("TH", qi->getTTH().toBase32());
		ClientManager::getInstance()->sendUDP(cmd, u.user->getCID(), false, true);
	}


	{
		RLock l (cs);
		//flag this file as moved
		auto s = find_if(b->getFinishedFiles(), [&qi](const QueueItemPtr& aQI) { return aQI->getTarget() == qi->getTarget(); });
		if (s != b->getFinishedFiles().end()) {
			qi->setFlag(QueueItem::FLAG_MOVED);
		} else if (b->getFinishedFiles().empty() && b->getQueueItems().empty()) {
			//the bundle was removed while the file was being moved?
			return;
		}
	}

	fire(QueueManagerListener::ItemStatusUpdated(), qi);

	checkBundleFinished(b);
}


bool QueueManager::checkBundleFinished(BundlePtr& aBundle) noexcept {
	bool hasNotifications = false, isPrivate = false;
	{
		RLock l (cs);
		//check if there are queued or non-moved files remaining
		if (!aBundle->allowHash()) 
			return false;

		// in order to avoid notifications about adding the file in share...
		if (aBundle->isFileBundle() && !aBundle->getFinishedFiles().empty())
			isPrivate = aBundle->getFinishedFiles().front()->isSet(QueueItem::FLAG_PRIVATE);

		hasNotifications = !aBundle->getFinishedNotifications().empty();
	}

	if (hasNotifications) {
		//the bundle has finished downloading so we don't need any partial bundle sharing notifications

		Bundle::FinishedNotifyList fnl;
		{
			WLock l(cs);
			aBundle->clearFinishedNotifications(fnl);
		}

		for(auto& ubp: fnl)
			sendRemovePBD(ubp.first, ubp.second);
	}

	setBundleStatus(aBundle, Bundle::STATUS_MOVED);

	if (!SETTING(SCAN_DL_BUNDLES) || aBundle->isFileBundle()) {
		LogManager::getInstance()->message(STRING_F(DL_BUNDLE_FINISHED, aBundle->getName().c_str()), LogMessage::SEV_INFO);
		setBundleStatus(aBundle, Bundle::STATUS_FINISHED);
	} else if (!scanBundle(aBundle)) {
		return true;
	} 

	if (SETTING(ADD_FINISHED_INSTANTLY)) {
		hashBundle(aBundle);
	} else if (!isPrivate) {
		if (ShareManager::getInstance()->allowAddDir(aBundle->getTarget())) {
			LogManager::getInstance()->message(CSTRING(INSTANT_SHARING_DISABLED), LogMessage::SEV_INFO);
		} else {
			LogManager::getInstance()->message(STRING_F(NOT_IN_SHARED_DIR, aBundle->getTarget().c_str()), LogMessage::SEV_INFO);
		}
	}

	return true;
}

bool QueueManager::scanBundle(BundlePtr& aBundle) noexcept {
	string error_;
	auto newStatus = ShareScannerManager::getInstance()->onScanBundle(aBundle, aBundle->getStatus() == Bundle::STATUS_MOVED, error_);
	if (!error_.empty())
		aBundle->setLastError(error_);

	setBundleStatus(aBundle, newStatus);
	return newStatus == Bundle::STATUS_FINISHED;
}

void QueueManager::hashBundle(BundlePtr& aBundle) noexcept {
	if(ShareManager::getInstance()->allowAddDir(aBundle->getTarget())) {
		setBundleStatus(aBundle, Bundle::STATUS_HASHING);

		QueueItemList hash;
		QueueItemList removed;

		{
			RLock l(cs);
			for (auto& qi: aBundle->getFinishedFiles()) {
				if (ShareManager::getInstance()->checkSharedName(qi->getTarget(), Text::toLower(qi->getTarget()), false, true, qi->getSize()) && Util::fileExists(qi->getTarget())) {
					qi->unsetFlag(QueueItem::FLAG_HASHED);
					hash.push_back(qi);
				} else {
					removed.push_back(qi);
				}
			}
		}

		if (!removed.empty()) {
			WLock l (cs);
			for(auto& q: removed) {
				//erase failed items
				bundleQueue.removeBundleItem(q, false);
				fileQueue.remove(q);
			}
		}

		int64_t hashSize = aBundle->getSize();

		{
			HashManager::HashPauser pauser;
			for(auto& q: hash) {
				HashedFile fi(q->getTTH(), File::getLastModified(q->getTarget()), q->getSize());
				if (SETTING(FINISHED_NO_HASH)) {
					try {
						if (HashManager::getInstance()->addFile(q->getTarget(), fi)) {
							q->setFlag(QueueItem::FLAG_HASHED);
							hashSize -= q->getSize();
							continue;
						}
					} catch(...) { 
						//hash it...
					}
				}

				try {
					// Schedule for hashing, it'll be added automatically later on...
					if (!HashManager::getInstance()->checkTTH(Text::toLower(q->getTarget()), q->getTarget(), fi)) {
						//..
					} else {
						//fine, it's there already..
						q->setFlag(QueueItem::FLAG_HASHED);
						hashSize -= q->getSize();
					}
				} catch(const Exception&) {
					//...
				}
			}
		}

		if (hashSize > 0) {
			LogManager::getInstance()->message(STRING_F(BUNDLE_ADDED_FOR_HASH, aBundle->getName() % Util::formatBytes(hashSize)), LogMessage::SEV_INFO);
		} else {
			//all files have been hashed already?
			checkBundleHashed(aBundle);
		}
	} else if (!aBundle->getQueueItems().empty() && !aBundle->getQueueItems().front()->isSet(QueueItem::FLAG_PRIVATE)) {
		//if (SETTING(ADD_FINISHED_INSTANTLY)) {
			LogManager::getInstance()->message(STRING_F(NOT_IN_SHARED_DIR, aBundle->getTarget().c_str()), LogMessage::SEV_INFO);
		//} else {
		//	LogManager::getInstance()->message(CSTRING(INSTANT_SHARING_DISABLED), LogMessage::SEV_INFO);
		//}
	} else {
		//removeFinishedBundle(aBundle);
	}
}

void QueueManager::onFileHashed(const string& aPath, HashedFile& aFileInfo, bool aFailed) noexcept {
	QueueItemPtr q = nullptr;

	{
		RLock l(cs);
		q = fileQueue.findFile(aPath);
	}

	if (!q) {
		if (!aFailed) {
			ShareManager::getInstance()->onFileHashed(aPath, aFileInfo);
		}

		return;
	}

	BundlePtr b = q->getBundle();
	if (!b)
		return;


	q->setFlag(QueueItem::FLAG_HASHED);
	if (aFailed) {
		setBundleStatus(b, Bundle::STATUS_HASH_FAILED);
	} else if (b->getStatus() != Bundle::STATUS_HASHING && b->getStatus() != Bundle::STATUS_HASH_FAILED) {
		// instant sharing disabled/the folder wasn't shared when the bundle finished
		ShareManager::getInstance()->onFileHashed(aPath, aFileInfo);
	}

	checkBundleHashed(b);
}

void QueueManager::checkBundleHashed(BundlePtr& b) noexcept {
	bool addToShare = false;

	{
		RLock l(cs);
		if (!b->getQueueItems().empty() || !all_of(b->getFinishedFiles().begin(), b->getFinishedFiles().end(), Flags::IsSet(QueueItem::FLAG_HASHED)))
			return;


		//don't fire anything if nothing has been hashed (the folder has probably been removed...)
		if (!b->getFinishedFiles().empty()) {
			if (!b->getQueueItems().empty()) {
				//new items have been added while it was being hashed
				dcassert(b->getStatus() != Bundle::STATUS_HASHING);
				return;
			}

			if (b->getStatus() == Bundle::STATUS_HASH_FAILED) {
				LogManager::getInstance()->message(STRING_F(BUNDLE_HASH_FAILED, b->getTarget().c_str()), LogMessage::SEV_ERROR);
				return;
			} else if (b->getStatus() == Bundle::STATUS_HASHING) {
				addToShare = true;
			} else {
				//instant sharing disabled/the folder wasn't shared when the bundle finished
			}
		}
	}

	if (addToShare) {
		setBundleStatus(b, Bundle::STATUS_HASHED);

		ShareManager::getInstance()->shareBundle(b);

		if (b->isFileBundle()) {
			setBundleStatus(b, Bundle::STATUS_SHARED);
		}
	}
}

void QueueManager::bundleDownloadFailed(BundlePtr& aBundle, const string& aError) {
	if (aBundle) {
		aBundle->setLastError(aError);
		setBundleStatus(aBundle, Bundle::STATUS_DOWNLOAD_FAILED);
	}
}

void QueueManager::onFileFinished(const QueueItemPtr& aQI, Download* aDownload, const string& aListDirectory) noexcept {
	auto isFilelist = aQI->isSet(QueueItem::FLAG_USER_LIST);
	auto nicks = ClientManager::getInstance()->getFormatedNicks(aDownload->getHintedUser());

	if (!isFilelist || SETTING(LOG_FILELIST_TRANSFERS)) {
		if (SETTING(SYSTEM_SHOW_DOWNLOADS)) {
			LogManager::getInstance()->message(STRING_F(FINISHED_DOWNLOAD, aQI->getTarget() % nicks), LogMessage::SEV_INFO);
		}

		if (SETTING(LOG_DOWNLOADS)) {
			ParamMap params;
			aDownload->getParams(aDownload->getUserConnection(), params);
			LOG(LogManager::DOWNLOAD, params);
		}
	}

	aQI->setLastSource(nicks);
	fire(QueueManagerListener::ItemFinished(), aQI, aListDirectory, aDownload->getHintedUser(), aDownload->getAverageSpeed());
}

void QueueManager::putDownload(Download* aDownload, bool aFinished, bool aNoAccess /*false*/, bool aRotateQueue /*false*/) throw(HashException) {
	HintedUserList getConn;
	QueueItemPtr q = nullptr;

	// Make sure the download gets killed
	unique_ptr<Download> d(aDownload);
	aDownload = nullptr;

	d->close();

	{
		RLock l(cs);
		q = fileQueue.findFile(d->getPath());
	}

	if (!q) {
		// Target has been removed, clean up the mess
		auto hasTempTarget = !d->getTempTarget().empty();
		auto isFullList = d->getType() == Transfer::TYPE_FULL_LIST;
		auto isFile = d->getType() == Transfer::TYPE_FILE && d->getTempTarget() != d->getPath();

		if (hasTempTarget && (isFullList || isFile)) {
			File::deleteFileEx(d->getTempTarget());
		}

		return;
	}

	if (q->isSet(QueueItem::FLAG_FINISHED)) {
		// Trying to finish it twice? Hmm..
		return;
	}

	if (!aFinished) {
		onDownloadFailed(q, d.get(), aNoAccess, aRotateQueue);
	} else if (q->isSet(QueueItem::FLAG_USER_LIST)) {
		onFilelistDownloadCompleted(q, d.get());
	} else {
		onFileDownloadCompleted(q, d.get());
	}
}

void QueueManager::onDownloadFailed(QueueItemPtr& aQI, Download* aDownload, bool aNoAccess, bool aRotateQueue) noexcept {
	if (aDownload->getType() == Transfer::TYPE_FULL_LIST && !aDownload->getTempTarget().empty()) {
		// No use keeping an unfinished file list...
		File::deleteFile(aDownload->getTempTarget());
	}

	if (aDownload->getType() != Transfer::TYPE_TREE && aQI->getDownloadedBytes() == 0) {
		if (aDownload->getType() == Transfer::TYPE_FILE)
			File::deleteFile(aDownload->getTempTarget());
		aQI->setTempTarget(Util::emptyString);
	}

	HintedUserList getConn;

	{
		WLock l(cs);
		if (aDownload->getType() == Transfer::TYPE_FILE) {
			// mark partially downloaded chunk, but align it to block size
			int64_t downloaded = aDownload->getPos();
			downloaded -= downloaded % aDownload->getTigerTree().getBlockSize();

			if (downloaded > 0) {
				aQI->addFinishedSegment(Segment(aDownload->getStartPos(), downloaded));
			}

			if (aRotateQueue && aQI->getBundle()) {
				aQI->getBundle()->rotateUserQueue(aQI, aDownload->getUser());
			}
		}

		if (aNoAccess) {
			aQI->blockSourceHub(aDownload->getHintedUser());
		}

		if (!aQI->isPausedPrio()) {
			aQI->getOnlineUsers(getConn);
		}

		userQueue.removeDownload(aQI, aDownload->getToken());
	}

	for (const auto& u : getConn) {
		if (u.user != aDownload->getUser())
			ConnectionManager::getInstance()->getDownloadConnection(u);
	}

	fire(QueueManagerListener::ItemStatusUpdated(), aQI);
	return;
}

void QueueManager::onFilelistDownloadCompleted(QueueItemPtr& aQI, Download* aDownload) noexcept {
	// Finished

	{
		WLock l(cs);
		aQI->addFinishedSegment(Segment(0, aQI->getSize()));
	}

	if (aDownload->isSet(Download::FLAG_XML_BZ_LIST)) {
		aQI->setFlag(QueueItem::FLAG_XML_BZLIST);
	}

	if (!aQI->isSet(QueueItem::FLAG_CLIENT_VIEW)) {
		if (aDownload->isSet(Download::FLAG_TTHLIST)) {
			matchTTHList(aDownload->getPFS(), aDownload->getHintedUser(), aQI->getFlags());
		} else {
			DirectoryListingManager::getInstance()->processList(aQI->getListName(), aDownload->getPFS(), aDownload->getHintedUser(), aDownload->getTempTarget(), aQI->getFlags());
		}

		if (aQI->isSet(QueueItem::FLAG_MATCH_BUNDLE)) {
			WLock l(cs);
			matchLists.right.erase(aQI->getTarget());
		}
	} else if (aDownload->getType() == Transfer::TYPE_PARTIAL_LIST) {
		fire(QueueManagerListener::PartialListFinished(), aDownload->getHintedUser(), aDownload->getPFS(), aQI->getTempTarget());
	} else {
		onFileFinished(aQI, aDownload, aQI->getTempTarget());  // We cheated and stored the initial display directory here (when opening lists from search)
	}

	{
		WLock l(cs);
		userQueue.removeQI(aQI);
		fileQueue.remove(aQI);
	}

	fire(QueueManagerListener::ItemRemoved(), aQI, true);
}

void QueueManager::onFileDownloadCompleted(QueueItemPtr& aQI, Download* aDownload) noexcept {
	if (aDownload->getType() == Transfer::TYPE_TREE) {
		{
			WLock l(cs);
			userQueue.removeDownload(aQI, aDownload->getToken());
		}

		dcassert(aDownload->getTreeValid());
		HashManager::getInstance()->addTree(aDownload->getTigerTree());
		fire(QueueManagerListener::ItemStatusUpdated(), aQI);
		return;
	}

	dcassert(aDownload->getType() == Transfer::TYPE_FILE);

	aDownload->setOverlapped(false);
	bool wholeFileCompleted = false;

	{
		WLock l(cs);
		aQI->addFinishedSegment(aDownload->getSegment());
		wholeFileCompleted = aQI->isFinished();

		dcdebug("Finish segment for %s (" I64_FMT ", " I64_FMT ")\n", aDownload->getToken().c_str(), aDownload->getSegment().getStart(), aDownload->getSegment().getEnd());

		if (wholeFileCompleted) {
			// Disconnect all possible overlapped downloads
			for (auto aD : aQI->getDownloads()) {
				if (compare(aD->getToken(), aDownload->getToken()) != 0)
					aD->getUserConnection().disconnect();
			}

			aQI->setTimeFinished(GET_TIME());
			aQI->setFlag(QueueItem::FLAG_FINISHED);
			userQueue.removeQI(aQI);

			if (!aQI->getBundle()) {
				fileQueue.remove(aQI);
			}
		} else {
			userQueue.removeDownload(aQI, aDownload->getToken());
		}
	}

	if (wholeFileCompleted) {
		// Remove from queued files
		if (aQI->getBundle()) {
			removeBundleItem(aQI, true);
		}

		// Check if we need to move the file
		if (!aDownload->getTempTarget().empty() && (Util::stricmp(aDownload->getPath().c_str(), aDownload->getTempTarget().c_str()) != 0)) {
			renameDownloadedFile(aDownload->getTempTarget(), aQI->getTarget(), aQI);
		}

		onFileFinished(aQI, aDownload, Util::emptyString);
	}

	if (wholeFileCompleted && !aQI->getBundle()) {
		fire(QueueManagerListener::ItemRemoved(), aQI, true);
	} else {
		fire(QueueManagerListener::ItemStatusUpdated(), aQI);
	}
}

void QueueManager::setSegments(const string& aTarget, uint8_t aSegments) noexcept {
	RLock l (cs);
	auto qi = fileQueue.findFile(aTarget);
	if (qi) {
		qi->setMaxSegments(aSegments);
	}
}

void QueueManager::matchTTHList(const string& aName, const HintedUser& aUser, int aFlags) noexcept {
	if (!(aFlags & QueueItem::FLAG_MATCH_QUEUE)) {
		return;
	}

	vector<TTHValue> tthList;
 	 
	{
		// Parse the list
		size_t start = 0;
		while (start + 39 < aName.length()) {
			tthList.emplace_back(aName.substr(start, 39));
			start = start + 40;
		}
	}
 	 
	if(tthList.empty())
		return;

	QueueItemList ql;

 	{	 
		RLock l(cs);
		for (const auto& tth: tthList) {
			fileQueue.findFiles(tth, ql);
		}
	}

	addSources(aUser, ql, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
}

void QueueManager::removeQI(QueueItemPtr& q, bool aDeleteData /*false*/) noexcept {
	StringList x;
	dcassert(q);

	// For partial-share
	UploadManager::getInstance()->abortUpload(q->getTempTarget());

	{
		WLock l(cs);
		if (q->isSet(QueueItem::FLAG_MATCH_BUNDLE)) {
			matchLists.right.erase(q->getTarget());
		}

		if(q->isRunning()) {
			for(const auto& d: q->getDownloads()) 
				x.push_back(d->getToken());
		} else if(!q->getTempTarget().empty() && q->getTempTarget() != q->getTarget()) {
			File::deleteFile(q->getTempTarget());
		}

		if(!q->isFinished()) {
			userQueue.removeQI(q);
		}

		fileQueue.remove(q);
	}

	if (aDeleteData) {
		File::deleteFile(q->getTarget());
	}

	fire(QueueManagerListener::ItemRemoved(), q, false);

	removeBundleItem(q, false);
	for (auto& token : x)
		ConnectionManager::getInstance()->disconnect(token);
}

void QueueManager::removeFileSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType aReason, bool aRemoveConn /* = true */) noexcept {
	QueueItemPtr qi = nullptr;
	{
		RLock l(cs);
		qi = fileQueue.findFile(aTarget);
	}

	if (qi) {
		removeFileSource(qi, aUser, aReason, aRemoveConn);
		fire(QueueManagerListener::SourceFilesUpdated(), aUser);
	}
}

#define MAX_SIZE_WO_TREE 20*1024*1024

void QueueManager::removeFileSource(QueueItemPtr& q, const UserPtr& aUser, Flags::MaskType aReason, bool aRemoveConn /* = true */) noexcept {
	bool isRunning = false;
	bool removeCompletely = false;
	{
		WLock l(cs);
		if(!q->isSource(aUser))
			return;

		if(q->isFinished())
			return;
	
		if(q->isSet(QueueItem::FLAG_USER_LIST)) {
			removeCompletely = true;
			goto endCheck;
		}

		if (aReason == QueueItem::Source::FLAG_NO_TREE) {
			q->getSource(aUser)->setFlag(aReason);
			if (q->getSize() < MAX_SIZE_WO_TREE) {
				return;
			}
		}

		isRunning = q->isRunning();

		userQueue.removeQI(q, aUser, false, aReason);
		q->removeSource(aUser, aReason);
	}

	fire(QueueManagerListener::ItemSourcesUpdated(), q);

	if (q->getBundle()) {
		q->getBundle()->setDirty();
		fire(QueueManagerListener::BundleSources(), q->getBundle());
	}
endCheck:
	if (isRunning && aRemoveConn) {
		DownloadManager::getInstance()->abortDownload(q->getTarget(), aUser);
	}

	if (removeCompletely) {
		removeQI(q);
	}
}

int QueueManager::removeSource(const UserPtr& aUser, Flags::MaskType aReason, std::function<bool (const QueueItemPtr&) > aExcludeF /*nullptr*/) noexcept {
	// @todo remove from finished items
	QueueItemList ql;

	{
		RLock l(cs);
		userQueue.getUserQIs(aUser, ql);

		if (aExcludeF) {
			ql.erase(remove_if(ql.begin(), ql.end(), aExcludeF), ql.end());
		}
	}

	for (auto& qi : ql) {
		removeFileSource(qi, aUser, aReason);
	}

	fire(QueueManagerListener::SourceFilesUpdated(), aUser);
	return static_cast<int>(ql.size());
}

void QueueManager::setBundlePriority(QueueToken aBundleToken, QueueItemBase::Priority p) noexcept {
	BundlePtr bundle = nullptr;
	{
		RLock l(cs);
		bundle = bundleQueue.findBundle(aBundleToken);
	}

	setBundlePriority(bundle, p, false);
}

void QueueManager::setBundlePriority(BundlePtr& aBundle, QueueItemBase::Priority p, bool aKeepAutoPrio, time_t aResumeTime/* = 0*/) noexcept {
	if (!aBundle || aBundle->getStatus() == Bundle::STATUS_RECHECK)
		return;

	if (p == QueueItem::DEFAULT) {
		if (!aBundle->getAutoPriority()) {
			toggleBundleAutoPriority(aBundle);
		}
		
		return;
	}

	QueueItemBase::Priority oldPrio = aBundle->getPriority();
	if (oldPrio == p) {
		if (aBundle->getResumeTime() != aResumeTime) {
			aBundle->setResumeTime(aResumeTime);
			fire(QueueManagerListener::BundlePriority(), aBundle);
		}
		return;
	}

	QueueItemPtr qi = nullptr;
	{
		WLock l(cs);

		if (aBundle->isFinished())
			return;

		bundleQueue.removeSearchPrio(aBundle);
		userQueue.setBundlePriority(aBundle, p);
		bundleQueue.addSearchPrio(aBundle);
		bundleQueue.recalculateSearchTimes(aBundle->isRecent(), true, GET_TICK());
		if (!aKeepAutoPrio) {
			aBundle->setAutoPriority(false);
		}

		aBundle->setResumeTime(aResumeTime);

		if (aBundle->isFileBundle()) {
			qi = aBundle->getQueueItems().front();
			userQueue.setQIPriority(qi, p);
			qi->setAutoPriority(aBundle->getAutoPriority());
		}
	}

	if (qi) {
		fire(QueueManagerListener::ItemStatusUpdated(), qi);
	}

	fire(QueueManagerListener::BundlePriority(), aBundle);

	aBundle->setDirty();

	if(p == QueueItemBase::PAUSED_FORCE) {
		DownloadManager::getInstance()->disconnectBundle(aBundle);
	} else if (oldPrio <= QueueItemBase::LOWEST) {
		connectBundleSources(aBundle);
	}

	dcassert(!aBundle->isFileBundle() || aBundle->getPriority() == aBundle->getQueueItems().front()->getPriority());
}

void QueueManager::toggleBundleAutoPriority(QueueToken aBundleToken) noexcept {
	BundlePtr bundle = nullptr;
	{
		RLock l(cs);
		bundle = bundleQueue.findBundle(aBundleToken);
	}

	toggleBundleAutoPriority(bundle);
}

void QueueManager::toggleBundleAutoPriority(BundlePtr& aBundle) noexcept {
	if (aBundle->isFinished())
		return;

	aBundle->setAutoPriority(!aBundle->getAutoPriority());
	if (aBundle->isFileBundle()) {
		RLock l(cs);
		aBundle->getQueueItems().front()->setAutoPriority(aBundle->getAutoPriority());
	}

	if (aBundle->isPausedPrio()) {
		// We don't want this one to stay paused if the auto priorities can't be counted
		setBundlePriority(aBundle, Bundle::LOW, true);
	} else {
		// Auto priority state may not be fired if the old priority is kept
		fire(QueueManagerListener::BundlePriority(), aBundle);
	}

	// Recount priorities as soon as possible
	setLastAutoPrio(0);

	aBundle->setDirty();
}

int QueueManager::removeFinishedBundles() noexcept {
	BundleList bundles;
	{
		RLock l(cs);
		boost::algorithm::copy_if(bundleQueue.getBundles() | map_values, back_inserter(bundles), [](const BundlePtr& aBundle) {
			return aBundle->isFinished() && !aBundle->isFailed();
		});
	}

	for (auto& bundle : bundles) {
		removeBundle(bundle, false);
	}

	return static_cast<int>(bundles.size());
}

void QueueManager::setPriority(QueueItemBase::Priority p) noexcept {
	Bundle::TokenMap bundles;
	{
		RLock l(cs);
		bundles = bundleQueue.getBundles();
	}

	for (auto& bundle : bundles | map_values) {
		setBundlePriority(bundle, p);
	}
}

void QueueManager::setQIPriority(const string& aTarget, QueueItemBase::Priority p) noexcept {
	QueueItemPtr q = nullptr;
	{
		RLock l(cs);
		q = fileQueue.findFile(aTarget);
	}

	setQIPriority(q, p);
}

void QueueManager::setQIPriority(QueueItemPtr& q, QueueItemBase::Priority p, bool aKeepAutoPrio /*false*/) noexcept {
	HintedUserList getConn;
	bool running = false;
	if (!q || !q->getBundle()) {
		//items without a bundle should always use the highest prio
		return;
	}

	if (p == QueueItem::DEFAULT) {
		if (!q->getAutoPriority()) {
			setQIAutoPriority(q->getTarget());
		}
		
		return;
	}

	BundlePtr b = q->getBundle();
	if (b->isFileBundle()) {
		dcassert(!aKeepAutoPrio);
		setBundlePriority(b, p, false);
		return;
	}

	{
		WLock l(cs);
		if(q->getPriority() != p && !q->isFinished() ) {
			if((q->isPausedPrio() && !b->isPausedPrio()) || (p == QueueItem::HIGHEST && b->getPriority() != QueueItemBase::PAUSED)) {
				// Problem, we have to request connections to all these users...
				q->getOnlineUsers(getConn);
			}

			running = q->isRunning();

			if (!aKeepAutoPrio)
				q->setAutoPriority(false);

			userQueue.setQIPriority(q, p);
		}
	}

	fire(QueueManagerListener::ItemStatusUpdated(), q);

	b->setDirty();
	if(p == QueueItem::PAUSED && running) {
		DownloadManager::getInstance()->abortDownload(q->getTarget());
	} else if (p != QueueItemBase::PAUSED) {
		for(auto& u: getConn)
			ConnectionManager::getInstance()->getDownloadConnection(u);
	}

	dcassert(!b->isFileBundle() || b->getPriority() == q->getPriority());
}

void QueueManager::setQIAutoPriority(const string& aTarget) noexcept {
	QueueItemPtr q = nullptr;

	{
		RLock l(cs);
		q = fileQueue.findFile(aTarget);
	}

	if (!q)
		return;
	if (!q->getBundle())
		return;

	if (q->getBundle()->isFileBundle()) {
		toggleBundleAutoPriority(q->getBundle()->getToken());
		return;
	}

	q->setAutoPriority(!q->getAutoPriority());
	fire(QueueManagerListener::ItemStatusUpdated(), q);

	q->getBundle()->setDirty();

	if(q->getAutoPriority()) {
		if (SETTING(AUTOPRIO_TYPE) == SettingsManager::PRIO_PROGRESS) {
			setQIPriority(q, q->calculateAutoPriority());
		} else if (q->isPausedPrio()) {
			setQIPriority(q, QueueItem::LOW);
		}
	}
}
void QueueManager::setFileListSize(const string& aPath, int64_t aNewSize) noexcept {
	WLock l(cs);
	auto q = fileQueue.findFile(aPath);
	if (q)
		q->setSize(aNewSize);
}


void QueueManager::handleSlowDisconnect(const UserPtr& aUser, const string& aTarget, const BundlePtr& aBundle) noexcept {
	switch (SETTING(DL_AUTO_DISCONNECT_MODE)) {
		case SettingsManager::QUEUE_FILE: removeFileSource(aTarget, aUser, QueueItem::Source::FLAG_SLOW_SOURCE); break;
		case SettingsManager::QUEUE_BUNDLE: removeBundleSource(aBundle, aUser, QueueItem::Source::FLAG_SLOW_SOURCE); break;
		case SettingsManager::QUEUE_ALL: removeSource(aUser, QueueItem::Source::FLAG_SLOW_SOURCE, [](const QueueItemPtr& aQI) { return aQI->getSources().size() <= 1; }); break;
	}
}

void QueueManager::removeBundleSource(QueueToken aBundleToken, const UserPtr& aUser, Flags::MaskType aReason) noexcept {
	BundlePtr bundle = nullptr;
	{
		RLock l(cs);
		bundle = bundleQueue.findBundle(aBundleToken);
	}
	removeBundleSource(bundle, aUser, aReason);
}

void QueueManager::removeBundleSource(BundlePtr aBundle, const UserPtr& aUser, Flags::MaskType aReason) noexcept {
	if (!aBundle) {
		return;
	}

	QueueItemList ql;

	{
		RLock l(cs);
		aBundle->getItems(aUser, ql);

		//we don't want notifications from this user anymore
		auto p = boost::find_if(aBundle->getFinishedNotifications(), [&aUser](const Bundle::UserBundlePair& ubp) { return ubp.first.user == aUser; });
		if (p != aBundle->getFinishedNotifications().end()) {
			sendRemovePBD(p->first, p->second);
		}
	}

	for(auto& qi: ql) {
		removeFileSource(qi, aUser, aReason);
	}

	fire(QueueManagerListener::SourceFilesUpdated(), aUser);
}

void QueueManager::sendRemovePBD(const HintedUser& aUser, const string& aRemoteToken) noexcept {
	AdcCommand cmd(AdcCommand::CMD_PBD, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", aUser.hint);
	cmd.addParam("BU", aRemoteToken);
	cmd.addParam("RM1");
	ClientManager::getInstance()->sendUDP(cmd, aUser.user->getCID(), false, true);
}

void QueueManager::saveQueue(bool aForce) noexcept {
	RLock l(cs);	
	bundleQueue.saveQueue(aForce);
}

class QueueLoader : public SimpleXMLReader::CallBack {
public:
	QueueLoader() : curFile(nullptr), inDownloads(false), inBundle(false), inFile(false), qm(QueueManager::getInstance()), version(0) { }
	~QueueLoader() { }
	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name);
	void createFile(QueueItemPtr& aQI, bool aAddedByAutoSearch);
	QueueItemBase::Priority validatePrio(const string& aPrio);
	void resetBundle() {
		curFile = nullptr;
		curBundle = nullptr;
		inFile = false;
		inBundle = false;
		inDownloads = false;
		curToken = 0;
		currentFileTarget.clear();
	}
private:
	string currentFileTarget;

	QueueItemPtr curFile;
	BundlePtr curBundle;
	bool inDownloads;
	bool inBundle;
	bool inFile;
	QueueToken curToken = 0;
	time_t bundleDate = 0;
	time_t resumeTime = 0;
	bool addedByAutosearch = false;

	int version;
	QueueManager* qm;
};

void QueueManager::loadQueue(function<void (float)> progressF) noexcept {
	setMatchers();

	// migrate old bundles
	Util::migrate(Util::getPath(Util::PATH_BUNDLES), "Bundle*");

	// multithreaded loading
	StringList fileList = File::findFiles(Util::getPath(Util::PATH_BUNDLES), "Bundle*", File::TYPE_FILE);
	atomic<long> loaded(0);
	try {
		parallel_for_each(fileList.begin(), fileList.end(), [&](const string& path) {
			if (Util::getFileExt(path) == ".xml") {
				QueueLoader loader;
				try {
					File f(path, File::READ, File::OPEN, File::BUFFER_SEQUENTIAL, false);
					SimpleXMLReader(&loader).parse(f);
				} catch (const Exception& e) {
					LogManager::getInstance()->message(STRING_F(BUNDLE_LOAD_FAILED, path % e.getError().c_str()), LogMessage::SEV_ERROR);
					File::deleteFile(path);
				}
			}
			loaded++;
			progressF(static_cast<float>(loaded) / static_cast<float>(fileList.size()));
		});
	} catch (std::exception& e) {
		LogManager::getInstance()->message("Loading the queue failed: " + string(e.what()), LogMessage::SEV_INFO);
	}

	try {
		//load the old queue file and delete it
		auto path = Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml";
		Util::migrate(path);

		{
			File f(path, File::READ, File::OPEN, File::BUFFER_SEQUENTIAL);
			QueueLoader loader;
			SimpleXMLReader(&loader).parse(f);
		}

		File::copyFile(Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml", Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml.bak");
		File::deleteFile(Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml");
	} catch(const Exception&) {
		// ...
	}

	TimerManager::getInstance()->addListener(this); 
	SearchManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);
	ShareManager::getInstance()->addListener(this);
}

static const string sFile = "File";
static const string sBundle = "Bundle";
static const string sName = "Name";
static const string sToken = "Token";
static const string sDownload = "Download";
static const string sTempTarget = "TempTarget";
static const string sTarget = "Target";
static const string sSize = "Size";
static const string sDownloaded = "Downloaded";
static const string sPriority = "Priority";
static const string sSource = "Source";
static const string sNick = "Nick";
static const string sDirectory = "Directory";
static const string sAdded = "Added";
static const string sDate = "Date";
static const string sTTH = "TTH";
static const string sCID = "CID";
static const string sHubHint = "HubHint";
static const string sRemotePath = "RemotePath";
static const string sSegment = "Segment";
static const string sStart = "Start";
static const string sAutoPriority = "AutoPriority";
static const string sMaxSegments = "MaxSegments";
static const string sBundleToken = "BundleToken";
static const string sFinished = "Finished";
static const string sVersion = "Version";
static const string sTimeFinished = "TimeFinished";
static const string sLastSource = "LastSource";
static const string sAddedByAutoSearch = "AddedByAutoSearch";
static const string sResumeTime = "ResumeTime";

QueueItemBase::Priority QueueLoader::validatePrio(const string& aPrio) {
	int prio = Util::toInt(aPrio);
	if (version == 1)
		prio++;

	if (prio > QueueItemBase::HIGHEST)
		return QueueItemBase::HIGHEST;
	if (prio < QueueItemBase::PAUSED_FORCE)
		return QueueItemBase::PAUSED_FORCE;

	return static_cast<QueueItemBase::Priority>(prio);
}

void QueueLoader::createFile(QueueItemPtr& aQI, bool aAddedByAutosearch) {
	if (ConnectionManager::getInstance()->tokens.addToken(Util::toString(curToken), CONNECTION_TYPE_DOWNLOAD)) {
		curBundle = new Bundle(aQI, bundleDate, curToken, false);
		curBundle->setTimeFinished(aQI->getTimeFinished());
		curBundle->setAddedByAutoSearch(aAddedByAutosearch);
		curBundle->setResumeTime(resumeTime);
	} else {
		qm->fileQueue.remove(aQI);
		throw Exception("Duplicate token");
	}
}

void QueueLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	if(!inDownloads && name == "Downloads") {
		inDownloads = true;
	} else if (!inFile && name == sFile) {
		curToken = Util::toUInt32(getAttrib(attribs, sToken, 1));
		bundleDate = Util::toInt64(getAttrib(attribs, sDate, 2));
		addedByAutosearch = Util::toBool(Util::toInt(getAttrib(attribs, sAddedByAutoSearch, 3)));
		resumeTime = static_cast<time_t>(Util::toInt64(getAttrib(attribs, sResumeTime, 4)));
		inFile = true;
		version = Util::toInt(getAttrib(attribs, sVersion, 0));
		if (version == 0 || version > Util::toInt(FILE_BUNDLE_VERSION))
			throw Exception("Non-supported file bundle version");
	} else if (!inBundle && name == sBundle) {
		version = Util::toInt(getAttrib(attribs, sVersion, 0));
		if (version == 0 || version > Util::toInt(DIR_BUNDLE_VERSION))
			throw Exception("Non-supported directory bundle version");

		const string& bundleTarget = getAttrib(attribs, sTarget, 1);
		const string& token = getAttrib(attribs, sToken, 2);
		if(token.empty())
			throw Exception("Missing bundle token");

		time_t added = static_cast<time_t>(Util::toInt64(getAttrib(attribs, sAdded, 2)));
		time_t dirDate = static_cast<time_t>(Util::toInt64(getAttrib(attribs, sDate, 3)));
		bool b_autoSearch = Util::toBool(Util::toInt(getAttrib(attribs, sAddedByAutoSearch, 4)));
		const string& prio = getAttrib(attribs, sPriority, 4);
		if(added == 0) {
			added = GET_TIME();
		}

		time_t b_resumeTime = static_cast<time_t>(Util::toInt64(getAttrib(attribs, sResumeTime, 5)));

		if (ConnectionManager::getInstance()->tokens.addToken(token, CONNECTION_TYPE_DOWNLOAD)) {
			curBundle = new Bundle(bundleTarget, added, !prio.empty() ? validatePrio(prio) : Bundle::DEFAULT, dirDate, Util::toUInt32(token), false);
			time_t finished = static_cast<time_t>(Util::toInt64(getAttrib(attribs, sTimeFinished, 5)));
			if (finished > 0) {
				curBundle->setTimeFinished(finished);
			}
			curBundle->setAddedByAutoSearch(b_autoSearch);
			curBundle->setResumeTime(b_resumeTime);

		} else {
			throw Exception("Duplicate bundle token");
		}

		inBundle = true;		
	} else if(inDownloads || inBundle || inFile) {
		if(!curFile && name == sDownload) {
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			if(size == 0)
				return;

			try {
				const string& tgt = getAttrib(attribs, sTarget, 0);
				// @todo do something better about existing files
				currentFileTarget = QueueManager::checkTarget(tgt);
				if(currentFileTarget.empty())
					return;
			} catch(const Exception&) {
				return;
			}

			if (curBundle && inBundle && !AirUtil::isParentOrExactLocal(curBundle->getTarget(), currentFileTarget)) {
				//the file isn't inside the main bundle dir, can't add this
				return;
			}

			time_t added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 2)));
			const string& tthRoot = getAttrib(attribs, sTTH, 3);
			if (tthRoot.empty())
				return;

			QueueItemBase::Priority p = validatePrio(getAttrib(attribs, sPriority, 4));

			string tempTarget = getAttrib(attribs, sTempTarget, 5);
			uint8_t maxSegments = (uint8_t)Util::toInt(getAttrib(attribs, sMaxSegments, 5));

			if(added == 0)
				added = GET_TIME();

			if (Util::toInt(getAttrib(attribs, sAutoPriority, 6)) == 1) {
				p = QueueItem::DEFAULT;
			}

			WLock l (qm->cs);
			auto ret = qm->fileQueue.add(currentFileTarget, size, 0, p, tempTarget, added, TTHValue(tthRoot));
			if(ret.second) {
				auto qi = ret.first;
				qi->setMaxSegments(max((uint8_t)1, maxSegments));

				//bundles
				if (curBundle && inBundle) {
					//LogManager::getInstance()->message("itemtoken exists: " + bundleToken);
					qm->bundleQueue.addBundleItem(qi, curBundle);
				} else if (inDownloads) {
					//assign bundles for the items in the old queue file
					curBundle = new Bundle(qi, 0);
				} else if (inFile && curToken != 0) {
					createFile(qi, addedByAutosearch);
				}
			}
			if(!simple)
				curFile = ret.first;
		} else if(curFile && name == sSegment) {
			if(!curFile)
				return;

			int64_t start = Util::toInt64(getAttrib(attribs, sStart, 0));
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			
			if(size > 0 && start >= 0 && (start + size) <= curFile->getSize()) {
				curFile->addFinishedSegment(Segment(start, size));
				if (curFile->getAutoPriority() && SETTING(AUTOPRIO_TYPE) == SettingsManager::PRIO_PROGRESS) {
					curFile->setPriority(curFile->calculateAutoPriority());
				}
			} else {
				dcdebug("Invalid segment: " I64_FMT " " I64_FMT "\n", start, size);
			}
		} else if(curFile && name == sSource) {
			const string& cid = getAttrib(attribs, sCID, 0);
			if(cid.length() != 39) {
				// Skip loading this source - sorry old users
				return;
			}
			ClientManager* cm = ClientManager::getInstance();
			UserPtr user = cm->getUser(CID(cid));

			try {
				const string& hubHint = getAttrib(attribs, sHubHint, 1);
				HintedUser hintedUser(user, hubHint);
				{
					WLock l(cm->getCS());
					cm->addOfflineUser(user, getAttrib(attribs, sNick, 1), hubHint);
					qm->addSource(curFile, hintedUser, 0, false) && user->isOnline();
				}
			} catch(const Exception&) {
				return;
			}
		} else if (name == sFinished && (inBundle || inFile)) {
			//LogManager::getInstance()->message("FOUND FINISHED TTH");
			const string& target = getAttrib(attribs, sTarget, 0);
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			time_t added = static_cast<time_t>(Util::toInt64(getAttrib(attribs, sAdded, 2)));
			const string& tth = getAttrib(attribs, sTTH, 3);
			time_t finished = static_cast<time_t>(Util::toInt64(getAttrib(attribs, sTimeFinished, 4)));
			const string& lastsource = getAttrib(attribs, sLastSource, 5);

			if(size == 0 || tth.empty() || target.empty() || added == 0)
				return;
			if(!Util::fileExists(target))
				return;

			WLock l(qm->cs);
			auto ret = qm->fileQueue.add(target, size, QueueItem::FLAG_FINISHED | QueueItem::FLAG_MOVED, QueueItemBase::DEFAULT, Util::emptyString, added, TTHValue(tth));
			if (!ret.second) {
				return;
			}

			auto& qi = ret.first;
			qi->addFinishedSegment(Segment(0, size)); //make it complete
			qi->setTimeFinished(finished);
			qi->setLastSource(lastsource);

			if (curBundle && inBundle) {
				//LogManager::getInstance()->message("itemtoken exists: " + bundleToken);
				qm->bundleQueue.addBundleItem(qi, curBundle);
			} else if (inFile && curToken != 0) {
				createFile(qi, addedByAutosearch);
			}
		} else {
			//LogManager::getInstance()->message("QUEUE LOADING ERROR");
		}
	}
}

void QueueLoader::endTag(const string& name) {
	
	if(inDownloads || inBundle || inFile) {
		if(name == "Downloads") {
			inDownloads = false;
		} else if(name == sBundle) {
			ScopedFunctor([this] { curBundle = nullptr; });
			inBundle = false;
			if (curBundle->getQueueItems().empty() && curBundle->getFinishedFiles().empty()) {
				throw Exception(STRING_F(NO_FILES_WERE_LOADED, curBundle->getTarget()));
			} else {
				qm->addLoadedBundle(curBundle);
			}
		} else if(name == sFile) {
			curToken = 0;
			inFile = false;
			if (!curBundle || (curBundle->isEmpty()))
				throw Exception(STRING(NO_FILES_FROM_FILE));

			qm->addLoadedBundle(curBundle);
		} else if(name == sDownload) {
			if (inDownloads && curBundle && curBundle->isFileBundle()) {
				/* Only for file bundles and when migrating an old queue */
				qm->addLoadedBundle(curBundle);
			}
			curFile = nullptr;
		}
	}
}

string QueueManager::getBundlePath(QueueToken aBundleToken) const noexcept{
	auto b = bundleQueue.findBundle(aBundleToken);
	return b ? b->getTarget() : "Unknown";
}

void QueueManager::noDeleteFileList(const string& path) noexcept {
	if(!SETTING(KEEP_LISTS)) {
		protectedFileLists.push_back(path);
	}
}

// SearchManagerListener
void QueueManager::on(SearchManagerListener::SR, const SearchResultPtr& sr) noexcept {
	QueueItemPtr selQI = nullptr;

	{
		QueueItemList matches;

		RLock l(cs);
		fileQueue.findFiles(sr->getTTH(), matches);

		for(const auto& q: matches) {
			if (!q->getBundle())
				continue;

			// Size compare to avoid popular spoof
			if((SETTING(AUTO_ADD_SOURCE) || (q->getBundle()->getLastSearch() != 0 && static_cast<uint64_t>(q->getBundle()->getLastSearch() + 15*60*1000) > GET_TICK())) && q->getSize() == sr->getSize() && !q->isSource(sr->getUser())) {
				if (q->getBundle()->isFinished()) {
					continue;
				}

				if(q->isFinished() && q->getBundle()->isSource(sr->getUser())) {
					continue;
				}

				if(static_cast<int>(q->getBundle()->countOnlineUsers() + matchLists.left.count(q->getBundle()->getToken())) < SETTING(MAX_AUTO_MATCH_SOURCES)) {
					selQI = q;
				} 
			}
			break;
		}
	}

	if (selQI) {
		{
			WLock l(cs);
			auto& rl = searchResults[selQI->getTarget()];
			if (find_if(rl, [&sr](const SearchResultPtr& aSR) { return aSR->getUser() == sr->getUser() && aSR->getPath() == sr->getPath(); }) != rl.end()) {
				//don't add the same result multiple times, makes the counting more reliable
				return;
			}
			rl.push_back(sr);
		}
		delayEvents.addEvent(selQI->getToken(), [=] { pickMatch(selQI); }, 2000);
	}
}

void QueueManager::pickMatch(QueueItemPtr qi) noexcept {
	SearchResultList results;
	int addNum = 0;

	//get the result list
	{
		WLock l(cs);
		auto p = searchResults.find(qi->getTarget());
		if (p != searchResults.end()) {
			results.swap(p->second);
			searchResults.erase(p);
		}

		addNum = SETTING(MAX_AUTO_MATCH_SOURCES) - (qi->getBundle()->countOnlineUsers() + matchLists.left.count(qi->getBundle()->getToken()));
	}

	if (addNum <= 0)
		return;

	SearchResult::pickResults(results, addNum);
	for(const auto& sr: results) {
		matchBundle(qi, sr);
	}
}

void QueueManager::matchBundle(QueueItemPtr& aQI, const SearchResultPtr& aResult) noexcept {
	if (aQI->getBundle()->isFileBundle()) {
		// No reason to match anything with file bundles
		addSources(aResult->getUser(), { aQI }, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
		return;
	} 

	auto isNmdc = aResult->getUser().user->isNMDC();

	auto path = AirUtil::getMatchPath(aResult->getPath(), aQI->getTarget(), aQI->getBundle()->getTarget(), isNmdc);
	if (!path.empty()) {
		if (isNmdc) {
			// A NMDC directory bundle, just add the sources without matching
			QueueItemList ql;

			{
				RLock l(cs);
				aQI->getBundle()->getDirQIs(path, ql);
			}

			auto newFiles = addSources(aResult->getUser(), ql, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);

			if (SETTING(REPORT_ADDED_SOURCES) && newFiles > 0) {
				LogManager::getInstance()->message(ClientManager::getInstance()->getFormatedNicks(aResult->getUser()) + ": " + 
					STRING_F(MATCH_SOURCE_ADDED, newFiles % aQI->getBundle()->getName().c_str()), LogMessage::SEV_INFO);
			}
		} else {
			//An ADC directory bundle, match recursive partial list
			try {
				addList(aResult->getUser(), QueueItem::FLAG_MATCH_QUEUE | QueueItem::FLAG_RECURSIVE_LIST |(path.empty() ? 0 : QueueItem::FLAG_PARTIAL_LIST), path, aQI->getBundle());
			} catch(...) { }
		}
	} else if (SETTING(ALLOW_MATCH_FULL_LIST)) {
		// No path to match, use full filelist
		dcassert(isNmdc);
		try {
			addList(aResult->getUser(), QueueItem::FLAG_MATCH_QUEUE, Util::emptyString, aQI->getBundle());
		} catch(const Exception&) {
			// ...
		}
	}
}

// ClientManagerListener
void QueueManager::on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool /*wasOffline*/) noexcept {
	bool hasDown = false;

	{
		QueueItemList ql;
		BundleList bl;
		{
			RLock l(cs);
			userQueue.getUserQIs(aUser.getUser(), ql);
			auto i = userQueue.getBundleList().find(aUser.getUser());
			if (i != userQueue.getBundleList().end())
				bl = i->second;
		}

		for(const auto& q: ql) {
			fire(QueueManagerListener::ItemSourcesUpdated(), q);
			if(!hasDown && !q->isPausedPrio() && !q->isHubBlocked(aUser.getUser(), aUser.getHubUrl()))
				hasDown = true;
		}

		for (const auto& b : bl) 
			fire(QueueManagerListener::BundleSources(), b);
	}

	if(hasDown) { 
		ConnectionManager::getInstance()->getDownloadConnection(HintedUser(aUser.getUser(), aUser.getHubUrl()));
	}
}

void QueueManager::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept {
	if (!wentOffline)
		return;

	QueueItemList ql;
	BundleList bl;
	{
		RLock l(cs);
		userQueue.getUserQIs(aUser, ql);
		auto i = userQueue.getBundleList().find(aUser);
		if (i != userQueue.getBundleList().end())
			bl = i->second;
	}

	for (const auto& q: ql)
		fire(QueueManagerListener::ItemSourcesUpdated(), q); 

	for (const auto& b : bl)
		fire(QueueManagerListener::BundleSources(), b);
}

void QueueManager::calculatePriorities(uint64_t aTick) noexcept {
	auto prioType = SETTING(AUTOPRIO_TYPE);
	if (prioType == SettingsManager::PRIO_DISABLED) {
		return;
	}

	if (lastAutoPrio != 0 && lastAutoPrio + (SETTING(AUTOPRIO_INTERVAL) * 1000) > aTick) {
		return;
	}

	vector<pair<QueueItemPtr, QueueItemBase::Priority>> qiPriorities;
	vector<pair<BundlePtr, QueueItemBase::Priority>> bundlePriorities;

	{
		RLock l(cs);

		// bundles
		for (const auto& b : bundleQueue.getBundles() | map_values) {
			if (b->isFinished()) {
				continue;
			}

			if (prioType == SettingsManager::PRIO_PROGRESS && b->getAutoPriority()) {
				auto p2 = b->calculateProgressPriority();
				if (b->getPriority() != p2) {
					bundlePriorities.emplace_back(b, p2);
				}
			}
		}

		// queueitems
		for (const auto& q : fileQueue.getPathQueue() | map_values) {
			if (!q->isRunning())
				continue;

			if (SETTING(QI_AUTOPRIO) && prioType == SettingsManager::PRIO_PROGRESS && q->getAutoPriority() && q->getBundle() && !q->getBundle()->isFileBundle()) {
				auto p1 = q->getPriority();
				if (p1 != QueueItemBase::PAUSED && p1 != QueueItemBase::PAUSED_FORCE) {
					auto p2 = q->calculateAutoPriority();
					if (p1 != p2)
						qiPriorities.emplace_back(q, p2);
				}
			}
		}
	}

	if (prioType == SettingsManager::PRIO_BALANCED) {
		//LogManager::getInstance()->message("Calculate autoprio (balanced)");
		calculateBundlePriorities(false);
		setLastAutoPrio(aTick);
	} else {
		//LogManager::getInstance()->message("Calculate autoprio (progress)");
		for (auto& bp : bundlePriorities)
			setBundlePriority(bp.first, bp.second, true);

		for (auto& qp : qiPriorities)
			setQIPriority(qp.first, qp.second);
	}

	lastAutoPrio = aTick;
}

void QueueManager::checkResumeBundles() noexcept {
	BundleList resumeBundles;

	{
		RLock l(cs);
		for (const auto& b : bundleQueue.getBundles() | map_values) {
			if (b->isFinished()) {
				continue;
			}

			if (b->getResumeTime() > 0 && GET_TIME() > b->getResumeTime()) {
				resumeBundles.push_back(b);
			}
		}
	}

	for (auto& b : resumeBundles) {
		setBundlePriority(b, QueueItemBase::DEFAULT, false);
	}
}

void QueueManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	tasks.addTask([=] {
		if ((lastXmlSave + 10000) < aTick) {
			saveQueue(false);
			lastXmlSave = aTick;
		}

		QueueItemList runningItems;

		{
			RLock l(cs);
			for (const auto& q : fileQueue.getPathQueue() | map_values) {
				if (!q->isRunning())
					continue;

				runningItems.push_back(q);
			}
		}

		for (const auto& q : runningItems) {
			fire(QueueManagerListener::ItemStatusUpdated(), q);
		}

		calculatePriorities(aTick);
	});
}

void QueueManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	tasks.addTask([=] {
		requestPartialSourceInfo(aTick);
		searchAlternates(aTick);
		checkResumeBundles();
	});
}

template<class T>
static void calculateBalancedPriorities(vector<pair<T, QueueItemBase::Priority>>& priorities, multimap<T, pair<int64_t, double>>& speedSourceMap, bool verbose) noexcept {
	if (speedSourceMap.empty())
		return;

	//scale the priorization maps
	double factorSpeed=0, factorSource=0;
	double maxSpeed = static_cast<double>(max_element(speedSourceMap.begin(), speedSourceMap.end())->second.first);
	if (maxSpeed > 0) {
		factorSpeed = 100 / maxSpeed;
	}

	auto maxSources = max_element(speedSourceMap.begin(), speedSourceMap.end())->second.second;
	if (maxSources > 0) {
		factorSource = 100 / maxSources;
	}

	multimap<double, T> finalMap;
	int uniqueValues = 0;
	for (auto& i: speedSourceMap) {
		auto points = (i.second.first * factorSpeed) + (i.second.second * factorSource);
		if (finalMap.find(points) == finalMap.end()) {
			uniqueValues++;
		}
		finalMap.emplace(points, i.first);
	}

	int prioGroup = 1;
	if (uniqueValues <= 1) {
		if (verbose) {
			LogManager::getInstance()->message("Not enough items with unique points to perform the priotization!", LogMessage::SEV_INFO);
		}
		return;
	} else if (uniqueValues > 2) {
		prioGroup = uniqueValues / 3;
	}

	if (verbose) {
		LogManager::getInstance()->message("Unique values: " + Util::toString(uniqueValues) + " prioGroup size: " + Util::toString(prioGroup), LogMessage::SEV_INFO);
	}


	//start with the high prio, continue to normal and low
	int8_t prio = QueueItemBase::HIGH;

	//counters for analyzing identical points
	double lastPoints = 999.0;
	int prioSet=0;

	for (auto& i: finalMap) {
		if (lastPoints==i.first) {
			if (verbose) {
				LogManager::getInstance()->message(i.second->getTarget() + " points: " + Util::toString(i.first) + " setting prio " + AirUtil::getPrioText(prio), LogMessage::SEV_INFO);
			}

			if(i.second->getPriority() != prio)
				priorities.emplace_back(i.second, static_cast<QueueItemBase::Priority>(prio));

			//don't increase the prio if two items have identical points
			if (prioSet < prioGroup) {
				prioSet++;
			}
		} else {
			//all priorities set from this group? but don't go below LOW
			if (prioSet == prioGroup && prio != QueueItemBase::LOW) {
				prio--;
				prioSet=0;
			} 

			if (verbose) {
				LogManager::getInstance()->message(i.second->getTarget() + " points: " + Util::toString(i.first) + " setting prio " + AirUtil::getPrioText(prio), LogMessage::SEV_INFO);
			}

			if(i.second->getPriority() != prio)
				priorities.emplace_back(i.second, static_cast<QueueItemBase::Priority>(prio));

			prioSet++;
			lastPoints = i.first;
		}
	}
}

void QueueManager::calculateBundlePriorities(bool verbose) noexcept {
	multimap<BundlePtr, pair<int64_t, double>> bundleSpeedSourceMap;

	/* Speed and source maps for files in each bundle */
	vector<multimap<QueueItemPtr, pair<int64_t, double>>> qiMaps;

	{
		RLock l (cs);
		for (auto& b: bundleQueue.getBundles() | map_values) {
			if (!b->isFinished()) {
				if (b->getAutoPriority()) {
					bundleSpeedSourceMap.emplace(b, b->getPrioInfo());
				}

				if (SETTING(QI_AUTOPRIO)) {
					qiMaps.push_back(b->getQIBalanceMaps());
				}
			}
		}
	}

	vector<pair<BundlePtr, QueueItemBase::Priority>> bundlePriorities;
	calculateBalancedPriorities<BundlePtr>(bundlePriorities, bundleSpeedSourceMap, verbose);

	for(auto& p: bundlePriorities) {
		setBundlePriority(p.first, p.second, true);
	}


	if (SETTING(QI_AUTOPRIO)) {

		vector<pair<QueueItemPtr, QueueItemBase::Priority>> qiPriorities;
		for(auto& s: qiMaps) {
			calculateBalancedPriorities<QueueItemPtr>(qiPriorities, s, verbose);
		}

		for(auto& p: qiPriorities) {
			setQIPriority(p.first, p.second, true);
		}
	}
}

bool QueueManager::checkDropSlowSource(Download* d) noexcept {
	BundlePtr b = d->getBundle();
	size_t onlineUsers = 0;

	int iHighSpeed = SETTING(DISCONNECT_FILE_SPEED);
	{
		RLock l (cs);
		onlineUsers = b->countOnlineUsers();
	}

	if((iHighSpeed == 0 || b->getSpeed() > Util::convertSize(iHighSpeed, Util::KB)) && onlineUsers >= 2) {
		d->setFlag(Download::FLAG_SLOWUSER);

		if(d->getAverageSpeed() < Util::convertSize(SETTING(REMOVE_SPEED), Util::KB)) {
			return true;
		} else {
			d->getUserConnection().disconnect();
		}
	}

	return false;
}

bool QueueManager::handlePartialResult(const HintedUser& aUser, const TTHValue& tth, const QueueItem::PartialSource& partialSource, PartsInfo& outPartialInfo) noexcept {
	bool wantConnection = false;
	dcassert(outPartialInfo.empty());
	QueueItemPtr qi = nullptr;

	// Locate target QueueItem in download queue
	{
		QueueItemList ql;

		RLock l(cs);
		fileQueue.findFiles(tth, ql);
		
		if(ql.empty()){
			dcdebug("Not found in download queue\n");
			return false;
		}
		
		qi = ql.front();

		// don't add sources to finished files
		// this could happen when "Keep finished files in queue" is enabled
		if(qi->isFinished())
			return false;
	}

	// Check min size
	if(qi->getSize() < PARTIAL_SHARE_MIN_SIZE){
		dcassert(0);
		return false;
	}

	// Get my parts info
	int64_t blockSize = qi->getBlockSize();

	{
		WLock l(cs);
		qi->getPartialInfo(outPartialInfo, blockSize);
		
		// Any parts for me?
		wantConnection = qi->isNeededPart(partialSource.getPartialInfo(), blockSize);

		// If this user isn't a source and has no parts needed, ignore it
		auto si = qi->getSource(aUser);
		if(si == qi->getSources().end()){
			si = qi->getBadSource(aUser);

			if(si != qi->getBadSources().end() && si->isSet(QueueItem::Source::FLAG_TTH_INCONSISTENCY))
				return false;

			if(!wantConnection) {
				if(si == qi->getBadSources().end())
					return false;
			} else {
				// add this user as partial file sharing source
				qi->addSource(aUser);
				si = qi->getSource(aUser);
				si->setFlag(QueueItem::Source::FLAG_PARTIAL);

				auto ps = new QueueItem::PartialSource(partialSource.getMyNick(),
					partialSource.getHubIpPort(), partialSource.getIp(), partialSource.getUdpPort());
				si->setPartialSource(ps);

				userQueue.addQI(qi, aUser);
				dcassert(si != qi->getSources().end());
			}
		}

		// Update source's parts info
		if(si->getPartialSource()) {
			si->getPartialSource()->setPartialInfo(partialSource.getPartialInfo());
		}
	}
	
	// Connect to this user
	if (wantConnection) {
		fire(QueueManagerListener::ItemSourcesUpdated(), qi);

		if (aUser.user->isOnline()) {
			ConnectionManager::getInstance()->getDownloadConnection(aUser);
		}
	}

	return true;
}

BundlePtr QueueManager::findBundle(const TTHValue& tth) const noexcept {
	QueueItemList ql;
	{
		RLock l(cs);
		fileQueue.findFiles(tth, ql);
	}

	if (!ql.empty()) {
		return ql.front()->getBundle();
	}
	return nullptr;
}

bool QueueManager::handlePartialSearch(const UserPtr& aUser, const TTHValue& tth, PartsInfo& _outPartsInfo, string& _bundle, bool& _reply, bool& _add) noexcept {
	QueueItemPtr qi = nullptr;
	{
		QueueItemList ql;

		RLock l(cs);
		// Locate target QueueItem in download queue
		fileQueue.findFiles(tth, ql);
		if (ql.empty()) {
			return false;
		}

		qi = ql.front();

		//don't share files download from private chat
		if (qi->isSet(QueueItem::FLAG_PRIVATE))
			return false;

		BundlePtr b = qi->getBundle();
		if (b) {
			_bundle = b->getStringToken();

			//should we notify the other user about finished item?
			_reply = !b->isFinished() && !b->isFinishedNotified(aUser);

			//do we have finished files that the other guy could download?
			_add = !b->getFinishedFiles().empty();
		}

		// do we have a file to send?
		if (!qi->hasPartialSharingTarget())
			return false;
	}

	if(qi->getSize() < PARTIAL_SHARE_MIN_SIZE){
		return false;  
	}


	int64_t blockSize = qi->getBlockSize();

	RLock l(cs);
	qi->getPartialInfo(_outPartsInfo, blockSize);

	return !_outPartsInfo.empty();
}

StringList QueueManager::getNmdcDirPaths(const string& aDirName) const noexcept {
	RLock l(cs);
	return bundleQueue.getNmdcDirPaths(aDirName);
}

void QueueManager::getBundlePaths(OrderedStringSet& retBundles) const noexcept {
	RLock l(cs);
	for (const auto& b : bundleQueue.getBundles() | map_values) {
		retBundles.insert(b->getTarget());
	}
}

void QueueManager::checkRefreshPaths(OrderedStringSet& retBundles_, RefreshPathList& refreshPaths_) noexcept {
	BundleList hash;
	{
		RLock l(cs);
		for (auto& b: bundleQueue.getBundles() | map_values) {
			if (b->isFileBundle() || (b->getStatus() >= Bundle::STATUS_HASHING && b->getStatus() != Bundle::STATUS_HASH_FAILED))
				continue;

			//check the path just to avoid hashing/scanning bundles from dirs that aren't being refreshed

			{
				// Find parent refresh directories of this bundle path
				auto refreshPathIter = find_if(refreshPaths_.begin(), refreshPaths_.end(), IsParentOrExact(b->getTarget(), PATH_SEPARATOR));

				if (refreshPathIter == refreshPaths_.end()) {
					continue;
				}

				// No point to refresh exact bundle paths
				if (Util::stricmp(*refreshPathIter, b->getTarget()) == 0) {
					refreshPaths_.erase(*refreshPathIter);
				}
			}

			if(b->isFinished() && (b->isFailed() || b->allowHash())) {
				hash.push_back(b);
			}

			retBundles_.insert(Text::toLower(b->getTarget()));
		}
	}

	for (auto& b: hash) {
		if(b->isFailed() && !scanBundle(b)) {
			continue;
		}

		hashBundle(b); 
	}
}

void QueueManager::shareBundle(BundlePtr aBundle, bool aSkipScan) noexcept{
	if (!aSkipScan && !scanBundle(aBundle)) {
		return;
	}

	setBundleStatus(aBundle, Bundle::STATUS_FINISHED);
	hashBundle(aBundle);
}

void QueueManager::on(ShareManagerListener::DirectoriesRefreshed, uint8_t aType, const RefreshPathList& aPaths) noexcept {
	if (aType == ShareManager::REFRESH_ALL) {
		onPathRefreshed(Util::emptyString, false);
	} else {
		for (const auto& p : aPaths) {
			onPathRefreshed(p, false);
		}
	}
}

void QueueManager::onPathRefreshed(const string& aPath, bool aStartup) noexcept{
	BundleList bundles;

	{
		RLock l(cs);
		for (auto& b : bundleQueue.getBundles() | map_values) {
			if (AirUtil::isParentOrExactLocal(aPath, b->getTarget()) && (b->getStatus() == Bundle::STATUS_FINISHED || b->getStatus() == Bundle::STATUS_HASHED)) {
				bundles.push_back(b);
			}
		}
	}

	for (auto& b : bundles) {
		if (ShareManager::getInstance()->isRealPathShared(b->getTarget())) {
			setBundleStatus(b, Bundle::STATUS_SHARED);
		} else if (aStartup) {
			// In case it's a failed bundle
			shareBundle(b, false);
		}
	}
}

void QueueManager::on(ShareManagerListener::ShareLoaded) noexcept {
	tasks.addTask([=] { onPathRefreshed(Util::emptyString, true); });
}

void QueueManager::setBundleStatus(BundlePtr aBundle, Bundle::Status newStatus) noexcept {
	if (aBundle->getStatus() != newStatus) {
		aBundle->setStatus(newStatus);
		fire(QueueManagerListener::BundleStatusChanged(), aBundle);
	}
}

bool QueueManager::isChunkDownloaded(const TTHValue& tth, int64_t startPos, int64_t& bytes, int64_t& fileSize_, string& target) noexcept {
	QueueItemList ql;

	RLock l(cs);
	fileQueue.findFiles(tth, ql);

	if(ql.empty()) return false;


	QueueItemPtr qi = ql.front();
	if (!qi->hasPartialSharingTarget())
		return false;

	fileSize_ = qi->getSize();
	target = qi->isFinished() ? qi->getTarget() : qi->getTempTarget();

	return qi->isChunkDownloaded(startPos, bytes);
}

void QueueManager::getSourceInfo(const UserPtr& aUser, Bundle::SourceBundleList& aSources, Bundle::SourceBundleList& aBad) const noexcept {
	RLock l(cs);
	bundleQueue.getSourceInfo(aUser, aSources, aBad);
}

int QueueManager::addSources(const HintedUser& aUser, QueueItemList items, Flags::MaskType aAddBad) noexcept {
	BundleList bundles;
	return addSources(aUser, items, aAddBad, bundles);
}

int QueueManager::addSources(const HintedUser& aUser, QueueItemList items, Flags::MaskType aAddBad, BundleList& matchingBundles_) noexcept {
	if (items.empty()) {
		return 0;
	}

	//int newFiles = 0;
	bool wantConnection = false;

	{
		// Add the source

		WLock l(cs);
		items.erase(boost::remove_if(items, [&](QueueItemPtr& q) {
			if (q->getBundle() && find(matchingBundles_, q->getBundle()) == matchingBundles_.end()) {
				matchingBundles_.push_back(q->getBundle());
			}

			try {
				if (addSource(q, aUser, aAddBad)) {
					wantConnection = true;
				}

				return false;
			} catch (...) {
				// Ignore...
			}

			return true;
		}), items.end());
	}

	if (items.empty()) {
		return 0;
	}

	{
		Bundle::Set updatedBundles;

		// Speakers
		for (const auto& qi : items) {
			fire(QueueManagerListener::ItemSourcesUpdated(), qi);

			if (qi->getBundle() && updatedBundles.insert(qi->getBundle()).second) {
				fire(QueueManagerListener::BundleSources(), qi->getBundle());
			}
		}

		fire(QueueManagerListener::SourceFilesUpdated(), aUser);
	}

	{
		// Connect
		if (wantConnection && aUser.user->isOnline()) {
			ConnectionManager::getInstance()->getDownloadConnection(aUser);
		}
	}

	return items.size();
}

void QueueManager::connectBundleSources(BundlePtr& aBundle) noexcept {
	if (aBundle->isPausedPrio())
		return;

	HintedUserList x;
	{
		RLock l(cs);
		aBundle->getSourceUsers(x);
	}

	for(const auto& u: x) { 
		if (u.user && u.user->isOnline()) {
			ConnectionManager::getInstance()->getDownloadConnection(u);
		}
	}
}

void QueueManager::readdBundle(BundlePtr& aBundle) noexcept {
	aBundle->setStatus(Bundle::STATUS_QUEUED);

	//check that the finished files still exist
	auto files = aBundle->getFinishedFiles();
	for(auto& qi: files) {
		if (!Util::fileExists(qi->getTarget())) {
			bundleQueue.removeBundleItem(qi, false);
			fileQueue.remove(qi);
		}
	}

	aBundle->setTimeFinished(0);
	bundleQueue.addSearchPrio(aBundle);

	aBundle->setDirty();
	LogManager::getInstance()->message(STRING_F(BUNDLE_READDED, aBundle->getName().c_str()), LogMessage::SEV_INFO);
}

DupeType QueueManager::isNmdcDirQueued(const string& aDir, int64_t aSize) const noexcept{
	RLock l(cs);
	return bundleQueue.isNmdcDirQueued(aDir, aSize);
}

BundlePtr QueueManager::findDirectoryBundle(const string& aPath) const noexcept {
	RLock l(cs);
	return bundleQueue.findBundle(aPath);
}

int QueueManager::getUnfinishedItemCount(const BundlePtr& aBundle) const noexcept {
	RLock l(cs); 
	return aBundle->getQueueItems().size(); 
}

int QueueManager::getFinishedItemCount(const BundlePtr& aBundle) const noexcept { 
	RLock l(cs); 
	return (int)aBundle->getFinishedFiles().size(); 
}

void QueueManager::addBundleUpdate(const BundlePtr& aBundle) noexcept{
	/*
	Add as Task to fix Deadlock!!
	handleBundleUpdate(..) has a Lock and this function is called inside a Lock, while delayEvents has its own locking for add/execute functions.
	*/
	tasks.addTask([=] { 
		delayEvents.addEvent(aBundle->getToken(), [=] { 
			handleBundleUpdate(aBundle->getToken()); 
		}, aBundle->isSet(Bundle::FLAG_SCHEDULE_SEARCH) ? 10000 : 1000); 
	});
}

void QueueManager::handleBundleUpdate(QueueToken aBundleToken) noexcept {
	//LogManager::getInstance()->message("QueueManager::sendBundleUpdate");
	BundlePtr b = nullptr;
	{
		RLock l(cs);
		b = bundleQueue.findBundle(aBundleToken);
	}

	if (b) {
		if (b->isSet(Bundle::FLAG_UPDATE_SIZE)) {
			if (b->isSet(Bundle::FLAG_UPDATE_SIZE)) {
				fire(QueueManagerListener::BundleSize(), b);
			} 

			DownloadManager::getInstance()->sendSizeUpdate(b);
		}
		
		if (b->isSet(Bundle::FLAG_SCHEDULE_SEARCH)) {
			searchBundleAlternates(b, false);
		}
	}
}
void QueueManager::removeBundleItem(QueueItemPtr& qi, bool aFinished) noexcept{
	BundlePtr bundle = qi->getBundle();
	if (!bundle) {
		return;
	}

	UserList sources;
	bool emptyBundle = false;

	{
		WLock l(cs);
		bundleQueue.removeBundleItem(qi, aFinished);
		if (aFinished) {
			fileQueue.decreaseSize(qi->getSize());
			if (bundle->getQueueItems().empty()) {
				bundleQueue.removeSearchPrio(bundle);
				emptyBundle = true;
			}
		} else {
			emptyBundle = bundle->isEmpty();
		}

		// update the sources
		for (auto& aSource : bundle->getSources())
			sources.push_back(aSource.getUser().user);
	}

	if (emptyBundle) {
		if (!aFinished) {
			removeBundle(bundle, false);
			return;
		} else {
			bundle->finishBundle();
			setBundleStatus(bundle, Bundle::STATUS_DOWNLOADED);
			removeBundleLists(bundle);
		}
	} else if (!aFinished && !checkBundleFinished(bundle)) {
		bundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
		addBundleUpdate(bundle);
	}

	for (auto& u : sources)
		fire(QueueManagerListener::SourceFilesUpdated(), u);

	fire(QueueManagerListener::BundleSources(), bundle);
	bundle->setDirty();
}

bool QueueManager::removeBundle(QueueToken aBundleToken, bool removeFinishedFiles) noexcept {
	BundlePtr b = nullptr;
	{
		RLock l(cs);
		b = bundleQueue.findBundle(aBundleToken);
	}

	if (b) {
		removeBundle(b, removeFinishedFiles);
		return true;
	}

	return false;
}

void QueueManager::removeBundle(BundlePtr& aBundle, bool aRemoveFinishedFiles) noexcept{
	if (aBundle->getStatus() == Bundle::STATUS_NEW) {
		return;
	}

	UserList sources;
	StringList deleteFiles;

	DownloadManager::getInstance()->disconnectBundle(aBundle);
	fire(QueueManagerListener::BundleRemoved(), aBundle);

	bool isFinished = false;

	{
		WLock l(cs);
		isFinished = aBundle->isFinished();

		for (auto& aSource : aBundle->getSources())
			sources.push_back(aSource.getUser().user);

		auto finishedItems = aBundle->getFinishedFiles();
		for (auto& qi : finishedItems) {
			fileQueue.remove(qi);
			bundleQueue.removeBundleItem(qi, false);
			if (aRemoveFinishedFiles) {
				UploadManager::getInstance()->abortUpload(qi->getTarget());
				deleteFiles.push_back(qi->getTarget());
			}
		}

		auto queueItems = aBundle->getQueueItems();
		for (auto& qi : queueItems) {
			UploadManager::getInstance()->abortUpload(qi->getTarget());

			if (!qi->isRunning() && !qi->getTempTarget().empty() && qi->getTempTarget() != qi->getTarget()) {
				deleteFiles.push_back(qi->getTempTarget());
			}

			if (!qi->isFinished()) {
				userQueue.removeQI(qi);
			}

			fileQueue.remove(qi);
			bundleQueue.removeBundleItem(qi, false);
		}

		bundleQueue.removeBundle(aBundle);
	}

	//Delete files outside lock range, waking up disks can take a long time.
	for_each(deleteFiles.begin(), deleteFiles.end(), &File::deleteFile);

	// An empty directory should be deleted even if finished files are not being deleted (directories are created even for temp files)
	// Avoid disk access when cleaning up finished bundles
	if (!isFinished && !aBundle->isFileBundle()) {
		AirUtil::removeDirectoryIfEmpty(aBundle->getTarget(), 10, !aRemoveFinishedFiles);
	}

	if (!isFinished) {
		LogManager::getInstance()->message(STRING_F(BUNDLE_X_REMOVED, aBundle->getName()), LogMessage::SEV_INFO);
	}

	for (const auto& aUser : sources)
		fire(QueueManagerListener::SourceFilesUpdated(), aUser);

	removeBundleLists(aBundle);
}

void QueueManager::removeBundleLists(BundlePtr& aBundle) noexcept{
	QueueItemList removed;
	{
		RLock l(cs);
		//erase all lists related to this bundle
		auto dl = matchLists.left.equal_range(aBundle->getToken());
		for (auto p = dl.first; p != dl.second; p++) {
			auto q = fileQueue.findFile(p->get_right());
			if (q)
				removed.push_back(q);
		}
	}

	for (auto& qi : removed)
		removeQI(qi);
}

MemoryInputStream* QueueManager::generateTTHList(QueueToken aBundleToken, bool isInSharingHub, BundlePtr& bundle_) throw(QueueException) {
	if(!isInSharingHub)
		throw QueueException(UserConnection::FILE_NOT_AVAILABLE);

	string tths;
	StringOutputStream tthList(tths);
	{
		RLock l(cs);
		bundle_ = bundleQueue.findBundle(aBundleToken);
		if (bundle_) {
			//write finished items
			string tmp2;
			for(auto& q: bundle_->getFinishedFiles()) {
				if (q->isSet(QueueItem::FLAG_MOVED)) {
					tmp2.clear();
					tthList.write(q->getTTH().toBase32(tmp2) + " ");
				}
			}
		}
	}

	if (tths.size() == 0) {
		throw QueueException(UserConnection::FILE_NOT_AVAILABLE);
	} else {
		return new MemoryInputStream(tths);
	}
}

void QueueManager::addBundleTTHList(const HintedUser& aUser, const string& aRemoteBundleToken, const TTHValue& tth) throw(QueueException) {
	//LogManager::getInstance()->message("ADD TTHLIST");
	auto b = findBundle(tth);
	if (b) {
		addList(aUser, QueueItem::FLAG_TTHLIST_BUNDLE | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_MATCH_QUEUE, aRemoteBundleToken, b);
	}
}

bool QueueManager::checkPBDReply(HintedUser& aUser, const TTHValue& aTTH, string& _bundleToken, bool& _notify, bool& _add, const string& remoteBundle) noexcept {
	BundlePtr bundle = findBundle(aTTH);
	if (bundle) {
		WLock l(cs);
		//LogManager::getInstance()->message("checkPBDReply: BUNDLE FOUND");
		_bundleToken = bundle->getStringToken();
		_add = !bundle->getFinishedFiles().empty();

		if (!bundle->isFinished()) {
			bundle->addFinishedNotify(aUser, remoteBundle);
			_notify = true;
		}
		return true;
	}
	//LogManager::getInstance()->message("checkPBDReply: CHECKNOTIFY FAIL");
	return false;
}

void QueueManager::addFinishedNotify(HintedUser& aUser, const TTHValue& aTTH, const string& remoteBundle) noexcept {
	BundlePtr bundle = findBundle(aTTH);
	if (bundle) {
		WLock l(cs);
		//LogManager::getInstance()->message("addFinishedNotify: BUNDLE FOUND");
		if (!bundle->isFinished()) {
			bundle->addFinishedNotify(aUser, remoteBundle);
		}
	}
	//LogManager::getInstance()->message("addFinishedNotify: BUNDLE NOT FOUND");
}

void QueueManager::removeBundleNotify(const UserPtr& aUser, QueueToken aBundleToken) noexcept {
	WLock l(cs);
	BundlePtr bundle = bundleQueue.findBundle(aBundleToken);
	if (bundle) {
		bundle->removeFinishedNotify(aUser);
	}
}

void QueueManager::updatePBD(const HintedUser& aUser, const TTHValue& aTTH) noexcept {
	QueueItemList qiList;

	{
		RLock l(cs);
		fileQueue.findFiles(aTTH, qiList);
	}

	addSources(aUser, qiList, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
}

void QueueManager::searchBundleAlternates(BundlePtr& aBundle, bool aIsManualSearch, uint64_t aTick) noexcept {
	QueueItemList searchItems;
	int64_t nextSearch = 0;

	// Get the possible items to search for
	{
		RLock l(cs);
		bool isScheduled = aBundle->isSet(Bundle::FLAG_SCHEDULE_SEARCH);

		aBundle->unsetFlag(Bundle::FLAG_SCHEDULE_SEARCH);
		if (!aIsManualSearch)
			nextSearch = (bundleQueue.recalculateSearchTimes(aBundle->isRecent(), false, aTick) - aTick) / (60*1000);

		if (isScheduled && !aBundle->allowAutoSearch())
			return;

		searchItems = bundleQueue.getSearchItems(aBundle);
	}

	if (searchItems.empty()) {
		return;
	}

	for (const auto& q : searchItems) {
		q->searchAlternates();
	}

	aBundle->setLastSearch(aTick);

	// Report
	if (aIsManualSearch) {
		LogManager::getInstance()->message(STRING_F(BUNDLE_ALT_SEARCH, aBundle->getName().c_str() % searchItems.size()), LogMessage::SEV_INFO);
	} else if(SETTING(REPORT_ALTERNATES)) {
		if (aBundle->isRecent()) {
			LogManager::getInstance()->message(STRING_F(BUNDLE_ALT_SEARCH_RECENT, aBundle->getName() % searchItems.size()) +
				" " + STRING_F(NEXT_RECENT_SEARCH_IN, nextSearch), LogMessage::SEV_INFO);
		} else {
			LogManager::getInstance()->message(STRING_F(BUNDLE_ALT_SEARCH, aBundle->getName() % searchItems.size()) +
				" " + STRING_F(NEXT_SEARCH_IN, nextSearch), LogMessage::SEV_INFO);
		}
	}
}

void QueueManager::onUseSeqOrder(BundlePtr& b) noexcept {
	if (!b)
		return;

	WLock l (cs);
	b->setSeqOrder(!b->getSeqOrder());
	auto ql = b->getQueueItems();
	for (auto& q: ql) {
		if (!q->isPausedPrio()) {
			userQueue.removeQI(q, false);
			userQueue.addQI(q);
		}
	}
}

} // namespace dcpp
