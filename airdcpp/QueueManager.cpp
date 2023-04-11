/* 
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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
#include "DCPlusPlus.h"
#include "DebugManager.h"
#include "DirectoryListing.h"
#include "DirectoryListingManager.h"
#include "Download.h"
#include "DownloadManager.h"
#include "ErrorCollector.h"
#include "FileReader.h"
#include "HashManager.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "ScopedFunctor.h"
#include "SearchManager.h"
#include "SearchResult.h"
#include "SFVReader.h"
#include "ShareManager.h"
#include "SimpleXMLReader.h"
#include "Transfer.h"
#include "UploadManager.h"
#include "UserConnection.h"
#include "ZUtils.h"
#include "version.h"

#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/count_if.hpp>

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
	udp(make_unique<Socket>(Socket::TYPE_UDP)),
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
	ShareManager::getInstance()->removeListener(this);

	if (SETTING(REMOVE_FINISHED_BUNDLES)){
		WLock l(cs);
		BundleList bl;
		for (auto& b : bundleQueue.getBundles() | map_values) {
			if (b->isCompleted()) {
				bl.push_back(b);
			}
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

	{
		RLock l(cs);
		b = bundleQueue.findBundle(aBundleToken);
		if (!b) {
			return;
		}

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

	log(STRING_F(INTEGRITY_CHECK_START_BUNDLE, b->getName() %
		Util::formatBytes(finishedSegmentsBegin)), LogMessage::SEV_INFO);
	

	// prepare for checking
	auto oldPrio = b->getPriority();
	auto oldStatus = b->getStatus();

	setBundlePriority(b, Priority::PAUSED_FORCE);
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
	log(STRING_F(INTEGRITY_CHECK_FINISHED_BUNDLE, b->getName() %
		Util::formatBytes(failedBytes)), LogMessage::SEV_INFO);

	b->setStatus(oldStatus);
	handleFailedRecheckItems(failedItems);
	setBundlePriority(b, oldPrio);
}

void QueueManager::recheckFiles(QueueItemList aQL) noexcept {
	log(STRING_F(INTEGRITY_CHECK_START_FILES, aQL.size()), LogMessage::SEV_INFO);

	QueueItemList failedItems;
	int64_t failedBytes = 0;
	for (auto& q : aQL) {
		bool running;

		{
			RLock l(cs);
			running = q->isRunning();
		}

		auto oldPrio = q->getPriority();
		setQIPriority(q, Priority::PAUSED_FORCE);
		if (running) {
			Thread::sleep(1000);
		}

		if (recheckFileImpl(q->getTarget(), false, failedBytes)) {
			failedItems.push_back(q);
		}

		setQIPriority(q, oldPrio);
	}

	handleFailedRecheckItems(failedItems);
	log(STRING_F(INTEGRITY_CHECK_FINISHED_FILES, Util::formatBytes(failedBytes)), LogMessage::SEV_INFO);
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

			q->setStatus(QueueItem::STATUS_QUEUED);

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
		log(STRING_F(INTEGRITY_CHECK, aError % q->getTarget()), LogMessage::SEV_ERROR);
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
		FileReader(FileReader::ASYNC).read(checkTarget, [&](const void* x, size_t n) {
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
	bool segmentsDone = false;

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

		segmentsDone = q->segmentsDone();
	}

	if (failedBytes > 0) {
		failedBytes_ += failedBytes;
		log(STRING_F(INTEGRITY_CHECK,
			STRING_F(FILE_CORRUPTION_FOUND, Util::formatBytes(failedBytes)) % q->getTarget()),
			LogMessage::SEV_WARNING);
	} else if (fileCRC && ttFile.getRoot() == tth && *fileCRC != crc32.getValue()) {
		log(q->getTarget() + ": " + STRING(ERROR_HASHING_CRC32), LogMessage::SEV_ERROR);
	}

	if (ttFile.getRoot() == tth && !q->isDownloaded()) {
		q->setTimeFinished(GET_TIME());
		setFileStatus(q, QueueItem::STATUS_DOWNLOADED);

		{
			WLock l(cs);
			userQueue.removeQI(q);
		}

		removeBundleItem(q, true);

		//If no bad blocks then the file probably got stuck in the temp folder for some reason
		if (checkTarget != q->getTarget()) {
			renameDownloadedFile(q->getTempTarget(), q->getTarget(), q);
		} else {
			setFileStatus(q, QueueItem::STATUS_COMPLETED);
		}

		//failFile(STRING(FILE_ALREADY_FINISHED));
		return false;
	}

	// we will also resume files that are added in the destination directory from other sources
	if (!segmentsDone && (q->isDownloaded() || q->getTarget() == checkTarget)) {
		try {
			File::renameFile(q->getTarget(), q->getTempTarget());
		} catch (const FileException& e) {
			log(STRING_F(UNABLE_TO_RENAME, q->getTarget() % e.getError()), LogMessage::SEV_ERROR);
		}
	}

	if (q->isDownloaded() && !segmentsDone) {
		return true;
	}

	fire(QueueManagerListener::FileRecheckDone(), q->getTarget());
	fire(QueueManagerListener::ItemStatus(), q);
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

bool QueueManager::getSearchInfo(const string& aTarget, TTHValue& tth_, int64_t& size_) noexcept {
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
			udp->writeTo(param->ip, param->udpPort, cmd.toString(ClientManager::getInstance()->getMyCID()));
		} catch (...) {
			dcdebug("Partial search caught error\n");
		}

		delete param;
	}
}

DirectoryContentInfo QueueManager::getBundleContent(const BundlePtr& aBundle) const noexcept {
	RLock l(cs);
	auto files = aBundle->getQueueItems().size() + aBundle->getFinishedFiles().size();
	auto directories = aBundle->isFileBundle() ? 0 : bundleQueue.getDirectoryCount(aBundle) - 1;
	return DirectoryContentInfo(directories, files);
}

bool QueueManager::hasDownloadedBytes(const string& aTarget) {
	RLock l(cs);
	auto q = fileQueue.findFile(aTarget);
	if (!q) {
		throw QueueException(STRING(TARGET_REMOVED));
	}

	return q->getDownloadedBytes() > 0;
}

QueueItemPtr QueueManager::addListHooked(const FilelistAddData& aListData, Flags::MaskType aFlags, const BundlePtr& aBundle /*nullptr*/) {
	if (!(aFlags & QueueItem::FLAG_TTHLIST_BUNDLE) && !Util::isAdcDirectoryPath(aListData.listPath)) {
		throw QueueException(STRING_F(INVALID_PATH, aListData.listPath));
	}

	// Pre-checks
	checkSourceHooked(aListData.user, aListData.caller);

	// Format the target
	auto target = getListPath(aListData.user);
	if((aFlags & QueueItem::FLAG_PARTIAL_LIST)) {
		target += ".partial[" + Util::validateFileName(aListData.listPath) + "]";
	}


	// Add in queue

	QueueItemPtr q = nullptr;
	{
		WLock l(cs);
		auto ret = fileQueue.add(target, -1, (Flags::MaskType)(QueueItem::FLAG_USER_LIST | aFlags), Priority::HIGHEST, aListData.listPath, GET_TIME(), TTHValue());
		if (!ret.second) {
			//exists already
			throw DupeException(STRING(LIST_ALREADY_QUEUED));
		}

		q = std::move(ret.first);
		addValidatedSource(q, aListData.user, QueueItem::Source::FLAG_MASK);
		if (aBundle) {
			matchLists.insert(TokenStringMultiBiMap::value_type(aBundle->getToken(), q->getTarget()));
		}
	}

	fire(QueueManagerListener::ItemAdded(), q);

	//connect
	if (aListData.user.user->isOnline()) {
		ConnectionManager::getInstance()->getDownloadConnection(aListData.user, (aFlags & QueueItem::FLAG_PARTIAL_LIST) || (aFlags & QueueItem::FLAG_TTHLIST_BUNDLE));
	}

	return q;
}

string QueueManager::getListPath(const HintedUser& user) const noexcept {
	StringList nicks = ClientManager::getInstance()->getNicks(user);
	string nick = nicks.empty() ? Util::emptyString : Util::validateFileName(nicks[0]) + ".";
	return Util::getListPath() + nick + user.user->getCID().toBase32();
}

bool QueueManager::checkRemovedTarget(const QueueItemPtr& q, int64_t aSize, const TTHValue& aTTH) {
	if (q->isDownloaded()) {
		/* The target file doesn't exist, add our item. Also recheck the existance in case of finished files being moved on the same time. */
		dcassert(q->getBundle());
		if (!Util::fileExists(q->getTarget()) && q->getBundle() && q->isCompleted()) {
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

void QueueManager::checkSourceHooked(const HintedUser& aUser, const void* aCaller) const {
	if (!aUser.user) { //atleast magnet links can cause this to happen.
		throw QueueException(STRING(UNKNOWN_USER));
	}

	if (aUser.hint.empty()) {
		dcassert(0);
		throw QueueException(ClientManager::getInstance()->getFormatedNicks(aUser) + ": " + STRING(HUB_UNKNOWN));
	}

	// Check that we're not downloading from ourselves...
	if (aUser.user == ClientManager::getInstance()->getMe()) {
		throw QueueException(STRING(NO_DOWNLOADS_FROM_SELF));
	}

	// Check the encryption
	if (aUser.user->isOnline() && !aUser.user->isNMDC() && !aUser.user->isSet(User::TLS) && SETTING(TLS_MODE) == SettingsManager::TLS_FORCED) {
		throw QueueException(ClientManager::getInstance()->getFormatedNicks(aUser) + ": " + STRING(SOURCE_NO_ENCRYPTION));
	}

	{
		auto error = sourceValidationHook.runHooksError(aCaller, aUser);
		if (error) {
			throw QueueException(ActionHookRejection::formatError(error));
		}
	}
}

void QueueManager::validateBundleFileHooked(const string& aBundleDir, BundleFileAddData& fileInfo_, const void* aCaller, Flags::MaskType aFlags/*=0*/) const {
	if (fileInfo_.size <= 0) {
		throw QueueException(STRING(ZERO_BYTE_QUEUE));
	}

	auto matchSkipList = [&] (string&& aName) -> void {
		if(skipList.match(aName)) {
			throw QueueException(STRING(SKIPLIST_DOWNLOAD_MATCH));
		}
	};

	// Check the skiplist
	// No skiplist for private (magnet) downloads
	if (!(aFlags & QueueItem::FLAG_PRIVATE)) {
		// Match the file name
		matchSkipList(Util::getFileName(fileInfo_.name));

		// Match all subdirectories (if any)
		string::size_type i = 0, j = 0;
		while ((i = fileInfo_.name.find(PATH_SEPARATOR, j)) != string::npos) {
			matchSkipList(fileInfo_.name.substr(j, i - j));
			j = i + 1;
		}
	}

	// Validate the target and check the existance
	fileInfo_.name = checkTarget(fileInfo_.name, aBundleDir);

	// Check share dupes
	if (SETTING(DONT_DL_ALREADY_SHARED) && ShareManager::getInstance()->isFileShared(fileInfo_.tth)) {
		auto paths = ShareManager::getInstance()->getRealPaths(fileInfo_.tth);
		if (!paths.empty()) {
			auto path = AirUtil::subtractCommonDirectories(aBundleDir, Util::getFilePath(paths.front()));
			throw DupeException(STRING_F(TTH_ALREADY_SHARED, path));
		}
	}

	// Check queue dupes
	if (SETTING(DONT_DL_ALREADY_QUEUED)) {
		RLock l(cs);
		auto q = fileQueue.getQueuedFile(fileInfo_.tth);
		if (q && q->getTarget() != aBundleDir + fileInfo_.name) {
			auto path = AirUtil::subtractCommonDirectories(aBundleDir, q->getFilePath());
			throw DupeException(STRING_F(FILE_ALREADY_QUEUED, path));
		}
	}

	try {
		auto data = bundleFileValidationHook.runHooksDataThrow(aCaller, aBundleDir, fileInfo_);
		for (const auto& bundleAddData: data) {
			if (bundleAddData->data.priority != Priority::DEFAULT) {
				fileInfo_.prio = bundleAddData->data.priority;
			}
		}
	} catch (const HookRejectException& e) {
		throw QueueException(ActionHookRejection::formatError(e.getRejection()));
	}

	// Valid file

	// Priority
	if (fileInfo_.prio == Priority::DEFAULT && highPrioFiles.match(Util::getFileName(fileInfo_.name))) {
		fileInfo_.prio = SETTING(PRIO_LIST_HIGHEST) ? Priority::HIGHEST : Priority::HIGH;
	}
}

QueueItemPtr QueueManager::addOpenedItemHooked(const ViewedFileAddData& aFileInfo, bool aIsClientView) {
	dcassert(aFileInfo.user);

	// Check source
	checkSourceHooked(aFileInfo.user, aFileInfo.caller);

	// Check size
	if (aFileInfo.size == 0) {
		// Can't view this...
		throw QueueException(STRING(CANT_OPEN_EMPTY_FILE));
	} else if (aIsClientView && aFileInfo.isText && aFileInfo.size > Util::convertSize(1, Util::MB)) {
		auto msg = STRING_F(VIEWED_FILE_TOO_BIG, aFileInfo.file % Util::formatBytes(aFileInfo.size));
		log(msg, LogMessage::SEV_ERROR);
		throw QueueException(msg);
	}

	// Check target
	auto target = Util::getOpenPath() + AirUtil::toOpenFileName(aFileInfo.file, aFileInfo.tth);

	// Add in queue
	QueueItemPtr qi = nullptr;
	bool wantConnection = false, added = false;

	Flags::MaskType flags;
	if (aIsClientView) {
		flags = QueueItem::FLAG_CLIENT_VIEW;
	} else {
		flags = QueueItem::FLAG_OPEN;
	}

	{
		WLock l(cs);
		auto ret = fileQueue.add(target, aFileInfo.size, flags, Priority::HIGHEST, Util::emptyString, GET_TIME(), aFileInfo.tth);

		qi = std::move(ret.first);
		added = ret.second;

		wantConnection = addValidatedSource(qi, aFileInfo.user, QueueItem::Source::FLAG_MASK);
	}

	if (added) {
		fire(QueueManagerListener::ItemAdded(), qi);
	}

	// Connect
	if(wantConnection || qi->usesSmallSlot()) {
		ConnectionManager::getInstance()->getDownloadConnection(aFileInfo.user, qi->usesSmallSlot());
	}

	return qi;
}

BundlePtr QueueManager::getBundle(const string& aTarget, Priority aPrio, time_t aDate, bool isFileBundle) noexcept {
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

void QueueManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(SETTINGS_QUEUE));
}

optional<DirectoryBundleAddResult> QueueManager::createDirectoryBundleHooked(const BundleAddOptions& aOptions, BundleAddData& aDirectory, BundleFileAddData::List& aFiles, string& errorMsg_) noexcept {
	string target = aOptions.target;

	// Bundle validation
	try {
		runAddBundleHooksThrow(target, aDirectory, aOptions.optionalUser, false);
	} catch (const HookRejectException& e) {
		errorMsg_ = ActionHookRejection::formatError(e.getRejection());
		return nullopt;
	}

	// Generic validations
	target = Util::joinDirectory(formatBundleTarget(target, aDirectory.date), Util::validatePath(aDirectory.name));

	{
		// There can't be existing bundles inside this directory
		BundleList subBundles;
		bundleQueue.getSubBundles(target, subBundles);
		if (!subBundles.empty()) {
			StringList subPaths;
			for (const auto& b : subBundles) {
				subPaths.push_back(b->getTarget());
			}

			errorMsg_ = STRING_F(BUNDLE_ERROR_SUBBUNDLES, subBundles.size() % target % AirUtil::subtractCommonParents(target, subPaths));
			return nullopt;
		}
	}

	if (aFiles.empty()) {
		errorMsg_ = STRING(DIR_EMPTY);
		return nullopt;
	}

	// Source
	if (aOptions.optionalUser) {
		try {
			checkSourceHooked(aOptions.optionalUser, aOptions.caller);
		} catch (const QueueException& e) {
			errorMsg_ = e.getError();
			return nullopt;
		}
	}

	// File validation
	int smallDupes = 0, fileCount = aFiles.size(), filesExist = 0;

	DirectoryBundleAddResult info;
	ErrorCollector errors(fileCount);

	aFiles.erase(boost::remove_if(aFiles, [&](BundleFileAddData& bfi) {
		try {
			validateBundleFileHooked(target, bfi, aOptions.caller);
			return false; // valid
		} catch(const QueueException& e) {
			errors.add(e.getError(), bfi.name, false);
			info.filesFailed++;
		} catch(const FileException& e) {
			errors.add(e.getError(), bfi.name, true);
			filesExist++;
		} catch(const DupeException& e) {
			bool isSmall = bfi.size < Util::convertSize(SETTING(MIN_DUPE_CHECK_SIZE), Util::KB);
			errors.add(e.getError(), bfi.name, isSmall);
			if (isSmall) {
				smallDupes++;
				return false;
			} else {
				info.filesFailed++;
			}
		}

		return true;
	}), aFiles.end());


	// Check file validation errors
	if (aFiles.empty()) {
		errorMsg_ = errors.getMessage();
		return nullopt;
	} else if (smallDupes > 0) {
		if (smallDupes == static_cast<int>(aFiles.size())) {
			// No reason to continue if all remaining files are dupes
			errorMsg_ = errors.getMessage();
			return nullopt;
		} else {
			// Those will get queued, don't report
			errors.clearMinor();
		}
	}



	BundlePtr b = nullptr;
	bool wantConnection = false;
	Bundle::Status oldStatus;

	QueueItem::ItemBoolList queueItems;
	{
		WLock l(cs);
		b = getBundle(target, aDirectory.prio, aDirectory.date, false);
		oldStatus = b->getStatus();

		//add the files
		for (auto& bfi: aFiles) {
			try {
				auto addInfo = addBundleFile(target + bfi.name, bfi.size, bfi.tth, aOptions.optionalUser, 0, true, bfi.prio, wantConnection, b);
				if (addInfo.second) {
					info.filesAdded++;
				} else {
					info.filesUpdated++;
				}

				queueItems.push_back(std::move(addInfo));
			} catch(QueueException& e) {
				errors.add(e.getError(), bfi.name, false);
				info.filesFailed++;
			} catch(FileException& e) {
				//the file has finished after we made the initial target check
				errors.add(e.getError(), bfi.name, true);
				filesExist++;
			}
		}

		addBundle(b, info.filesAdded);
	}

	if (queueItems.empty()) {
		errorMsg_ = errors.getMessage();
		return nullopt;
	}

	dcassert(b);

	// Those don't need to be reported to the user
	errors.clearMinor();

	onBundleAdded(b, oldStatus, queueItems, aOptions.optionalUser, wantConnection);
	info.bundleInfo = BundleAddInfo(b, oldStatus != Bundle::STATUS_NEW);

	if (info.filesAdded > 0) {
		// Report
		if (oldStatus == Bundle::STATUS_NEW) {
			log(STRING_F(BUNDLE_CREATED, b->getName() % info.filesAdded) + " (" + CSTRING_F(TOTAL_SIZE, Util::formatBytes(b->getSize())) + ")", LogMessage::SEV_INFO);
		} else if (b->getTarget() == target) {
			log(STRING_F(X_BUNDLE_ITEMS_ADDED, info.filesAdded % b->getName().c_str()), LogMessage::SEV_INFO);
		} else {
			log(STRING_F(BUNDLE_MERGED, Util::getLastDir(target) % b->getName() % info.filesAdded), LogMessage::SEV_INFO);
		}
	}

	errorMsg_ = errors.getMessage();
	return info;
}

void QueueManager::addLoadedBundle(const BundlePtr& aBundle) noexcept {
	WLock l(cs);
	if (aBundle->isEmpty())
		return;

	if (bundleQueue.getMergeBundle(aBundle->getTarget()))
		return;

	bundleQueue.addBundle(aBundle);
}

void QueueManager::addBundle(const BundlePtr& aBundle, int aItemsAdded) noexcept {
	if (aItemsAdded == 0) {
		return;
	}

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
}

void QueueManager::onBundleAdded(const BundlePtr& aBundle, Bundle::Status aOldStatus, const QueueItem::ItemBoolList& aItemsAdded, const HintedUser& aOptionalUser, bool aWantConnection) noexcept {
	if (aOldStatus == Bundle::STATUS_NEW) {
		fire(QueueManagerListener::BundleAdded(), aBundle);

		if (autoSearchEnabled() && !aBundle->isPausedPrio()) {
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
				fire(QueueManagerListener::ItemSources(), itemInfo.first);
			}
		}
	}

	if (aOptionalUser) {
		fire(QueueManagerListener::SourceFilesUpdated(), aOptionalUser);
	}

	if (aWantConnection && aOptionalUser && aOptionalUser.user->isOnline()) {
		//connect to the source (we must have an user in this case)
		ConnectionManager::getInstance()->getDownloadConnection(aOptionalUser);
	}
}

void QueueManager::runAddBundleHooksThrow(string& target_, BundleAddData& aDirectory, const HintedUser& aOptionalUser, bool aIsFile) {
	auto results = bundleValidationHook.runHooksDataThrow(this, target_, aDirectory, aOptionalUser, aIsFile);
	for (const auto& result: results) {
		auto& data = result->data;

		// Prio
		if (data.priority != Priority::DEFAULT) {
			aDirectory.prio = data.priority;
		}

		// Target
		if (!data.target.empty()) {
			target_ = data.target;
		}
	}
}

string QueueManager::formatBundleTarget(const string& aPath, time_t aRemoteDate) noexcept {
	ParamMap params;
	params["username"] = [] { return Util::getSystemUsername(); };
	
	auto time = (SETTING(FORMAT_DIR_REMOTE_TIME) && aRemoteDate > 0) ? aRemoteDate : GET_TIME();
	auto formatedPath = Util::formatParams(aPath, params, nullptr, time);
	return Util::validatePath(formatedPath);
}

BundleAddInfo QueueManager::createFileBundleHooked(const BundleAddOptions& aOptions, BundleFileAddData& aFileInfo, Flags::MaskType aFlags) {
	auto filePath = aOptions.target;

	// Bundle validation
	try {
		runAddBundleHooksThrow(filePath, aFileInfo, aOptions.optionalUser, true);
	} catch (const HookRejectException& e) {
		throw QueueException(ActionHookRejection::formatError(e.getRejection()));
	}

	filePath = formatBundleTarget(filePath, aFileInfo.date);

	// Source validation
	if (aOptions.optionalUser) {
		checkSourceHooked(aOptions.optionalUser, aOptions.caller);
	}

	validateBundleFileHooked(filePath, aFileInfo, aOptions.caller, aFlags);

	BundlePtr b = nullptr;
	bool wantConnection = false;

	auto target = filePath + aFileInfo.name;

	Bundle::Status oldStatus;
	FileAddInfo fileAddInfo;

	{
		WLock l(cs);
		b = getBundle(target, aFileInfo.prio, aFileInfo.date, true);
		oldStatus = b->getStatus();

		fileAddInfo = addBundleFile(target, aFileInfo.size, aFileInfo.tth, aOptions.optionalUser, aFlags, true, aFileInfo.prio, wantConnection, b);

		addBundle(b, fileAddInfo.second ? 1 : 0);
	}

	onBundleAdded(b, oldStatus, { fileAddInfo }, aOptions.optionalUser, wantConnection);

	if (fileAddInfo.second) {
		if (oldStatus == Bundle::STATUS_NEW) {
			log(STRING_F(FILE_X_QUEUED, b->getName() % Util::formatBytes(b->getSize())), LogMessage::SEV_INFO);
		} else {
			log(STRING_F(BUNDLE_ITEM_ADDED, Util::getFileName(target) % b->getName()), LogMessage::SEV_INFO);
		}
	}

	return BundleAddInfo(b, oldStatus != Bundle::STATUS_NEW);
}

QueueManager::FileAddInfo QueueManager::addBundleFile(const string& aTarget, int64_t aSize, const TTHValue& aRoot, const HintedUser& aOptionalUser, Flags::MaskType aFlags /* = 0 */,
								   bool addBad /* = true */, Priority aPrio, bool& wantConnection_, BundlePtr& aBundle)
{
	dcassert(aSize > 0);

	// Add the file
	auto ret = fileQueue.add(aTarget, aSize, aFlags, aPrio, Util::emptyString, GET_TIME(), aRoot);

	if(!ret.second) {
		// Exists already
		if (checkRemovedTarget(ret.first, aSize, aRoot)) {
			ret = std::move(fileQueue.add(aTarget, aSize, aFlags, aPrio, Util::emptyString, GET_TIME(), aRoot));
		}
	}
	
	// New item? Add in the bundle
	if (ret.second) {
		// Highest wouldn't be started if the bundle is forced paused
		if (aBundle->getPriority() == Priority::PAUSED && ret.first->getPriority() == Priority::HIGHEST) {
			ret.first->setPriority(Priority::HIGH);
		}

		bundleQueue.addBundleItem(ret.first, aBundle);
	}

	// Add the source
	if (aOptionalUser) {
		try {
			if (addValidatedSource(ret.first, aOptionalUser, (Flags::MaskType)(addBad ? QueueItem::Source::FLAG_MASK : 0))) {
				wantConnection_ = true;
			}
		} catch(const Exception&) {
			dcassert(!ret.second);
			//This should never fail for new items, and for existing items it doesn't matter (useless spam)
		}
	}

	return ret;
}

bool QueueManager::readdQISourceHooked(const string& target, const HintedUser& aUser) noexcept {
	QueueItemPtr qi = nullptr;

	{
		WLock l(cs);
		qi = fileQueue.findFile(target);
		if (!qi || !qi->isBadSource(aUser)) {
			return false;
		}
	}

	auto added = addSourcesHooked(aUser, { qi }, QueueItem::Source::FLAG_MASK);
	return added > 0;
}

void QueueManager::readdBundleSourceHooked(BundlePtr aBundle, const HintedUser& aUser) noexcept {
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

	addSourcesHooked(aUser, items, QueueItem::Source::FLAG_MASK);
}

string QueueManager::checkTarget(const string& toValidate, const string& aParentDir /*empty*/) {
#ifdef _WIN32
	if (toValidate.length() + aParentDir.length() > UNC_MAX_PATH) {
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
bool QueueManager::addValidatedSource(const QueueItemPtr& qi, const HintedUser& aUser, Flags::MaskType aAddBad) {
	if (qi->isDownloaded()) //no need to add source to finished item.
		throw QueueException(STRING(FILE_ALREADY_FINISHED) + ": " + Util::getFileName(qi->getTarget()));
	
	bool wantConnection = !qi->isPausedPrio();
	dcassert(qi->getBundle() || qi->getPriority() == Priority::HIGHEST);

	if(qi->isSource(aUser)) {
		if(qi->isSet(QueueItem::FLAG_USER_LIST)) {
			return wantConnection;
		}
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + Util::getFileName(qi->getTarget()));
	}

	bool isBad = false;
	if(qi->isBadSourceExcept(aUser, aAddBad, isBad)) {
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + Util::getFileName(qi->getTarget()));
	}

	qi->addSource(aUser);
	userQueue.addQI(qi, aUser, isBad);

#if defined(_WIN32) && defined(HAVE_GUI)
	if ((!SETTING(SOURCEFILE).empty()) && (!SETTING(SOUNDS_DISABLED)))
		PlaySound(Text::toT(SETTING(SOURCEFILE)).c_str(), NULL, SND_FILENAME | SND_ASYNC);
#endif

	if (qi->getBundle()) {
		qi->getBundle()->setDirty();
	}

	return wantConnection;
	
}

Download* QueueManager::getDownload(UserConnection& aSource, const QueueTokenSet& aRunningBundles, const OrderedStringSet& aOnlineHubs, string& lastError_, string& newUrl, QueueItemBase::DownloadType aType) noexcept{
	QueueItemPtr q = nullptr;
	Download* d = nullptr;

	{
		WLock l(cs);
		dcdebug("Getting download for %s...", aSource.getUser()->getCID().toBase32().c_str());

		const UserPtr& u = aSource.getUser();
		bool hasDownload = false;

		q = userQueue.getNext(aSource.getUser(), aRunningBundles, aOnlineHubs, lastError_, hasDownload, Priority::LOWEST, aSource.getChunkSize(), aSource.getSpeed(), aType);
		if (!q) {
			dcdebug("none\n");
			return nullptr;
		}

		auto source = q->getSource(aSource.getUser());

		//update the hub hint
		newUrl = aSource.getHubUrl();
		source->updateDownloadHubUrl(aOnlineHubs, newUrl, (q->isSet(QueueItem::FLAG_USER_LIST) && !q->isSet(QueueItem::FLAG_TTHLIST_BUNDLE)));

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

	fire(QueueManagerListener::ItemSources(), q);
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


	//check if we have free space to continue the download now... otherwise results in paused priority..
	if (aQI->getBundle() && (aQI->getBundle()->getStatus() == Bundle::STATUS_DOWNLOAD_ERROR)) {
		if (File::getFreeSpace(aQI->getBundle()->getTarget()) >= static_cast<int64_t>(aQI->getSize() - aQI->getDownloadedBytes())) {
			setBundleStatus(aQI->getBundle(), Bundle::STATUS_QUEUED);
		} else {
			lastError_ = aQI->getBundle()->getError();
			onDownloadError(aQI->getBundle(), lastError_);
			return false;
		}
	}

	size_t downloadCount = DownloadManager::getInstance()->getFileDownloadConnectionCount();
	bool slotsFull = (AirUtil::getSlots(true) != 0) && (downloadCount >= static_cast<size_t>(AirUtil::getSlots(true)));
	bool speedFull = (AirUtil::getSpeedLimit(true) != 0) && (DownloadManager::getInstance()->getRunningAverage() >= Util::convertSize(AirUtil::getSpeedLimit(true), Util::KB));
	//log("Speedlimit: " + Util::toString(Util::getSpeedLimit(true)*1024) + " slots: " + Util::toString(Util::getSlots(true)) + " (avg: " + Util::toString(getRunningAverage()) + ")");

	if (slotsFull || speedFull) {
		size_t slots = AirUtil::getSlots(true);
		bool extraFull = (slots != 0) && (downloadCount >= (slots + static_cast<size_t>(SETTING(EXTRA_DOWNLOAD_SLOTS))));
		if (extraFull || mcn || aQI->getPriority() != Priority::HIGHEST) {
			lastError_ = slotsFull ? STRING(ALL_DOWNLOAD_SLOTS_TAKEN) : STRING(MAX_DL_SPEED_REACHED);
			return false;
		}
		return true;
	}

	// bundle with the lowest prio? don't start if there are other bundle running
	if (aQI->getBundle() && aQI->getBundle()->getPriority() == Priority::LOWEST && !runningBundles.empty() && runningBundles.find(aQI->getBundle()->getToken()) == runningBundles.end()) {
		lastError_ = STRING(LOWEST_PRIO_ERR_BUNDLES);
		return false;
	}

	if (aQI->getPriority() == Priority::LOWEST) {
		if (aQI->getBundle()) {
			// start only if there are no other downloads running in this bundle (or the downloads belong to this file)
			auto bundleDownloads = DownloadManager::getInstance()->getBundleDownloadConnectionCount(aQI->getBundle());

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
		qi = userQueue.getNext(aUser, runningBundles, onlineHubs, lastError_, hasDownload, Priority::LOWEST, 0, aLastSpeed, aType);
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
			qi = userQueue.getNext(aUser, runningBundles, hubs, lastError_, hasDownload, Priority::LOWEST, 0, 0, aType);

			if (qi) {
				if (qi->getBundle()) {
					bundleToken = qi->getBundle()->getToken();
				}

				if (hubs.find(hubHint) == hubs.end()) {
					//we can't connect via a hub that is offline...
					hubHint = *hubs.begin();
				}

				allowUrlChange = !qi->isSet(QueueItem::FLAG_USER_LIST);
				qi->getSource(aUser)->updateDownloadHubUrl(hubs, hubHint, (qi->isSet(QueueItem::FLAG_USER_LIST) && !qi->isSet(QueueItem::FLAG_TTHLIST_BUNDLE)));
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

	newFiles_ = addValidatedSources(dl.getHintedUser(), matchingItems, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE, bundles_);
}

QueueItemPtr QueueManager::getQueueInfo(const HintedUser& aUser) noexcept {
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

	return qi;
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

void QueueManager::updateFilelistUrl(const HintedUser& aUser) noexcept {
	QueueItemList updated;

	{
		QueueItemList ql;

		RLock l(cs);
		userQueue.getUserQIs(aUser, ql);

		for (const auto& q : ql) {
			if (q->isFilelist() && q->isSet(QueueItem::FLAG_CLIENT_VIEW)) {
				auto source = q->getSource(aUser);
				source->setHubUrl(aUser.hint);
				updated.push_back(q);
			}
		}
	}

	if (!updated.empty()) {
		for (const auto& q: updated) {
			fire(QueueManagerListener::ItemSources(), q);
		}

		ConnectionManager::getInstance()->getDownloadConnection(aUser);
	}
}

void QueueManager::logDownload(Download* aDownload) noexcept {
	if (!aDownload->isFilelist() || SETTING(LOG_FILELIST_TRANSFERS)) {
		if (SETTING(SYSTEM_SHOW_DOWNLOADS)) {
			auto nicks = ClientManager::getInstance()->getFormatedNicks(aDownload->getHintedUser());
			log(STRING_F(FINISHED_DOWNLOAD, aDownload->getPath() % nicks), LogMessage::SEV_INFO);
		}

		if (SETTING(LOG_DOWNLOADS)) {
			ParamMap params;
			aDownload->getParams(aDownload->getUserConnection(), params);
			LOG(LogManager::DOWNLOAD, params);
		}
	}
}

void QueueManager::renameDownloadedFile(const string& source, const string& target, const QueueItemPtr& aQI) noexcept {
	try {
		File::ensureDirectory(target);
		UploadManager::getInstance()->abortUpload(source);
		File::renameFile(source, target);
	} catch(const FileException& e1) {
		// Try to just rename it to the correct name at least
		string newTarget = Util::getFilePath(source) + Util::getFileName(target);
		try {
			File::renameFile(source, newTarget);
			log(STRING_F(MOVE_FILE_FAILED, newTarget % Util::getFilePath(target) % e1.getError()), LogMessage::SEV_ERROR);
		} catch(const FileException& e2) {
			log(STRING_F(UNABLE_TO_RENAME, source % e2.getError()), LogMessage::SEV_ERROR);
		}
	}

	tasks.addTask([=] {
		// Handle the results later...
		runFileCompletionHooks(aQI);

		auto bundle = aQI->getBundle();
		if (bundle) {
			sendFileCompletionNotifications(aQI);

			{
				RLock l(cs);
				if (bundle->getFinishedFiles().empty() && bundle->getQueueItems().empty()) {
					// The bundle was removed?
					return;
				}
			}

			checkBundleFinishedHooked(bundle);
		}
	});
}

void QueueManager::sendFileCompletionNotifications(const QueueItemPtr& qi) noexcept {
	dcassert(qi->getBundle());
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
		//cmd.addParam("HI", u.hint); update adds sources, so ip port needed here...
		cmd.addParam("TH", qi->getTTH().toBase32());
		ClientManager::getInstance()->sendUDP(cmd, u.user->getCID(), false, true);
	}
}


bool QueueManager::checkBundleFinishedHooked(const BundlePtr& aBundle) noexcept {
	bool hasNotifications = false, isPrivate = false;

	if (aBundle->getStatus() == Bundle::STATUS_SHARED)
		return true;

	if (!aBundle->isDownloaded()) {
		return false;
	}

	if (!checkFailedBundleFilesHooked(aBundle, false)) {
		return false;
	}


	{
		RLock l (cs);
		// Check if there are queued or non-moved files remaining
		if (!aBundle->filesCompleted()) {
			return false;
		}

		// In order to avoid notifications about adding the file in share...
		if (aBundle->isFileBundle() && !aBundle->getFinishedFiles().empty()) {
			isPrivate = aBundle->getFinishedFiles().front()->isSet(QueueItem::FLAG_PRIVATE);
		}

		hasNotifications = !aBundle->getFinishedNotifications().empty();
	}

	if (hasNotifications) {
		//the bundle has finished downloading so we don't need any partial bundle sharing notifications

		Bundle::FinishedNotifyList fnl;
		{
			WLock l(cs);
			aBundle->clearFinishedNotifications(fnl);
		}

		for (const auto& ubp: fnl) {
			sendRemovePBD(ubp.first, ubp.second);
		}
	}

	log(STRING_F(DL_BUNDLE_FINISHED, aBundle->getName().c_str()), LogMessage::SEV_INFO);
	shareBundle(aBundle, false);

	return true;
}

void QueueManager::shareBundle(BundlePtr aBundle, bool aSkipValidations) noexcept {
	
	if (aBundle->getStatus() == Bundle::STATUS_SHARED)
		return;

	tasks.addTask([=] {
		if (!aSkipValidations && !runBundleCompletionHooks(aBundle)) {
			return;
		}

		setBundleStatus(aBundle, Bundle::STATUS_COMPLETED);

		if (!ShareManager::getInstance()->allowShareDirectoryHooked(aBundle->getTarget(), this)) {
			log(STRING_F(NOT_IN_SHARED_DIR, aBundle->getTarget().c_str()), LogMessage::SEV_INFO);
			return;
		}

		// Add the downloaded trees for all bundle file paths in hash database
		QueueItemList finishedFiles;

		{
			RLock l(cs);
			finishedFiles = aBundle->getFinishedFiles();
		}

		{
			HashManager::HashPauser pauser;
			for (const auto& q : finishedFiles) {
				HashedFile fi(q->getTTH(), File::getLastModified(q->getTarget()), q->getSize());
				try {
					HashManager::getInstance()->addFile(q->getTarget(), fi);
				}
				catch (...) {
					//hash it...
				}
			}
		}

		ShareManager::getInstance()->shareBundle(aBundle);
		if (aBundle->isFileBundle()) {
			setBundleStatus(aBundle, Bundle::STATUS_SHARED);
		}
	});
}

bool QueueManager::checkFailedBundleFilesHooked(const BundlePtr& aBundle, bool aRevalidateFailed) noexcept {
	QueueItemList failedFiles;

	{
		RLock l(cs);
		failedFiles = aBundle->getFailedItems();
	}

	if (aRevalidateFailed && !failedFiles.empty()) {
		setBundleStatus(aBundle, Bundle::STATUS_VALIDATION_RUNNING);
		failedFiles.erase(remove_if(failedFiles.begin(), failedFiles.end(), [this](const QueueItemPtr& aQI) {
			return runFileCompletionHooks(aQI);
		}), failedFiles.end());
	}

	if (!failedFiles.empty()) {
		aBundle->setHookError(failedFiles.front()->getHookError());
		setBundleStatus(aBundle, Bundle::STATUS_VALIDATION_ERROR);
		return false;
	}

	return true;
}

bool QueueManager::runBundleCompletionHooks(const BundlePtr& aBundle) noexcept {
	if (!checkFailedBundleFilesHooked(aBundle, true)) {
		return false;
	}

	if (bundleCompletionHook.hasSubscribers()) {
		setBundleStatus(aBundle, Bundle::STATUS_VALIDATION_RUNNING);

		auto error = bundleCompletionHook.runHooksError(this, aBundle);
		if (error) {
			aBundle->setHookError(error);
			setBundleStatus(aBundle, Bundle::STATUS_VALIDATION_ERROR);
			return false;
		}
	}

	setBundleStatus(aBundle, Bundle::STATUS_COMPLETED);
	return true;
}

bool QueueManager::runFileCompletionHooks(const QueueItemPtr& aQI) noexcept {
	if (aQI->getBundle() && fileCompletionHook.hasSubscribers()) {
		setFileStatus(aQI, QueueItem::STATUS_VALIDATION_RUNNING);

		auto error = fileCompletionHook.runHooksError(this, aQI);
		if (error) {
			aQI->setHookError(error);
			setFileStatus(aQI, QueueItem::STATUS_VALIDATION_ERROR);
			return false;
		}
	}

	setFileStatus(aQI, QueueItem::STATUS_COMPLETED);
	return true;
}

void QueueManager::onDownloadError(const BundlePtr& aBundle, const string& aError) {
	if (!aBundle) {
		return;
	}

	//Pause bundle, to give other bundles a chance to get downloaded...
	if (aBundle->getStatus() == Bundle::STATUS_QUEUED || aBundle->getStatus() == Bundle::STATUS_DOWNLOAD_ERROR) {
		tasks.addTask([=] { setBundlePriority(aBundle, Priority::PAUSED_FORCE, false); });
	}

	aBundle->setError(aError);
	setBundleStatus(aBundle, Bundle::STATUS_DOWNLOAD_ERROR);
}

void QueueManager::putDownloadHooked(Download* aDownload, bool aFinished, bool aNoAccess /*false*/, bool aRotateQueue /*false*/) {
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

	if (q->isDownloaded()) {
		// Trying to finish it twice? Hmm..
		return;
	}

	if (!aFinished) {
		onDownloadFailed(q, d.get(), aNoAccess, aRotateQueue);
	} else if (q->isSet(QueueItem::FLAG_USER_LIST)) {
		onFilelistDownloadCompletedHooked(q, d.get());
	} else if (d->getType() == Transfer::TYPE_TREE) {
		onTreeDownloadCompleted(q, d.get());
	} else {
		onFileDownloadCompleted(q, d.get());
	}
}

void QueueManager::onDownloadFailed(const QueueItemPtr& aQI, Download* aDownload, bool aNoAccess, bool aRotateQueue) noexcept {
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
		if (u.user != aDownload->getUser()) //trying a different user? we rotated queue, shouldnt we try another file?
			ConnectionManager::getInstance()->getDownloadConnection(u);
	}

	fire(QueueManagerListener::ItemStatus(), aQI);
	return;
}

void QueueManager::onFilelistDownloadCompletedHooked(const QueueItemPtr& aQI, Download* aDownload) noexcept {
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
			DirectoryListingManager::getInstance()->processListHooked(aQI->getListName(), aDownload->getPFS(), aDownload->getHintedUser(), aDownload->getListDirectoryPath(), aQI->getFlags());
		}

		if (aQI->isSet(QueueItem::FLAG_MATCH_QUEUE)) {
			WLock l(cs);
			matchLists.right.erase(aQI->getTarget());
		}
	} else if (aDownload->getType() == Transfer::TYPE_PARTIAL_LIST) {
		fire(QueueManagerListener::PartialListFinished(), aDownload->getHintedUser(), aDownload->getPFS(), aQI->getListDirectoryPath());
	} else {
		fire(QueueManagerListener::ItemFinished(), aQI, aQI->getListDirectoryPath(), aDownload->getHintedUser(), aDownload->getAverageSpeed());
	}

	logDownload(aDownload);

	{
		WLock l(cs);
		userQueue.removeQI(aQI);
		fileQueue.remove(aQI);
	}

	fire(QueueManagerListener::ItemRemoved(), aQI, true);
}

void QueueManager::onTreeDownloadCompleted(const QueueItemPtr& aQI, Download* aDownload) {
	{
		WLock l(cs);
		userQueue.removeDownload(aQI, aDownload->getToken());
	}

	dcassert(aDownload->getTreeValid());
	HashManager::getInstance()->addTree(aDownload->getTigerTree());
	fire(QueueManagerListener::ItemStatus(), aQI);
}

void QueueManager::onFileDownloadCompleted(const QueueItemPtr& aQI, Download* aDownload) noexcept {
	dcassert(aDownload->getType() == Transfer::TYPE_FILE);

	aDownload->setOverlapped(false);
	bool wholeFileCompleted = false;

	{
		WLock l(cs);
		aQI->addFinishedSegment(aDownload->getSegment());
		wholeFileCompleted = aQI->segmentsDone();

		dcdebug("Finish segment for %s (" I64_FMT ", " I64_FMT ")\n", aDownload->getToken().c_str(), aDownload->getSegment().getStart(), aDownload->getSegment().getEnd());

		if (wholeFileCompleted) {
			// Disconnect all possible overlapped downloads
			for (auto aD : aQI->getDownloads()) {
				if (compare(aD->getToken(), aDownload->getToken()) != 0)
					aD->getUserConnection().disconnect();
			}

			aQI->setTimeFinished(GET_TIME());
			aQI->setStatus(QueueItem::STATUS_DOWNLOADED);
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

		logDownload(aDownload);

		auto nicks = ClientManager::getInstance()->getFormatedNicks(aDownload->getHintedUser());
		aQI->setLastSource(nicks);
		fire(QueueManagerListener::ItemFinished(), aQI, Util::emptyString, aDownload->getHintedUser(), aDownload->getAverageSpeed());
	}

	if (wholeFileCompleted && !aQI->getBundle()) {
		fire(QueueManagerListener::ItemRemoved(), aQI, true);
	} else {
		fire(QueueManagerListener::ItemStatus(), aQI);
	}
}

void QueueManager::setSegments(const string& aTarget, uint8_t aSegments) noexcept {
	RLock l (cs);
	auto qi = fileQueue.findFile(aTarget);
	if (qi) {
		qi->setMaxSegments(aSegments);
	}
}

void QueueManager::addDoneSegment(const QueueItemPtr& aQI, const Segment& aSegment) noexcept {
	{
		WLock l(cs);
		aQI->addFinishedSegment(aSegment);
	}

	fire(QueueManagerListener::ItemStatus(), aQI);
	
	// TODO: add bundle listener
}

void QueueManager::resetDownloadedSegments(const QueueItemPtr& aQI) noexcept {
	{
		WLock l(cs);
		aQI->resetDownloaded();
	}

	fire(QueueManagerListener::ItemStatus(), aQI);

	// TODO: add bundle listener
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

	addValidatedSources(aUser, ql, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
}

void QueueManager::removeQI(const QueueItemPtr& q, bool aDeleteData /*false*/) noexcept {
	StringList x;
	dcassert(q);

	// For partial-share
	UploadManager::getInstance()->abortUpload(q->getTempTarget());

	{
		WLock l(cs);
		if (q->isSet(QueueItem::FLAG_MATCH_QUEUE)) {
			matchLists.right.erase(q->getTarget());
		}

		if(q->isRunning()) {
			for(const auto& d: q->getDownloads()) 
				x.push_back(d->getToken());
		} else if(!q->getTempTarget().empty() && q->getTempTarget() != q->getTarget()) {
			File::deleteFile(q->getTempTarget());
		}

		if (!q->isDownloaded()) {
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

void QueueManager::removeFileSource(const QueueItemPtr& q, const UserPtr& aUser, Flags::MaskType aReason, bool aRemoveConn /* = true */) noexcept {
	bool isRunning = false;
	bool removeCompletely = false;
	{
		WLock l(cs);
		if(!q->isSource(aUser))
			return;

		if (q->isDownloaded())
			return;
	
		if(q->isSet(QueueItem::FLAG_USER_LIST)) {
			q->getSource(aUser)->setFlag(aReason);
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

	fire(QueueManagerListener::ItemSources(), q);

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

void QueueManager::setBundlePriority(QueueToken aBundleToken, Priority p) noexcept {
	BundlePtr bundle = nullptr;
	{
		RLock l(cs);
		bundle = bundleQueue.findBundle(aBundleToken);
	}

	setBundlePriority(bundle, p, false);
}

void QueueManager::setBundlePriority(const BundlePtr& aBundle, Priority p, bool aKeepAutoPrio, time_t aResumeTime/* = 0*/) noexcept {
	if (!aBundle || aBundle->getStatus() == Bundle::STATUS_RECHECK)
		return;

	if (p == Priority::DEFAULT) {
		if (!aBundle->getAutoPriority()) {
			toggleBundleAutoPriority(aBundle);
		}
		
		return;
	}

	Priority oldPrio = aBundle->getPriority();
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

		if (aBundle->isDownloaded())
			return;

		bundleQueue.removeSearchPrio(aBundle);
		userQueue.setBundlePriority(aBundle, p);
		bundleQueue.addSearchPrio(aBundle);
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
		fire(QueueManagerListener::ItemPriority(), qi);
	}

	fire(QueueManagerListener::BundlePriority(), aBundle);

	aBundle->setDirty();

	if (p == Priority::PAUSED_FORCE) {
		DownloadManager::getInstance()->disconnectBundle(aBundle);
	} else if (oldPrio <= Priority::LOWEST) {
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

void QueueManager::toggleBundleAutoPriority(const BundlePtr& aBundle) noexcept {
	if (aBundle->isDownloaded())
		return;

	aBundle->setAutoPriority(!aBundle->getAutoPriority());
	if (aBundle->isFileBundle()) {
		RLock l(cs);
		aBundle->getQueueItems().front()->setAutoPriority(aBundle->getAutoPriority());
	}

	if (aBundle->isPausedPrio()) {
		// We don't want this one to stay paused if the auto priorities can't be counted
		setBundlePriority(aBundle, Priority::LOW, true);
	} else {
		// Auto priority state may not be fired if the old priority is kept
		fire(QueueManagerListener::BundlePriority(), aBundle);
	}

	// Recount priorities as soon as possible
	setLastAutoPrio(0);

	aBundle->setDirty();
}

int QueueManager::removeCompletedBundles() noexcept {
	BundleList bundles;
	{
		RLock l(cs);
		boost::algorithm::copy_if(bundleQueue.getBundles() | map_values, back_inserter(bundles), [](const BundlePtr& aBundle) {
			return aBundle->isCompleted();
		});
	}

	for (auto& bundle : bundles) {
		removeBundle(bundle, false);
	}

	return static_cast<int>(bundles.size());
}

void QueueManager::setPriority(Priority p) noexcept {
	Bundle::TokenMap bundles;
	{
		RLock l(cs);
		bundles = bundleQueue.getBundles();
	}

	for (auto& bundle : bundles | map_values) {
		setBundlePriority(bundle, p);
	}
}

void QueueManager::setQIPriority(const string& aTarget, Priority p) noexcept {
	QueueItemPtr q = nullptr;
	{
		RLock l(cs);
		q = fileQueue.findFile(aTarget);
	}

	setQIPriority(q, p);
}

void QueueManager::setQIPriority(const QueueItemPtr& q, Priority p, bool aKeepAutoPrio /*false*/) noexcept {
	HintedUserList getConn;
	bool running = false;
	if (!q || !q->getBundle()) {
		//items without a bundle should always use the highest prio
		return;
	}

	if (p == Priority::DEFAULT) {
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

	if (q->getPriority() != p && !q->isDownloaded()) {
		WLock l(cs);
		if((q->isPausedPrio() && !b->isPausedPrio()) || (p == Priority::HIGHEST && b->getPriority() != Priority::PAUSED_FORCE)) {
			// Problem, we have to request connections to all these users...
			q->getOnlineUsers(getConn);
		}

		running = q->isRunning();

		if (!aKeepAutoPrio)
			q->setAutoPriority(false);

		userQueue.setQIPriority(q, p);
	}

	fire(QueueManagerListener::ItemPriority(), q);

	b->setDirty();
	if (p == Priority::PAUSED_FORCE && running) {
		DownloadManager::getInstance()->abortDownload(q->getTarget());
	} else if (!q->isPausedPrio()) {
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
	fire(QueueManagerListener::ItemPriority(), q);

	q->getBundle()->setDirty();

	if(q->getAutoPriority()) {
		if (SETTING(AUTOPRIO_TYPE) == SettingsManager::PRIO_PROGRESS) {
			setQIPriority(q, q->calculateAutoPriority());
		} else if (q->isPausedPrio()) {
			setQIPriority(q, Priority::LOW);
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

size_t QueueManager::removeBundleSource(QueueToken aBundleToken, const UserPtr& aUser, Flags::MaskType aReason) noexcept {
	BundlePtr bundle = nullptr;
	{
		RLock l(cs);
		bundle = bundleQueue.findBundle(aBundleToken);
	}

	return removeBundleSource(bundle, aUser, aReason);
}

size_t QueueManager::removeBundleSource(BundlePtr aBundle, const UserPtr& aUser, Flags::MaskType aReason) noexcept {
	if (!aBundle) {
		return 0;
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
	return ql.size();
}

void QueueManager::sendRemovePBD(const HintedUser& aUser, const string& aRemoteToken) noexcept {
	AdcCommand cmd(AdcCommand::CMD_PBD, AdcCommand::TYPE_UDP);

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
	QueueLoader() : qm(QueueManager::getInstance()) { }
	~QueueLoader() { }
	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name);
	void createFileBundle(QueueItemPtr& aQI, QueueToken aToken);

	void loadDirectoryBundle(StringPairList& attribs, bool simple);
	void loadFileBundle(StringPairList& attribs, bool simple);
	void loadQueueFile(StringPairList& attribs, bool simple);
	void loadFinishedFile(StringPairList& attribs, bool simple);

	Priority validatePrio(const string& aPrio);
private:
	struct FileBundleInfo {
		QueueToken token = 0;
		time_t date = 0;
		time_t resumeTime = 0;
		bool addedByAutosearch = false;
	};

	QueueItemPtr curFile = nullptr;
	BundlePtr curBundle = nullptr;
	bool inLegacyQueue = false;
	bool inDirBundle = false;
	bool inFileBundle = false;
	FileBundleInfo curFileBundleInfo;

	int bundleVersion = 0;
	QueueManager* qm;
};

void QueueManager::loadQueue(StartupLoader& aLoader) noexcept {
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
					log(STRING_F(BUNDLE_LOAD_FAILED, path % e.getError().c_str()), LogMessage::SEV_ERROR);
					File::deleteFile(path);
				}
			}
			loaded++;
			aLoader.progressF(static_cast<float>(loaded) / static_cast<float>(fileList.size()));
		});
	} catch (std::exception& e) {
		log("Loading the queue failed: " + string(e.what()), LogMessage::SEV_INFO);
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
	ShareManager::getInstance()->addListener(this);

	auto finishedCount = getFinishedBundlesCount();
	if (finishedCount > 500) {
		log(STRING_F(BUNDLE_X_FINISHED_WARNING, finishedCount), LogMessage::SEV_WARNING);
	}

	// Completion checks involve hooks, let everything load first
	aLoader.addPostLoadTask([this] {
		checkCompletedBundles(Util::emptyString, true);
	});
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

Priority QueueLoader::validatePrio(const string& aPrio) {
	int prio = Util::toInt(aPrio);
	if (bundleVersion == 1)
		prio++;

	if (prio > static_cast<int>(Priority::HIGHEST))
		return Priority::HIGHEST;
	if (prio < static_cast<int>(Priority::PAUSED_FORCE))
		return Priority::PAUSED_FORCE;

	return static_cast<Priority>(prio);
}

void QueueLoader::createFileBundle(QueueItemPtr& aQI, QueueToken aToken) {
	if (ConnectionManager::getInstance()->tokens.addToken(Util::toString(aToken), CONNECTION_TYPE_DOWNLOAD)) {
		curBundle = make_shared<Bundle>(aQI, curFileBundleInfo.date, aToken, false);
		curBundle->setTimeFinished(aQI->getTimeFinished());
		curBundle->setAddedByAutoSearch(curFileBundleInfo.addedByAutosearch);
		curBundle->setResumeTime(curFileBundleInfo.resumeTime);

		qm->bundleQueue.addBundleItem(aQI, curBundle);
	} else {
		qm->fileQueue.remove(aQI);
		throw Exception("Duplicate token");
	}
}

void QueueLoader::loadDirectoryBundle(StringPairList& attribs, bool) {
	bundleVersion = Util::toInt(getAttrib(attribs, sVersion, 0));
	if (bundleVersion == 0 || bundleVersion > Util::toInt(DIR_BUNDLE_VERSION))
		throw Exception("Non-supported directory bundle version");

	const string& bundleTarget = getAttrib(attribs, sTarget, 1);
	const string& token = getAttrib(attribs, sToken, 2);
	if (token.empty())
		throw Exception("Missing bundle token");

	auto added = Util::toTimeT(getAttrib(attribs, sAdded, 2));
	auto dirDate = Util::toTimeT(getAttrib(attribs, sDate, 3));
	auto b_autoSearch = Util::toBool(Util::toInt(getAttrib(attribs, sAddedByAutoSearch, 4)));
	const string& prio = getAttrib(attribs, sPriority, 4);
	if (added == 0) {
		added = GET_TIME();
	}

	auto b_resumeTime = Util::toTimeT(getAttrib(attribs, sResumeTime, 5));
	auto finished = Util::toTimeT(getAttrib(attribs, sTimeFinished, 5));

	if (ConnectionManager::getInstance()->tokens.addToken(token, CONNECTION_TYPE_DOWNLOAD)) {
		curBundle = make_shared<Bundle>(bundleTarget, added, !prio.empty() ? validatePrio(prio) : Priority::DEFAULT, dirDate, Util::toUInt32(token), false);
		curBundle->setTimeFinished(finished);
		curBundle->setAddedByAutoSearch(b_autoSearch);
		curBundle->setResumeTime(b_resumeTime);
	} else {
		throw Exception("Duplicate bundle token");
	}

	inDirBundle = true;
}


void QueueLoader::loadFileBundle(StringPairList& attribs, bool) {
	bundleVersion = Util::toInt(getAttrib(attribs, sVersion, 0));
	if (bundleVersion == 0 || bundleVersion > Util::toInt(FILE_BUNDLE_VERSION))
		throw Exception("Non-supported file bundle version");

	{
		const string& token = getAttrib(attribs, sToken, 1);
		if (token.empty())
			throw Exception("Missing bundle token");

		FileBundleInfo info;
		info.token = Util::toUInt32(token);
		info.date = Util::toTimeT(getAttrib(attribs, sDate, 2));
		info.addedByAutosearch = Util::toBool(Util::toInt(getAttrib(attribs, sAddedByAutoSearch, 3)));
		info.resumeTime = Util::toTimeT(getAttrib(attribs, sResumeTime, 4));
		curFileBundleInfo = std::move(info);
	}

	inFileBundle = true;
}

void QueueLoader::loadQueueFile(StringPairList& attribs, bool simple) {
	auto size = Util::toInt64(getAttrib(attribs, sSize, 1));
	if (size == 0)
		return;

	string currentFileTarget;
	try {
		const string& tgt = getAttrib(attribs, sTarget, 0);
		// @todo do something better about existing files
		currentFileTarget = QueueManager::checkTarget(tgt);
		if (currentFileTarget.empty())
			return;
	} catch (const Exception&) {
		return;
	}

	if (curBundle && inDirBundle && !AirUtil::isParentOrExactLocal(curBundle->getTarget(), currentFileTarget)) {
		//the file isn't inside the main bundle dir, can't add this
		return;
	}

	auto added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 2)));
	if (added == 0)
		added = GET_TIME();

	const string& tthRoot = getAttrib(attribs, sTTH, 3);
	if (tthRoot.empty())
		return;

	auto p = validatePrio(getAttrib(attribs, sPriority, 4));

	auto tempTarget = getAttrib(attribs, sTempTarget, 5);
	auto maxSegments = (uint8_t)Util::toInt(getAttrib(attribs, sMaxSegments, 5));

	if (Util::toInt(getAttrib(attribs, sAutoPriority, 6)) == 1) {
		p = Priority::DEFAULT;
	}

	WLock l(qm->cs);
	auto ret = qm->fileQueue.add(currentFileTarget, size, 0, p, tempTarget, added, TTHValue(tthRoot));
	if (ret.second) {
		auto qi = ret.first;
		qi->setMaxSegments(max((uint8_t)1, maxSegments));

		// Bundles
		if (curBundle && inDirBundle) {
			qm->bundleQueue.addBundleItem(qi, curBundle);
		} else if (inLegacyQueue) {
			createFileBundle(qi, Util::rand());
		} else if (inFileBundle) {
			createFileBundle(qi, curFileBundleInfo.token);
		}
	}

	if (!simple)
		curFile = ret.first;
}

void QueueLoader::loadFinishedFile(StringPairList& attribs, bool) {
	//log("FOUND FINISHED TTH");
	const string& target = getAttrib(attribs, sTarget, 0);
	auto size = Util::toInt64(getAttrib(attribs, sSize, 1));
	auto added = Util::toTimeT(getAttrib(attribs, sAdded, 2));
	const string& tth = getAttrib(attribs, sTTH, 3);
	auto finished = Util::toTimeT(getAttrib(attribs, sTimeFinished, 4));
	const string& lastsource = getAttrib(attribs, sLastSource, 5);

	if (size == 0 || tth.empty() || target.empty() || added == 0)
		return;
	if (!Util::fileExists(target))
		return;

	WLock l(qm->cs);
	auto ret = qm->fileQueue.add(target, size, 0, Priority::DEFAULT, Util::emptyString, added, TTHValue(tth));
	if (!ret.second) {
		return;
	}

	auto& qi = ret.first;
	qi->setStatus(QueueItem::STATUS_COMPLETED);
	qi->addFinishedSegment(Segment(0, size)); //make it complete
	qi->setTimeFinished(finished);
	qi->setLastSource(lastsource);

	if (curBundle && inDirBundle) {
		qm->bundleQueue.addBundleItem(qi, curBundle);
	} else if (inFileBundle) {
		createFileBundle(qi, curFileBundleInfo.token);
	}
}

void QueueLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	if (!inLegacyQueue && name == "Downloads") {
		inLegacyQueue = true;
	} else if (!inFileBundle && name == sFile) {
		loadFileBundle(attribs, simple);
	} else if (!inDirBundle && name == sBundle) {
		loadDirectoryBundle(attribs, simple);
	} else if (inLegacyQueue || inDirBundle || inFileBundle) {
		if (!curFile && name == sDownload) {
			loadQueueFile(attribs, simple);
		} else if(curFile && name == sSegment) {
			auto start = Util::toInt64(getAttrib(attribs, sStart, 0));
			auto size = Util::toInt64(getAttrib(attribs, sSize, 1));
			
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
			const string& nick = getAttrib(attribs, sNick, 1);
			const string& hubHint = getAttrib(attribs, sHubHint, 2);

			auto cm = ClientManager::getInstance();
			auto user = cm->loadUser(cid, hubHint, nick);
			if (user == nullptr) {
				return;
			}

			try {
				if (hubHint.empty()) {
					throw QueueException(nick + ": " + STRING(HUB_UNKNOWN));
				}

				HintedUser hintedUser(user, hubHint);
				WLock l(qm->cs);
				qm->addValidatedSource(curFile, hintedUser, 0);
			} catch (const Exception& e) {
				qm->log(STRING_F(SOURCE_ADD_ERROR, e.what()), LogMessage::SEV_WARNING);
				return;
			}
		} else if (name == sFinished && (inDirBundle || inFileBundle)) {
			loadFinishedFile(attribs, simple);
		} else {
			//log("QUEUE LOADING ERROR");
		}
	}
}

void QueueLoader::endTag(const string& name) {
	if (inLegacyQueue || inDirBundle || inFileBundle) {
		if (name == "Downloads") {
			inLegacyQueue = false;
		} else if(name == sBundle) {
			// Directory bundle
			ScopedFunctor([this] { curBundle = nullptr; });
			inDirBundle = false;
			if (!curBundle || curBundle->isEmpty()) {
				throw Exception(STRING_F(NO_FILES_WERE_LOADED, curBundle->getTarget()));
			} else {
				qm->addLoadedBundle(curBundle);
			}
		} else if(name == sFile) {
			ScopedFunctor([this] { curBundle = nullptr; });
			// File bundle
			curFileBundleInfo = FileBundleInfo();
			inFileBundle = false;
			if (!curBundle || curBundle->isEmpty())
				throw Exception(STRING(NO_FILES_FROM_FILE));

			qm->addLoadedBundle(curBundle);
		} else if(name == sDownload) {
			// Queue file
			if (inLegacyQueue && curBundle && curBundle->isFileBundle()) {
				// Only when migrating an old queue
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
	if (!SETTING(KEEP_LISTS)) {
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

		for (const auto& q: matches) {
			if (!q->getBundle())
				continue;

			// Size compare to avoid popular spoof
			if ((SETTING(AUTO_ADD_SOURCE) || (q->getBundle()->getLastSearch() != 0 && static_cast<uint64_t>(q->getBundle()->getLastSearch() + 15*60*1000) > GET_TICK())) && q->getSize() == sr->getSize() && !q->isSource(sr->getUser())) {
				if (q->getBundle()->isDownloaded()) {
					continue;
				}

				if (q->isDownloaded() && q->getBundle()->isSource(sr->getUser())) {
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
			if (boost::find_if(rl, [&sr](const SearchResultPtr& aSR) { return aSR->getUser() == sr->getUser() && aSR->getAdcPath() == sr->getAdcPath(); }) != rl.end()) {
				//don't add the same result multiple times, makes the counting more reliable
				return;
			}
			rl.push_back(sr);
		}
		delayEvents.addEvent(selQI->getToken(), [=] { pickMatchHooked(selQI); }, 2000);
	}
}

void QueueManager::pickMatchHooked(QueueItemPtr qi) noexcept {
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
		matchBundleHooked(qi, sr);
	}
}

void QueueManager::matchBundleHooked(const QueueItemPtr& aQI, const SearchResultPtr& aResult) noexcept {
	if (aQI->getBundle()->isFileBundle()) {
		// No reason to match anything with file bundles
		addSourcesHooked(aResult->getUser(), { aQI }, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
		return;
	} 

	auto isNmdc = aResult->getUser().user->isNMDC();

	auto path = AirUtil::getAdcMatchPath(aResult->getAdcPath(), aQI->getTarget(), aQI->getBundle()->getTarget(), isNmdc);
	if (!path.empty()) {
		if (isNmdc) {
			// A NMDC directory bundle, just add the sources without matching
			QueueItemList ql;

			{
				RLock l(cs);
				aQI->getBundle()->getDirQIs(path, ql);
			}

			auto newFiles = addSourcesHooked(aResult->getUser(), ql, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);

			if (SETTING(REPORT_ADDED_SOURCES) && newFiles > 0) {
				log(ClientManager::getInstance()->getFormatedNicks(aResult->getUser()) + ": " + 
					STRING_F(MATCH_SOURCE_ADDED, newFiles % aQI->getBundle()->getName().c_str()), LogMessage::SEV_INFO);
			}
		} else {
			//An ADC directory bundle, match recursive partial list
			try {
				auto info = FilelistAddData(aResult->getUser(), this, path);
				addListHooked(info, QueueItem::FLAG_MATCH_QUEUE | QueueItem::FLAG_RECURSIVE_LIST | QueueItem::FLAG_PARTIAL_LIST, aQI->getBundle());
			} catch(...) { }
		}
	} else if (SETTING(ALLOW_MATCH_FULL_LIST)) {
		// No path to match, use full filelist
		dcassert(isNmdc);
		try {
			auto info = FilelistAddData(aResult->getUser(), this, ADC_ROOT_STR);
			addListHooked(info, QueueItem::FLAG_MATCH_QUEUE, aQI->getBundle());
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
			fire(QueueManagerListener::ItemSources(), q);
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
		fire(QueueManagerListener::ItemSources(), q); 

	for (const auto& b : bl)
		fire(QueueManagerListener::BundleSources(), b);
}

void QueueManager::calculatePriorities(uint64_t aTick) noexcept {
	auto prioType = SETTING(AUTOPRIO_TYPE);
	if (prioType == SettingsManager::PRIO_DISABLED) {
		return;
	}

	if (lastAutoPrio != 0 && (lastAutoPrio + (SETTING(AUTOPRIO_INTERVAL) * 1000) > aTick)) {
		return;
	}

	vector<pair<QueueItemPtr, Priority>> qiPriorities;
	vector<pair<BundlePtr, Priority>> bundlePriorities;

	{
		RLock l(cs);

		// bundles
		for (const auto& b : bundleQueue.getBundles() | map_values) {
			if (b->isDownloaded()) {
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
				if (p1 != Priority::PAUSED && p1 != Priority::PAUSED_FORCE) {
					auto p2 = q->calculateAutoPriority();
					if (p1 != p2)
						qiPriorities.emplace_back(q, p2);
				}
			}
		}
	}

	if (prioType == SettingsManager::PRIO_BALANCED) {
		//log("Calculate autoprio (balanced)");
		calculateBundlePriorities(false);
		setLastAutoPrio(aTick);
	} else {
		//log("Calculate autoprio (progress)");
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
			if (b->isDownloaded()) {
				continue;
			}

			if (b->getResumeTime() > 0 && GET_TIME() > b->getResumeTime()) {
				resumeBundles.push_back(b);
			}

			//check if we have free space to continue the download...
			if (b->getStatus() == Bundle::STATUS_DOWNLOAD_ERROR) {
				if (File::getFreeSpace(b->getTarget()) >= static_cast<int64_t>(b->getSize() - b->getDownloadedBytes())) {
					resumeBundles.push_back(b);
				}
			}
		}
	}

	for (auto& b : resumeBundles) {

		if (b->getStatus() == Bundle::STATUS_DOWNLOAD_ERROR)
			setBundleStatus(b, Bundle::STATUS_QUEUED);

		setBundlePriority(b, Priority::DEFAULT, false);
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
			fire(QueueManagerListener::ItemTick(), q);
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
static void calculateBalancedPriorities(vector<pair<T, Priority>>& priorities, multimap<T, pair<int64_t, double>>& speedSourceMap, bool verbose) noexcept {
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
			LogManager::getInstance()->message("Not enough items with unique points to perform the priotization!", LogMessage::SEV_INFO, "Debug");
		}
		return;
	} else if (uniqueValues > 2) {
		prioGroup = uniqueValues / 3;
	}

	if (verbose) {
		LogManager::getInstance()->message("Unique values: " + Util::toString(uniqueValues) + " prioGroup size: " + Util::toString(prioGroup), LogMessage::SEV_INFO, "Debug");
	}


	//start with the high prio, continue to normal and low
	auto prio = static_cast<int>(Priority::HIGH);

	//counters for analyzing identical points
	double lastPoints = 999.0;
	int prioSet=0;

	for (auto& i: finalMap) {
		Priority newItemPrio;

		if (lastPoints==i.first) {
			newItemPrio = static_cast<Priority>(prio);

			// Don't increase the prio if two items have identical points
			if (prioSet < prioGroup) {
				prioSet++;
			}
		} else {
			// All priorities set from this group? but don't go below LOW
			if (prioSet == prioGroup && prio != static_cast<int>(Priority::LOW)) {
				prio--;
				prioSet = 0;
			} 

			newItemPrio = static_cast<Priority>(prio);

			prioSet++;
			lastPoints = i.first;
		}

		if (verbose) {
			LogManager::getInstance()->message(i.second->getTarget() + " points: " + Util::toString(i.first) + " using prio " + AirUtil::getPrioText(newItemPrio), LogMessage::SEV_INFO, "Debug");
		}

		if (i.second->getPriority() != newItemPrio) {
			priorities.emplace_back(i.second, newItemPrio);
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
			if (b->isDownloaded()) {
				continue;
			}

			if (b->getAutoPriority()) {
				bundleSpeedSourceMap.emplace(b, b->getPrioInfo());
			}

			if (SETTING(QI_AUTOPRIO)) {
				qiMaps.push_back(b->getQIBalanceMaps());
			}
		}
	}

	vector<pair<BundlePtr, Priority>> bundlePriorities;
	calculateBalancedPriorities<BundlePtr>(bundlePriorities, bundleSpeedSourceMap, verbose);

	for(auto& p: bundlePriorities) {
		setBundlePriority(p.first, p.second, true);
	}


	if (SETTING(QI_AUTOPRIO)) {

		vector<pair<QueueItemPtr, Priority>> qiPriorities;
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

bool QueueManager::handlePartialResultHooked(const HintedUser& aUser, const TTHValue& aTTH, const QueueItem::PartialSource& aPartialSource, PartsInfo& outPartialInfo_) noexcept {
	bool wantConnection = false;
	dcassert(outPartialInfo_.empty());
	QueueItemPtr qi = nullptr;

	// Locate target QueueItem in download queue
	{
		QueueItemList ql;

		RLock l(cs);
		fileQueue.findFiles(aTTH, ql);
		
		if(ql.empty()){
			dcdebug("Not found in download queue\n");
			return false;
		}
		
		qi = ql.front();

		// don't add sources to finished files
		// this could happen when "Keep finished files in queue" is enabled
		if (qi->isDownloaded()) {
			return false;
		}
	}

	// Check min size
	if (qi->getSize() < PARTIAL_SHARE_MIN_SIZE){
		dcassert(0);
		return false;
	}

	// Check source
	try {
		checkSourceHooked(aUser, nullptr);
	} catch (const QueueException& e) {
		log(STRING_F(SOURCE_ADD_ERROR, e.what()), LogMessage::SEV_WARNING);
		return false;
	}

	// Get my parts info
	int64_t blockSize = qi->getBlockSize();

	{
		WLock l(cs);
		qi->getPartialInfo(outPartialInfo_, blockSize);
		
		// Any parts for me?
		wantConnection = qi->isNeededPart(aPartialSource.getPartialInfo(), blockSize);

		// If this user isn't a source and has no parts needed, ignore it
		auto si = qi->getSource(aUser);
		if(si == qi->getSources().end()) {
			si = qi->getBadSource(aUser);

			if (si != qi->getBadSources().end() && si->isSet(QueueItem::Source::FLAG_TTH_INCONSISTENCY)) {
				return false;
			}

			if (!wantConnection) {
				if (si == qi->getBadSources().end()) {
					return false;
				}
			} else {
				// add this user as partial file sharing source
				qi->addSource(aUser);
				si = qi->getSource(aUser);
				si->setFlag(QueueItem::Source::FLAG_PARTIAL);

				auto ps = make_shared<QueueItem::PartialSource>(aPartialSource.getMyNick(),
					aPartialSource.getHubIpPort(), aPartialSource.getIp(), aPartialSource.getUdpPort());
				si->setPartialSource(ps);

				userQueue.addQI(qi, aUser);
				dcassert(si != qi->getSources().end());
			}
		}

		// Update source's parts info
		if (si->getPartialSource()) {
			si->getPartialSource()->setPartialInfo(aPartialSource.getPartialInfo());
		}
	}
	
	// Connect to this user
	if (wantConnection) {
		fire(QueueManagerListener::ItemSources(), qi);

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
			_reply = !b->isDownloaded() && !b->isFinishedNotified(aUser);

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

StringList QueueManager::getAdcDirectoryPaths(const string& aDirName) const noexcept {
	RLock l(cs);
	return bundleQueue.getAdcDirectoryPaths(aDirName);
}

void QueueManager::getBundlePaths(OrderedStringSet& retBundles) const noexcept {
	RLock l(cs);
	for (const auto& b : bundleQueue.getBundles() | map_values) {
		retBundles.insert(b->getTarget());
	}
}

void QueueManager::on(ShareManagerListener::RefreshCompleted, const ShareRefreshTask& aTask, bool aSucceed, const ShareRefreshStats&) noexcept {
	if (!aSucceed) {
		return;
	}

	if (aTask.type == ShareRefreshType::REFRESH_ALL) {
		checkCompletedBundles(Util::emptyString, false);
	} else {
		for (const auto& p : aTask.dirs) {
			checkCompletedBundles(p, false);
		}
	}
}

void QueueManager::checkCompletedBundles(const string& aPath, bool aValidateCompleted) noexcept{
	BundleList bundles;

	{
		RLock l(cs);
		for (auto& b : bundleQueue.getBundles() | map_values) {
			if (b->isCompleted() && AirUtil::isParentOrExactLocal(aPath, b->getTarget())) {
				bundles.push_back(b);
			}
		}
	}

	for (auto& b: bundles) {
		if (ShareManager::getInstance()->isRealPathShared(b->getTarget())) {
			setBundleStatus(b, Bundle::STATUS_SHARED);
		} else if (aValidateCompleted) {
			// In case it's a failed bundle
			shareBundle(b, false);
		}
	}
}

void QueueManager::setBundleStatus(const BundlePtr& aBundle, Bundle::Status aNewStatus) noexcept {
	if (aBundle->getStatus() != aNewStatus) {
		if (!Bundle::isFailedStatus(aNewStatus)) {
			aBundle->setHookError(nullptr);
		}

		aBundle->setStatus(aNewStatus);
		fire(QueueManagerListener::BundleStatusChanged(), aBundle);
	}
}

void QueueManager::setFileStatus(const QueueItemPtr& aFile, QueueItem::Status aNewStatus) noexcept {
	if (aFile->getStatus() != aNewStatus) {
		if (!QueueItem::isFailedStatus(aNewStatus)) {
			aFile->setHookError(nullptr);
		}

		aFile->setStatus(aNewStatus);
		fire(QueueManagerListener::ItemStatus(), aFile);
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
	target = qi->isDownloaded() ? qi->getTarget() : qi->getTempTarget();

	return qi->isChunkDownloaded(startPos, bytes);
}

void QueueManager::getSourceInfo(const UserPtr& aUser, Bundle::SourceBundleList& aSources, Bundle::SourceBundleList& aBad) const noexcept {
	RLock l(cs);
	bundleQueue.getSourceInfo(aUser, aSources, aBad);
}

int QueueManager::addSourcesHooked(const HintedUser& aUser, const QueueItemList& aItems, Flags::MaskType aAddBad) noexcept {
	try {
		checkSourceHooked(aUser, nullptr);
	} catch (const QueueException& e) {
		log(STRING_F(SOURCE_ADD_ERROR, e.what()), LogMessage::SEV_WARNING);
		return 0;
	}

	BundleList bundles;
	return addValidatedSources(aUser, aItems, aAddBad, bundles);
}

int QueueManager::addValidatedSources(const HintedUser& aUser, const QueueItemList& aItems, Flags::MaskType aAddBad) noexcept {
	BundleList bundles;
	return addValidatedSources(aUser, aItems, aAddBad, bundles);
}

int QueueManager::addValidatedSources(const HintedUser& aUser, const QueueItemList& aItems, Flags::MaskType aAddBad, BundleList& matchingBundles_) noexcept {
	bool wantConnection = false;

	QueueItemList addedItems;
	{
		// Add sources

		WLock l(cs);
		boost::algorithm::copy_if(aItems, back_inserter(addedItems), [&](const QueueItemPtr& q) {
			if (q->getBundle() && find(matchingBundles_, q->getBundle()) == matchingBundles_.end()) {
				matchingBundles_.push_back(q->getBundle());
			}

			try {
				if (addValidatedSource(q, aUser, aAddBad)) {
					wantConnection = true;
				}

				return false;
			} catch (const QueueException&) {
				// Ignore...
			}

			return true;
		});
	}

	if (!addedItems.empty()) {
		// Speakers
		for (const auto& qi : addedItems) {
			fire(QueueManagerListener::ItemSources(), qi);
		}

		for (const auto& b : matchingBundles_) {
			fire(QueueManagerListener::BundleSources(), b);
		}

		fire(QueueManagerListener::SourceFilesUpdated(), aUser);


		// Connect
		if (wantConnection && aUser.user->isOnline()) {
			ConnectionManager::getInstance()->getDownloadConnection(aUser);
		}
	}

	return addedItems.size();
}

void QueueManager::connectBundleSources(const BundlePtr& aBundle) noexcept {
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

void QueueManager::readdBundle(const BundlePtr& aBundle) noexcept {
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
	log(STRING_F(BUNDLE_READDED, aBundle->getName().c_str()), LogMessage::SEV_INFO);
}

DupeType QueueManager::isAdcDirectoryQueued(const string& aDir, int64_t aSize) const noexcept{
	RLock l(cs);
	return bundleQueue.isAdcDirectoryQueued(aDir, aSize);
}

BundlePtr QueueManager::isRealPathQueued(const string& aPath) const noexcept {
	RLock l(cs);
	if (!aPath.empty() && aPath.back() == PATH_SEPARATOR) {
		auto b = bundleQueue.isLocalDirectoryQueued(aPath);
		return b;
	} else {
		auto qi = fileQueue.findFile(aPath);
		return qi ? qi->getBundle() : nullptr;
	}
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

int QueueManager::getFinishedBundlesCount() const noexcept {
	RLock l(cs);
	return static_cast<int>(boost::count_if(bundleQueue.getBundles() | map_values, [&](const BundlePtr& b) { return b->isDownloaded(); }));
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
	//log("QueueManager::sendBundleUpdate");
	BundlePtr b = nullptr;
	{
		RLock l(cs);
		b = bundleQueue.findBundle(aBundleToken);
	}

	if (b) {
		if (b->isSet(Bundle::FLAG_UPDATE_SIZE)) {
			fire(QueueManagerListener::BundleSize(), b);
			DownloadManager::getInstance()->sendSizeUpdate(b);
		}
		
		if (b->isSet(Bundle::FLAG_SCHEDULE_SEARCH)) {
			searchBundleAlternates(b);
		}
	}
}
void QueueManager::removeBundleItem(const QueueItemPtr& qi, bool aFinished) noexcept{
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
	} else if (!aFinished ) {
		//Delay event to prevent multiple scans when removing files...
		delayEvents.addEvent(bundle->getToken(), [=] { 
			tasks.addTask([=] {
				if (!checkBundleFinishedHooked(bundle)) {
					bundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
					addBundleUpdate(bundle);
				}
			});
		}, 3000);
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

void QueueManager::removeBundle(const BundlePtr& aBundle, bool aRemoveFinishedFiles) noexcept{
	if (aBundle->getStatus() == Bundle::STATUS_NEW) {
		return;
	}

	UserList sources;
	StringList deleteFiles;

	DownloadManager::getInstance()->disconnectBundle(aBundle);
	fire(QueueManagerListener::BundleRemoved(), aBundle);

	bool isCompleted = false;

	{
		WLock l(cs);
		isCompleted = aBundle->isCompleted();

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

			if (!qi->isDownloaded()) {
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
	if (!aBundle->isFileBundle() && (aRemoveFinishedFiles || !isCompleted)) { // IMPORTANT: avoid disk access when cleaning up finished bundles so don't remove the finished check
		if (!AirUtil::removeDirectoryIfEmpty(aBundle->getTarget(), 10) && !aRemoveFinishedFiles) {
			log(STRING_F(DIRECTORY_NOT_REMOVED, aBundle->getTarget()), LogMessage::SEV_INFO);
		}
	}

	if (!isCompleted) {
		log(STRING_F(BUNDLE_X_REMOVED, aBundle->getName()), LogMessage::SEV_INFO);
	}

	for (const auto& aUser : sources)
		fire(QueueManagerListener::SourceFilesUpdated(), aUser);

	removeBundleLists(aBundle);
}

void QueueManager::removeBundleLists(const BundlePtr& aBundle) noexcept{
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

MemoryInputStream* QueueManager::generateTTHList(QueueToken aBundleToken, bool isInSharingHub, BundlePtr& bundle_) {
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
				if (q->isDownloaded()) {
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

void QueueManager::addBundleTTHListHooked(const HintedUser& aUser, const string& aRemoteBundleToken, const TTHValue& aTTH) {
	//log("ADD TTHLIST");
	auto b = findBundle(aTTH);
	if (b) {
		auto info = FilelistAddData(aUser, this, aRemoteBundleToken);
		addListHooked(info, (QueueItem::FLAG_TTHLIST_BUNDLE | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_MATCH_QUEUE), b);
	}
}

bool QueueManager::checkPBDReply(HintedUser& aUser, const TTHValue& aTTH, string& _bundleToken, bool& _notify, bool& _add, const string& remoteBundle) noexcept {
	BundlePtr bundle = findBundle(aTTH);
	if (bundle) {
		WLock l(cs);
		//log("checkPBDReply: BUNDLE FOUND");
		_bundleToken = bundle->getStringToken();
		_add = !bundle->getFinishedFiles().empty();

		if (!bundle->isDownloaded()) {
			bundle->addFinishedNotify(aUser, remoteBundle);
			_notify = true;
		}
		return true;
	}
	//log("checkPBDReply: CHECKNOTIFY FAIL");
	return false;
}

void QueueManager::addFinishedNotify(HintedUser& aUser, const TTHValue& aTTH, const string& remoteBundle) noexcept {
	BundlePtr bundle = findBundle(aTTH);
	if (bundle) {
		WLock l(cs);
		//log("addFinishedNotify: BUNDLE FOUND");
		if (!bundle->isDownloaded()) {
			bundle->addFinishedNotify(aUser, remoteBundle);
		}
	}
	//log("addFinishedNotify: BUNDLE NOT FOUND");
}

void QueueManager::removeBundleNotify(const UserPtr& aUser, QueueToken aBundleToken) noexcept {
	WLock l(cs);
	BundlePtr bundle = bundleQueue.findBundle(aBundleToken);
	if (bundle) {
		bundle->removeFinishedNotify(aUser);
	}
}

void QueueManager::updatePBDHooked(const HintedUser& aUser, const TTHValue& aTTH) noexcept {
	QueueItemList qiList;

	{
		RLock l(cs);
		fileQueue.findFiles(aTTH, qiList);
	}

	addSourcesHooked(aUser, qiList, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
}

bool QueueManager::autoSearchEnabled() noexcept {
	return SETTING(AUTO_SEARCH) && SETTING(AUTO_ADD_SOURCE);
}

void QueueManager::searchAlternates(uint64_t aTick) noexcept {
	if (!autoSearchEnabled() || ClientManager::getInstance()->hasSearchQueueOverflow()) {
		return;
	}

	BundlePtr bundle;

	// Get the item to search for
	{
		WLock l(cs);
		bundle = bundleQueue.maybePopSearchItem(aTick);
	}

	if (!bundle) {
		return;
	}

	// Perform the search
	searchBundleAlternates(bundle, aTick);
}

int QueueManager::searchBundleAlternates(const BundlePtr& aBundle, uint64_t aTick) noexcept {
	QueueItemList searchItems;
	bool isScheduled = false;
	// Get the possible items to search for
	{
		RLock l(cs);
		isScheduled = aBundle->isSet(Bundle::FLAG_SCHEDULE_SEARCH);

		aBundle->unsetFlag(Bundle::FLAG_SCHEDULE_SEARCH);

		if (isScheduled && !aBundle->allowAutoSearch())
			return 0;

		searchItems = bundleQueue.getSearchItems(aBundle);
	}

	if (searchItems.empty()) {
		return 0;
	}

	// Perform the searches
	int queuedFileSearches = 0;
	for (const auto& q : searchItems) {
		auto success = !searchFileAlternates(q).queuedHubUrls.empty();
		if (success) {
			queuedFileSearches++;
		}
	}

	if (queuedFileSearches > 0) {
		aBundle->setLastSearch(aTick);

		uint64_t nextSearchTick = 0;
		if (autoSearchEnabled()) {
			RLock l(cs);
			
			if (isScheduled)
				bundleQueue.recalculateSearchTimes(aBundle->isRecent(), true, aTick);

			nextSearchTick = aBundle->isRecent() ? bundleQueue.getNextSearchRecent() : bundleQueue.getNextSearchNormal();
		}

		if (SETTING(REPORT_ALTERNATES)) {
			if (nextSearchTick == 0 || aTick >= nextSearchTick) {
				log(STRING_F(BUNDLE_ALT_SEARCH, aBundle->getName().c_str() % queuedFileSearches), LogMessage::SEV_INFO);
			} else {
				auto nextSearchMinutes = (nextSearchTick - aTick) / (60 * 1000);
				if (aBundle->isRecent()) {
					log(STRING_F(BUNDLE_ALT_SEARCH_RECENT, aBundle->getName() % queuedFileSearches) +
						" " + STRING_F(NEXT_RECENT_SEARCH_IN, nextSearchMinutes), LogMessage::SEV_INFO);
				} else {
					log(STRING_F(BUNDLE_ALT_SEARCH, aBundle->getName() % queuedFileSearches) +
						" " + STRING_F(NEXT_SEARCH_IN, nextSearchMinutes), LogMessage::SEV_INFO);
				}
			}
		}
	}

	return queuedFileSearches;
}

SearchQueueInfo QueueManager::searchFileAlternates(const QueueItemPtr& aQI) const noexcept {
	auto s = make_shared<Search>(Priority::LOW, "qa");
	s->query = aQI->getTTH().toBase32();
	s->fileType = Search::TYPE_TTH;

	return SearchManager::getInstance()->search(s);
}

void QueueManager::onUseSeqOrder(const BundlePtr& b) noexcept {
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
