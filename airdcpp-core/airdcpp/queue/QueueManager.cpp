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
#include <airdcpp/queue/QueueManager.h>

#include <airdcpp/util/AutoLimitUtil.h>
#include <airdcpp/queue/Bundle.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/DCPlusPlus.h>
#include <airdcpp/protocol/ProtocolCommandManager.h>
#include <airdcpp/filelist/DirectoryListing.h>
#include <airdcpp/filelist/DirectoryListingManager.h>
#include <airdcpp/transfer/download/Download.h>
#include <airdcpp/transfer/download/DownloadManager.h>
#include <airdcpp/core/classes/ErrorCollector.h>
#include <airdcpp/core/io/FileReader.h>
#include <airdcpp/hash/HashManager.h>
#include <airdcpp/hash/HashedFile.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/core/classes/ScopedFunctor.h>
#include <airdcpp/search/SearchManager.h>
#include <airdcpp/search/SearchResult.h>
#include <airdcpp/core/io/SFVReader.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/core/io/xml/SimpleXMLReader.h>
#include <airdcpp/core/io/stream/Streams.h>
#include <airdcpp/util/SystemUtil.h>
#include <airdcpp/transfer/Transfer.h>
#include <airdcpp/transfer/upload/UploadManager.h>
#include <airdcpp/connection/UserConnection.h>
#include <airdcpp/util/ValueGenerator.h>
#include <airdcpp/core/io/compress/ZUtils.h>
#include <airdcpp/core/version.h>

#include <boost/range/algorithm_ext/for_each.hpp>

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

QueueManager::QueueManager() : 
	tasks(true),
	udp(make_unique<Socket>(Socket::TYPE_UDP))
{ 
	//add listeners in loadQueue
	File::ensureDirectory(AppUtil::getListPath());
	File::ensureDirectory(AppUtil::getBundlePath());

	SettingsManager::getInstance()->registerChangeHandler({ 
		SettingsManager::HIGH_PRIO_FILES, SettingsManager::HIGHEST_PRIORITY_USE_REGEXP, 
		SettingsManager::SKIPLIST_DOWNLOAD, SettingsManager::DOWNLOAD_SKIPLIST_USE_REGEXP 
	}, [this](auto ...) {
		setMatchers();
	});
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
		for (const auto& b : bundleQueue.getBundles() | views::values) {
			if (b->isCompleted()) {
				bl.push_back(b);
			}
		}
		ranges::for_each(bl, [=, this](const BundlePtr& b) { bundleQueue.removeBundle(b); });
	}

	saveQueue(false);

	if (!SETTING(KEEP_LISTS)) {
		string path = AppUtil::getListPath();

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
	for (const auto& q : ql) {
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

void QueueManager::recheckFiles(const QueueItemList& aQL) noexcept {
	log(STRING_F(INTEGRITY_CHECK_START_FILES, aQL.size()), LogMessage::SEV_INFO);

	QueueItemList failedItems;
	int64_t failedBytes = 0;
	for (const auto& q : aQL) {
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
		checkTarget = PathUtil::fileExists(q->getTarget()) ? q->getTarget() : q->getTempTarget();
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

/*void QueueManager::getBloom(HashBloom& bloom) const noexcept {
	RLock l(cs);
	fileQueue.getBloom(bloom);
}*/

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

DirectoryContentInfo QueueManager::getBundleContent(const BundlePtr& aBundle) const noexcept {
	RLock l(cs);
	auto files = static_cast<int>(aBundle->getQueueItems().size() + aBundle->getFinishedFiles().size());
	auto directories = static_cast<int>(aBundle->isFileBundle() ? 0 : bundleQueue.getDirectoryCount(aBundle) - 1);
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
	if (!(aFlags & QueueItem::FLAG_TTHLIST_BUNDLE) && !PathUtil::isAdcDirectoryPath(aListData.listPath)) {
		throw QueueException(STRING_F(INVALID_PATH, aListData.listPath));
	}

	// Pre-checks
	checkSourceHooked(aListData.user, aListData.caller);

	// Format the target
	auto target = getListPath(aListData.user);
	if (aFlags & QueueItem::FLAG_PARTIAL_LIST) {
		target += ".partial[" + PathUtil::validateFileName(aListData.listPath) + "]";
	}


	// Add in queue

	QueueItemPtr q = nullptr;
	{
		WLock l(cs);
		auto [qi, added] = fileQueue.add(target, -1, (QueueItem::FLAG_USER_LIST | aFlags), Priority::HIGHEST, aListData.listPath, GET_TIME(), TTHValue());
		if (!added) {
			//exists already
			throw DupeException(STRING(LIST_ALREADY_QUEUED));
		}

		q = std::move(qi);
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
	string nick = nicks.empty() ? Util::emptyString : PathUtil::validateFileName(nicks[0]) + ".";
	return AppUtil::getListPath() + nick + user.user->getCID().toBase32();
}

bool QueueManager::checkRemovedTarget(const QueueItemPtr& q, int64_t aSize, const TTHValue& aTTH) {
	if (q->isDownloaded()) {
		/* The target file doesn't exist, add our item. Also recheck the existence in case of finished files being moved on the same time. */
		dcassert(q->getBundle());
		if (!PathUtil::fileExists(q->getTarget()) && q->getBundle() && q->isCompleted()) {
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

void QueueManager::checkSourceHooked(const HintedUser& aUser, CallerPtr aCaller) const {
	if (!aUser.user) { //atleast magnet links can cause this to happen.
		throw QueueException(STRING(UNKNOWN_USER));
	}

	if (aUser.hint.empty()) {
		dcassert(0);
		throw QueueException(ClientManager::getInstance()->getFormattedNicks(aUser) + ": " + STRING(HUB_UNKNOWN));
	}

	// Check that we're not downloading from ourselves...
	if (aUser.user == ClientManager::getInstance()->getMe()) {
		throw QueueException(STRING(NO_DOWNLOADS_FROM_SELF));
	}

	// Check the encryption
	if (aUser.user->isOnline() && !aUser.user->isNMDC() && !aUser.user->isSet(User::TLS) && SETTING(TLS_MODE) == SettingsManager::TLS_FORCED) {
		throw QueueException(ClientManager::getInstance()->getFormattedNicks(aUser) + ": " + STRING(SOURCE_NO_ENCRYPTION));
	}

	{
		auto error = sourceValidationHook.runHooksError(aCaller, aUser);
		if (error) {
			throw QueueException(ActionHookRejection::formatError(error));
		}
	}
}

void QueueManager::validateBundleFileHooked(const string& aBundleDir, BundleFileAddData& fileInfo_, CallerPtr aCaller, Flags::MaskType aFlags/*=0*/) const {
	if (fileInfo_.size <= 0) {
		throw QueueException(STRING(ZERO_BYTE_QUEUE));
	}

	auto matchSkipList = [&] (const string& aName) {
		if(skipList.match(aName)) {
			throw QueueException(STRING(SKIPLIST_DOWNLOAD_MATCH));
		}
	};

	// Check the skiplist
	// No skiplist for private (magnet) downloads
	if (!(aFlags & QueueItem::FLAG_PRIVATE)) {
		// Match the file name
		matchSkipList(PathUtil::getFileName(fileInfo_.name));

		// Match all subdirectories (if any)
		string::size_type i = 0, j = 0;
		while ((i = fileInfo_.name.find(PATH_SEPARATOR, j)) != string::npos) {
			matchSkipList(fileInfo_.name.substr(j, i - j));
			j = i + 1;
		}
	}

	// Validate the target and check the existence
	fileInfo_.name = checkTarget(fileInfo_.name, aBundleDir);

	// Check share dupes
	if (SETTING(DONT_DL_ALREADY_SHARED) && ShareManager::getInstance()->isFileShared(fileInfo_.tth)) {
		auto paths = ShareManager::getInstance()->getRealPaths(fileInfo_.tth);
		if (!paths.empty()) {
			auto path = PathUtil::subtractCommonDirectories(aBundleDir, PathUtil::getFilePath(paths.front()));
			throw DupeException(STRING_F(TTH_ALREADY_SHARED, path));
		}
	}

	// Check queue dupes
	if (SETTING(DONT_DL_ALREADY_QUEUED)) {
		RLock l(cs);
		auto q = fileQueue.getQueuedFile(fileInfo_.tth);
		if (q && q->getTarget() != aBundleDir + fileInfo_.name) {
			auto path = PathUtil::subtractCommonDirectories(aBundleDir, q->getFilePath());
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
	if (fileInfo_.prio == Priority::DEFAULT && highPrioFiles.match(PathUtil::getFileName(fileInfo_.name))) {
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
	auto target = AppUtil::getOpenPath() + ValueGenerator::toOpenFileName(aFileInfo.file, aFileInfo.tth);

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
		tie(qi, added) = fileQueue.add(target, aFileInfo.size, flags, Priority::HIGHEST, Util::emptyString, GET_TIME(), aFileInfo.tth);

		// qi = std::move(ret.first);
		// added = ret.second;

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

BundlePtr QueueManager::getBundle(const string& aTarget, Priority aPrio, time_t aDate, bool isFileBundle) const noexcept {
	auto b = bundleQueue.getMergeBundle(aTarget);
	if (!b) {
		// create a new bundle
		b = make_shared<Bundle>(aTarget, GET_TIME(), aPrio, aDate, 0, true, isFileBundle);
	} else {
		// use an existing one
		dcassert(!PathUtil::isSubLocal(b->getTarget(), aTarget));
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
	target = PathUtil::joinDirectory(formatBundleTarget(target, aDirectory.date), PathUtil::validatePath(aDirectory.name));

	{
		// There can't be existing bundles inside this directory
		BundleList subBundles;
		bundleQueue.getSubBundles(target, subBundles);
		if (!subBundles.empty()) {
			StringList subPaths;
			for (const auto& b : subBundles) {
				subPaths.push_back(b->getTarget());
			}

			errorMsg_ = STRING_F(BUNDLE_ERROR_SUBBUNDLES, subBundles.size() % target % PathUtil::subtractCommonParents(target, subPaths));
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
	int smallDupes = 0, fileCount = static_cast<int>(aFiles.size()), filesExist = 0;

	DirectoryBundleAddResult info;
	ErrorCollector errors(fileCount);

	std::erase_if(aFiles, [&](BundleFileAddData& bfi) {
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
	});


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
		for (auto const& bfi: aFiles) {
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
			log(STRING_F(BUNDLE_MERGED, PathUtil::getLastDir(target) % b->getName() % info.filesAdded), LogMessage::SEV_INFO);
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

		for (const auto& [qi, added] : aItemsAdded) {
			if (added) {
				fire(QueueManagerListener::ItemAdded(), qi);
			} else {
				fire(QueueManagerListener::ItemSources(), qi);
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

void QueueManager::runAddBundleHooksThrow(string& target_, BundleAddData& aDirectory, const HintedUser& aOptionalUser, bool aIsFile) const {
	auto results = bundleValidationHook.runHooksDataThrow(this, target_, aDirectory, aOptionalUser, aIsFile);
	for (const auto& result: results) {
		auto const& data = result->data;

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
	params["username"] = [] { return SystemUtil::getSystemUsername(); };
	
	auto time = (SETTING(FORMAT_DIR_REMOTE_TIME) && aRemoteDate > 0) ? aRemoteDate : GET_TIME();
	auto formattedPath = Util::formatParams(aPath, params, nullptr, time);
	return PathUtil::validatePath(formattedPath);
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
			log(STRING_F(BUNDLE_ITEM_ADDED, PathUtil::getFileName(target) % b->getName()), LogMessage::SEV_INFO);
		}
	}

	return BundleAddInfo(b, oldStatus != Bundle::STATUS_NEW);
}

QueueManager::FileAddInfo QueueManager::addBundleFile(const string& aTarget, int64_t aSize, const TTHValue& aRoot, const HintedUser& aOptionalUser, Flags::MaskType aFlags /* = 0 */,
								   bool addBad /* = true */, Priority aPrio, bool& wantConnection_, const BundlePtr& aBundle)
{
	dcassert(aSize > 0);

	// Add the file
	auto ret = fileQueue.add(aTarget, aSize, aFlags, aPrio, Util::emptyString, GET_TIME(), aRoot);

	if(!ret.second) {
		// Exists already
		if (checkRemovedTarget(ret.first, aSize, aRoot)) {
			ret = fileQueue.add(aTarget, aSize, aFlags, aPrio, Util::emptyString, GET_TIME(), aRoot);
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

	string target = PathUtil::validatePath(toValidate);

	// Check that the file doesn't already exist...
	if (PathUtil::fileExists(aParentDir + target)) {
		/* TODO: add for recheck */
		throw FileException(STRING(TARGET_FILE_EXISTS));
	}
	return target;	
}

/** Add a source to an existing queue item */
bool QueueManager::addValidatedSource(const QueueItemPtr& qi, const HintedUser& aUser, Flags::MaskType aAddBad) {
	if (qi->isDownloaded()) //no need to add source to finished item.
		throw QueueException(STRING(FILE_ALREADY_FINISHED) + ": " + PathUtil::getFileName(qi->getTarget()));
	
	bool wantConnection = !qi->isPausedPrio();
	dcassert(qi->getBundle() || qi->getPriority() == Priority::HIGHEST);

	if (qi->isSource(aUser)) {
		if(qi->isSet(QueueItem::FLAG_USER_LIST)) {
			return wantConnection;
		}
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + PathUtil::getFileName(qi->getTarget()));
	}

	bool isBad = false;
	if (qi->isBadSourceExcept(aUser, aAddBad, isBad)) {
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + PathUtil::getFileName(qi->getTarget()));
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

QueueManager::DownloadResult QueueManager::getDownload(UserConnection& aSource, const QueueTokenSet& aRunningBundles, const OrderedStringSet& aOnlineHubs) noexcept {
	const auto& user = aSource.getUser();

	DownloadResult result;

	QueueItemPtr q = nullptr;
	OptionalTransferSlot slotType;

	{
		// Segments shouldn't be assigned simultaneously for multiple connections
		Lock slotLock(slotAssignCS);

		{
			auto startResult = startDownload(aSource.getHintedUser(), aSource.getDownloadType(), aRunningBundles, aOnlineHubs, &aSource);
			result.merge(startResult);

			if (!startResult.slotType) {
				// dcdebug("none\n");
				return result;
			}

			q = startResult.qi;
			slotType = startResult.slotType;
			dcassert(q);
		}

		{
			WLock l(cs);

			// Check partial sources
			auto source = q->getSource(user);
			if (source->isSet(QueueItem::Source::FLAG_PARTIAL)) {
				auto segment = q->getNextSegment(q->getBlockSize(), aSource.getChunkSize(), aSource.getSpeed(), source->getPartsInfo(), false);
				if (segment.getStart() != -1 && segment.getSize() == 0) {
					// dcdebug("no needed chunks)\n");
					// no other partial chunk from this user, remove him from queue
					userQueue.removeQI(q, user);
					q->removeSource(user, QueueItem::Source::FLAG_NO_NEED_PARTS);
					result.lastError = STRING(NO_NEEDED_PART);
					return result;
				}
			}

			// Check that the file we will be downloading to exists
			if (q->getDownloadedBytes() > 0) {
				if (!PathUtil::fileExists(q->getTempTarget())) {
					// Temp target gone?
					q->resetDownloaded();
				}
			}

			result.download = new Download(aSource, *q);
			if (TransferSlot::toType(aSource.getSlot()) != TransferSlot::USERSLOT) {
				aSource.setSlot(slotType);
			}

			userQueue.addDownload(q, result.download);
		}
	}

	fire(QueueManagerListener::ItemSources(), q);
	dcdebug("QueueManager::getDownload: found %s for connection %s (segment " I64_FMT ", " I64_FMT ")\n", q->getTarget().c_str(), result.download->getConnectionToken().c_str(), result.download->getSegment().getStart(), result.download->getSegment().getEnd());
	return result;
}

bool QueueManager::checkLowestPrioRules(const QueueItemPtr& aQI, const QueueTokenSet& aRunningBundles, string& lastError_) const noexcept {
	auto& b = aQI->getBundle();
	if (!b) {
		return true;
	}

	if (b->getPriority() == Priority::LOWEST) {
		// Don't start if there are other bundles running
		if (!aRunningBundles.empty() && !aRunningBundles.contains(b->getToken())) {
			lastError_ = STRING(LOWEST_PRIO_ERR_BUNDLES);
			return false;
		}
	}

	if (aQI->getPriority() == Priority::LOWEST) {
		// Start only if there are no other downloads running in this bundle 
		// (or all bundle downloads belong to this file)
		auto bundleDownloads = DownloadManager::getInstance()->getBundleDownloadConnectionCount(aQI->getBundle());

		RLock l(cs);
		auto start = bundleDownloads == 0 || bundleDownloads == aQI->getDownloads().size();
		if (!start) {
			lastError_ = STRING(LOWEST_PRIO_ERR_FILES);
			return false;
		}
	}

	return true;
}

bool QueueManager::checkDownloadLimits(const QueueItemPtr& aQI, string& lastError_) const noexcept {
	auto downloadSlots = AutoLimitUtil::getSlots(true);
	auto downloadCount = static_cast<int>(DownloadManager::getInstance()->getFileDownloadConnectionCount());
	bool slotsFull = downloadSlots != 0 && downloadCount >= downloadSlots;

	auto speedLimit = Util::convertSize(AutoLimitUtil::getSpeedLimitKbps(true), Util::KB);
	auto downloadSpeed = DownloadManager::getInstance()->getRunningAverage();
	bool speedFull = speedLimit != 0 && downloadSpeed >= speedLimit;
	//log("Speedlimit: " + Util::toString(Util::getSpeedLimit(true)*1024) + " slots: " + Util::toString(Util::getSlots(true)) + " (avg: " + Util::toString(getRunningAverage()) + ")");

	if (slotsFull || speedFull) {
		bool extraFull = downloadSlots != 0 && downloadCount >= downloadSlots + SETTING(EXTRA_DOWNLOAD_SLOTS);
		if (extraFull || aQI->getPriority() != Priority::HIGHEST) {
			if (slotsFull) {
				lastError_ = STRING(ALL_DOWNLOAD_SLOTS_TAKEN);
			} else {
				lastError_ = STRING(MAX_DL_SPEED_REACHED);
			}
			return false;
		}
	}

	return true;
}

bool QueueManager::checkDiskSpace(const QueueItemPtr& aQI, string& lastError_) noexcept {
	auto& b = aQI->getBundle();
	if (!b) {
		return true;
	}

	//check if we have free space to continue the download now... otherwise results in paused priority..
	if (aQI->getBundle()->getStatus() == Bundle::STATUS_DOWNLOAD_ERROR) {
		if (File::getFreeSpace(b->getTarget()) >= static_cast<int64_t>(aQI->getSize() - aQI->getDownloadedBytes())) {
			setBundleStatus(b, Bundle::STATUS_QUEUED);
		} else {
			lastError_ = b->getError();
			onDownloadError(b, lastError_);
			return false;
		}
	}

	return true;
}

#define SLOT_SOURCE_QUEUE "queue"
OptionalTransferSlot QueueManager::allowStartQI(const QueueItemPtr& aQI, const QueueTokenSet& aRunningBundles, string& lastError_, const OptionalTransferSlot& aExistingSlot) noexcept{
	// nothing to download?
	if (!aQI)
		return nullopt;

	// override the slot settings for partial lists and small files
	if (aQI->usesSmallSlot())
		return TransferSlot(TransferSlot::FILESLOT, SLOT_SOURCE_QUEUE);


	// paused?
	if (aQI->isPausedPrio())
		return nullopt;

	if (!checkDiskSpace(aQI, lastError_)) {
		return nullopt;
	}

	if ((!aExistingSlot || aExistingSlot->type != TransferSlot::USERSLOT) && !checkDownloadLimits(aQI, lastError_)) {
		return nullopt;
	}

	if (!checkLowestPrioRules(aQI, aRunningBundles, lastError_)) {
		return nullopt;
	}

	return TransferSlot(TransferSlot::USERSLOT, SLOT_SOURCE_QUEUE);
}

QueueDownloadResult QueueManager::startDownload(const HintedUser& aUser, QueueDownloadType aType) noexcept{
	auto hubs = ClientManager::getInstance()->getHubSet(aUser.user->getCID());
	auto runningBundleTokens = DownloadManager::getInstance()->getRunningBundles();
	return startDownload(aUser, aType, runningBundleTokens, hubs, nullptr);
}

QueueDownloadResult QueueManager::startDownload(const HintedUser& aUser, QueueDownloadType aType, const QueueTokenSet& aRunningBundles, const OrderedStringSet& aOnlineHubs, const UserConnection* aExistingConnection) noexcept {
	QueueDownloadResult result(aUser.hint);
	if (aOnlineHubs.empty()) {
		result.lastError = STRING(USER_OFFLINE);
		return result;
	}

	QueueDownloadQuery query(aUser, aOnlineHubs, aRunningBundles);
	query.lastSpeed = aExistingConnection ? aExistingConnection->getSpeed() : 0;
	query.downloadType = aType;

	{
		RLock l(cs);
		auto qi = userQueue.getNext(query, result.lastError, result.hasDownload);

		if (qi) {
			result.qi = qi;
			if (qi->getBundle()) {
				result.bundleToken = qi->getBundle()->getToken();
			}

			if (!aOnlineHubs.contains(aUser.hint)) {
				//we can't connect via a hub that is offline...
				result.hubHint = *aOnlineHubs.begin();
			}

			result.allowUrlChange = qi->allowUrlChange();

			qi->getSource(aUser)->updateDownloadHubUrl(aOnlineHubs, result.hubHint, result.allowUrlChange);
		}
	}

	if (result.qi) {
		result.slotType = allowStartQI(result.qi, aRunningBundles, result.lastError, aExistingConnection ? aExistingConnection->getSlot() : nullopt);
		result.downloadType = result.qi->usesSmallSlot() ? QueueDownloadType::SMALL : QueueDownloadType::ANY;
	}

	return result;
}

QueueItemList QueueManager::findFiles(const TTHValue& tth) const noexcept {
	QueueItemList ql;

	RLock l(cs);
	fileQueue.findFiles(tth, ql);

	return ql;
}

string QueueManager::QueueMatchResults::format() const noexcept {
	if (matchingFiles > 0) {
		if (bundles.size() == 1) {
			return STRING_F(MATCHED_FILES_BUNDLE, matchingFiles % bundles.front()->getName().c_str() % newFiles);
		} else {
			return STRING_F(MATCHED_FILES_X_BUNDLES, matchingFiles % (int)bundles.size() % newFiles);
		}
	}

	return STRING(NO_MATCHED_FILES);
}

QueueManager::QueueMatchResults QueueManager::matchListing(const DirectoryListing& dl) noexcept {
	QueueMatchResults results;
	if (dl.getUser() == ClientManager::getInstance()->getMe())
		return results;

	QueueItemList matchingItems;

	{
		RLock l(cs);
		fileQueue.matchListing(dl, matchingItems);
	}

	results.matchingFiles = static_cast<int>(matchingItems.size());

	results.newFiles = addValidatedSources(dl.getHintedUser(), matchingItems, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE, results.bundles);
	return results;
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
	if (auto qi = fileQueue.findFile(aTarget); qi) {
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

	for(auto const& q: ql)
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

void QueueManager::logDownload(Download* aDownload) const noexcept {
	if (!aDownload->isFilelist() || SETTING(LOG_FILELIST_TRANSFERS)) {
		if (SETTING(SYSTEM_SHOW_DOWNLOADS)) {
			auto nicks = ClientManager::getInstance()->getFormattedNicks(aDownload->getHintedUser());
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
		string newTarget = PathUtil::getFilePath(source) + PathUtil::getFileName(target);
		try {
			File::renameFile(source, newTarget);
			log(STRING_F(MOVE_FILE_FAILED, newTarget % PathUtil::getFilePath(target) % e1.getError()), LogMessage::SEV_ERROR);
		} catch(const FileException& e2) {
			log(STRING_F(UNABLE_TO_RENAME, source % e2.getError()), LogMessage::SEV_ERROR);
		}
	}

	tasks.addTask([=, this] {
		// Handle the results later...
		runFileCompletionHooks(aQI);

		auto bundle = aQI->getBundle();
		if (bundle) {
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

bool QueueManager::checkBundleFinishedHooked(const BundlePtr& aBundle) noexcept {
	bool isPrivate = false;

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
	}

	log(STRING_F(DL_BUNDLE_FINISHED, aBundle->getName().c_str()), LogMessage::SEV_INFO);
	shareBundle(aBundle, false);

	return true;
}

void QueueManager::shareBundle(BundlePtr aBundle, bool aSkipValidations) noexcept {
	
	if (aBundle->getStatus() == Bundle::STATUS_SHARED)
		return;

	tasks.addTask([aBundle, aSkipValidations, this] {
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
		std::erase_if(failedFiles, [this](const QueueItemPtr& aQI) {
			return runFileCompletionHooks(aQI);
		});
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
		tasks.addTask([aBundle, this] { setBundlePriority(aBundle, Priority::PAUSED_FORCE, false); });
	}

	aBundle->setError(aError);
	setBundleStatus(aBundle, Bundle::STATUS_DOWNLOAD_ERROR);
}

void QueueManager::putDownloadHooked(Download* aDownload, bool aFinished, bool aNoAccess /*false*/, bool aRotateQueue /*false*/) {

	// Make sure the download gets killed
	unique_ptr<Download> d(aDownload);
	aDownload = nullptr;

	d->close();

	QueueItemPtr q = nullptr;
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

		userQueue.removeDownload(aQI, aDownload);
	}

	for (const auto& u : getConn) {
		if (u.user != aDownload->getUser()) //trying a different user? we rotated queue, shouldn't we try another file?
			ConnectionManager::getInstance()->getDownloadConnection(u);
	}

	onFileDownloadRemoved(aQI, true);
	return;
}

void QueueManager::onFileDownloadRemoved(const QueueItemPtr& aQI, bool aFailed) noexcept {
	fire(QueueManagerListener::ItemStatus(), aQI);
	if (aQI->getBundle()) {
		auto checkWaiting = [this, bundle = aQI->getBundle()] {
			auto downloads = DownloadManager::getInstance()->getBundleDownloadConnectionCount(bundle);
			if (downloads == 0) {
				fire(QueueManagerListener::BundleDownloadStatus(), bundle);
				bundle->setStart(0);
			}
		};

		if (aFailed) {
			checkWaiting();
		} else {
			delayEvents.addEvent(aQI->getBundle()->getToken(), [this, checkWaiting] {
				checkWaiting();
			}, 1000);
		}
	}
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
		userQueue.removeDownload(aQI, aDownload);
	}

	dcassert(aDownload->getTreeValid());
	try {
		HashManager::getInstance()->addTree(aDownload->getTigerTree());
	} catch (const HashException& e) {
		ConnectionManager::getInstance()->failDownload(aDownload->getConnectionToken(), e.getError(), true);
		throw;
	}

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

		// dcdebug("Finish segment for %s (" I64_FMT ", " I64_FMT ")\n", aDownload->getToken().c_str(), aDownload->getSegment().getStart(), aDownload->getSegment().getEnd());

		if (wholeFileCompleted) {
			// Disconnect all possible overlapped downloads
			for (auto qiDownload : aQI->getDownloads()) {
				if (qiDownload != aDownload) {
					qiDownload->getUserConnection().disconnect();
				}
			}

			aQI->setTimeFinished(GET_TIME());
			aQI->setStatus(QueueItem::STATUS_DOWNLOADED);
			userQueue.removeQI(aQI);

			if (!aQI->getBundle()) {
				fileQueue.remove(aQI);
			}
		} else {
			userQueue.removeDownload(aQI, aDownload);
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

		auto nicks = ClientManager::getInstance()->getFormattedNicks(aDownload->getHintedUser());
		aQI->setLastSource(nicks);
		fire(QueueManagerListener::ItemFinished(), aQI, Util::emptyString, aDownload->getHintedUser(), aDownload->getAverageSpeed());
	}

	if (wholeFileCompleted && !aQI->getBundle()) {
		fire(QueueManagerListener::ItemRemoved(), aQI, true);
	} else {
		onFileDownloadRemoved(aQI, false);
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
	StringList disconnectTokens;
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
				disconnectTokens.push_back(d->getConnectionToken());
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
	for (const auto& token: disconnectTokens)
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

int QueueManager::removeSource(const UserPtr& aUser, Flags::MaskType aReason, const QueueItemExcludeF& aExcludeF /*nullptr*/) noexcept {
	// @todo remove from finished items
	QueueItemList ql;

	{
		RLock l(cs);
		userQueue.getUserQIs(aUser, ql);

		if (aExcludeF) {
			std::erase_if(ql, aExcludeF);
		}
	}

	for (const auto& qi : ql) {
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

		bundleQueue.searchQueue.removeSearchPrio(aBundle);
		userQueue.setBundlePriority(aBundle, p);
		bundleQueue.searchQueue.addSearchPrio(aBundle);
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
		ranges::copy_if(bundleQueue.getBundles() | views::values, back_inserter(bundles), [](const BundlePtr& aBundle) {
			return aBundle->isCompleted();
		});
	}

	for (const auto& bundle: bundles) {
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

	for (const auto& bundle: bundles | views::values) {
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
		for (const auto& u: getConn)
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
		/*auto p = ranges::find_if(aBundle->getFinishedNotifications(), [&aUser](const Bundle::UserBundlePair& ubp) { return ubp.first.user == aUser; });
		if (p != aBundle->getFinishedNotifications().end()) {
			sendRemovePBD(p->first, p->second);
		}*/
	}

	for (const auto& qi: ql) {
		removeFileSource(qi, aUser, aReason);
	}

	fire(QueueManagerListener::SourceFilesUpdated(), aUser);
	return ql.size();
}

void QueueManager::saveQueue(bool aForce) noexcept {
	RLock l(cs);	
	bundleQueue.saveQueue(aForce);
}

class QueueLoader : public SimpleXMLReader::CallBack {
public:
	QueueLoader() = default;
	~QueueLoader() override = default;
	void startTag(const string& name, StringPairList& attribs, bool simple) override;
	void endTag(const string& name) override;
	void createFileBundle(QueueItemPtr& aQI, QueueToken aToken);

	void loadDirectoryBundle(StringPairList& attribs, bool simple);
	void loadFileBundle(StringPairList& attribs, bool simple);
	void loadQueueFile(StringPairList& attribs, bool simple);
	void loadFinishedFile(StringPairList& attribs, bool simple);
	void loadSource(StringPairList& attribs, bool simple);
	void loadSegment(StringPairList& attribs, bool simple);

	Priority validatePrio(const string& aPrio) const;
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
	QueueManager* qm = QueueManager::getInstance();
};

void QueueManager::loadBundleFile(const string& aXmlPath) const noexcept {
	QueueLoader loader;
	try {
		File f(aXmlPath, File::READ, File::OPEN, File::BUFFER_SEQUENTIAL, false);
		SimpleXMLReader(&loader).parse(f);
	} catch (const Exception& e) {
		log(STRING_F(BUNDLE_LOAD_FAILED, aXmlPath % e.getError().c_str()), LogMessage::SEV_ERROR);
		File::deleteFile(aXmlPath);
	}
}

void QueueManager::loadQueue(StartupLoader& aLoader) noexcept {
	setMatchers();

	// migrate old bundles
	AppUtil::migrate(AppUtil::getPath(AppUtil::PATH_BUNDLES), "Bundle*");

	// multithreaded loading
	auto fileList = File::findFiles(AppUtil::getPath(AppUtil::PATH_BUNDLES), "Bundle*", File::TYPE_FILE);
	atomic<long> loaded(0);
	try {
		parallel_for_each(fileList.begin(), fileList.end(), [&](const string& path) {
			if (PathUtil::getFileExt(path) != ".xml") {
				return;
			}

			loadBundleFile(path);

			loaded++;
			aLoader.progressF(static_cast<float>(loaded) / static_cast<float>(fileList.size()));
		});
	} catch (std::exception& e) {
		log("Loading the queue failed: " + string(e.what()), LogMessage::SEV_INFO);
	}

	// Old Queue.xml (useful only for users migrating from other clients)
	migrateLegacyQueue();

	// Listeners
	TimerManager::getInstance()->addListener(this); 
	SearchManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);
	ShareManager::getInstance()->addListener(this);

	// Finished bundles
	auto finishedCount = getFinishedBundlesCount();
	if (finishedCount > 500) {
		log(STRING_F(BUNDLE_X_FINISHED_WARNING, finishedCount), LogMessage::SEV_WARNING);
	}

	// Completion checks involve hooks, let everything load first
	aLoader.addPostLoadTask([this] {
		checkCompletedBundles(Util::emptyString, true);
	});
}

void QueueManager::migrateLegacyQueue() noexcept {
	try {
		//load the old queue file and delete it
		auto path = AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "Queue.xml";
		AppUtil::migrate(path);

		{
			File f(path, File::READ, File::OPEN, File::BUFFER_SEQUENTIAL);
			QueueLoader loader;
			SimpleXMLReader(&loader).parse(f);
		}

		File::copyFile(AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "Queue.xml", AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "Queue.xml.bak");
		File::deleteFile(AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "Queue.xml");
	} catch (const Exception&) {
		// ...
	}
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

Priority QueueLoader::validatePrio(const string& aPrio) const {
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
		auto priority = !prio.empty() ? validatePrio(prio) : Priority::DEFAULT;
		curBundle = make_shared<Bundle>(bundleTarget, added, priority, dirDate, Util::toUInt32(token), false);
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

	if (curBundle && inDirBundle && !PathUtil::isParentOrExactLocal(curBundle->getTarget(), currentFileTarget)) {
		//the file isn't inside the main bundle dir, can't add this
		return;
	}

	auto timeAdded = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 2)));
	if (timeAdded == 0)
		timeAdded = GET_TIME();

	const string& tthRoot = getAttrib(attribs, sTTH, 3);
	if (tthRoot.empty())
		return;

	auto p = validatePrio(getAttrib(attribs, sPriority, 4));

	auto& tempTarget = getAttrib(attribs, sTempTarget, 5);
	auto maxSegments = (uint8_t)Util::toInt(getAttrib(attribs, sMaxSegments, 5));

	if (Util::toInt(getAttrib(attribs, sAutoPriority, 6)) == 1) {
		p = Priority::DEFAULT;
	}

	WLock l(qm->cs);
	auto [qi, added] = qm->fileQueue.add(currentFileTarget, size, 0, p, tempTarget, timeAdded, TTHValue(tthRoot));
	if (added) {
		qi->setMaxSegments(max((uint8_t)1, maxSegments));

		// Bundles
		if (curBundle && inDirBundle) {
			qm->bundleQueue.addBundleItem(qi, curBundle);
		} else if (inLegacyQueue) {
			createFileBundle(qi, QueueItem::idCounter.next());
		} else if (inFileBundle) {
			createFileBundle(qi, curFileBundleInfo.token);
		}
	}

	if (!simple)
		curFile = qi;
}

void QueueLoader::loadFinishedFile(StringPairList& attribs, bool) {
	//log("FOUND FINISHED TTH");
	const string& target = getAttrib(attribs, sTarget, 0);
	auto size = Util::toInt64(getAttrib(attribs, sSize, 1));
	auto timeAdded = Util::toTimeT(getAttrib(attribs, sAdded, 2));
	const string& tth = getAttrib(attribs, sTTH, 3);
	auto finished = Util::toTimeT(getAttrib(attribs, sTimeFinished, 4));
	const string& lastSource = getAttrib(attribs, sLastSource, 5);

	if (size == 0 || tth.empty() || target.empty() || timeAdded == 0)
		return;
	if (!PathUtil::fileExists(target))
		return;

	WLock l(qm->cs);
	auto [qi, added] = qm->fileQueue.add(target, size, 0, Priority::DEFAULT, Util::emptyString, timeAdded, TTHValue(tth));
	if (!added) {
		return;
	}

	qi->setStatus(QueueItem::STATUS_COMPLETED);
	qi->addFinishedSegment(Segment(0, size)); //make it complete
	qi->setTimeFinished(finished);
	qi->setLastSource(lastSource);

	if (curBundle && inDirBundle) {
		qm->bundleQueue.addBundleItem(qi, curBundle);
	} else if (inFileBundle) {
		createFileBundle(qi, curFileBundleInfo.token);
	}
}

void QueueLoader::loadSource(StringPairList& attribs, bool) {
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
}

void QueueLoader::loadSegment(StringPairList& attribs, bool) {
	auto start = Util::toInt64(getAttrib(attribs, sStart, 0));
	auto size = Util::toInt64(getAttrib(attribs, sSize, 1));

	if (size > 0 && start >= 0 && (start + size) <= curFile->getSize()) {
		curFile->addFinishedSegment(Segment(start, size));
		if (curFile->getAutoPriority() && SETTING(AUTOPRIO_TYPE) == SettingsManager::PRIO_PROGRESS) {
			curFile->setPriority(curFile->calculateAutoPriority());
		}
	} else {
		dcdebug("Invalid segment: " I64_FMT " " I64_FMT "\n", start, size);
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
		} else if (curFile && name == sSegment) {
			loadSegment(attribs, simple);
		} else if (curFile && name == sSource) {
			loadSource(attribs, simple);
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
			if (ranges::find_if(rl, [&sr](const SearchResultPtr& aSR) { return aSR->getUser() == sr->getUser() && aSR->getAdcPath() == sr->getAdcPath(); }) != rl.end()) {
				//don't add the same result multiple times, makes the counting more reliable
				return;
			}
			rl.push_back(sr);
		}
		delayEvents.addEvent(selQI->getToken(), [selQI, this] { pickMatchHooked(selQI); }, 2000);
	}
}

void QueueManager::pickMatchHooked(QueueItemPtr qi) noexcept {
	SearchResultList results;
	int addNum = 0;

	//get the result list
	{
		WLock l(cs);
		if (auto p = searchResults.find(qi->getTarget()); p != searchResults.end()) {
			results.swap(p->second);
			searchResults.erase(p);
		}

		auto totalBundleSources = qi->getBundle()->countOnlineUsers() + static_cast<int>(matchLists.left.count(qi->getBundle()->getToken()));
		addNum = SETTING(MAX_AUTO_MATCH_SOURCES) - totalBundleSources;
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

	auto path = PathUtil::getAdcMatchPath(aResult->getAdcPath(), aQI->getTarget(), aQI->getBundle()->getTarget(), isNmdc);
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
				log(ClientManager::getInstance()->getFormattedNicks(aResult->getUser()) + ": " + 
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
			if (!hasDown && !q->isPausedPrio() && q->validateHub(aUser.getUser(), aUser.getHubUrl())) {
				hasDown = true;
			}
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
		for (const auto& b : bundleQueue.getBundles() | views::values) {
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
		for (const auto& q : fileQueue.getPathQueue() | views::values) {
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
		for (const auto& [b, p] : bundlePriorities)
			setBundlePriority(b, p, true);

		for (const auto& [q, p] : qiPriorities)
			setQIPriority(q, p);
	}

	lastAutoPrio = aTick;
}

void QueueManager::checkResumeBundles() noexcept {
	BundleList resumeBundles;

	{
		RLock l(cs);
		for (const auto& b : bundleQueue.getBundles() | views::values) {
			if (b->isDownloaded()) {
				continue;
			}

			if (b->getResumeTime() > 0 && GET_TIME() > b->getResumeTime()) {
				resumeBundles.push_back(b);
			}

			//check if we have free space to continue the download...
			if (b->getStatus() == Bundle::STATUS_DOWNLOAD_ERROR) {
				if (File::getFreeSpace(b->getTarget()) >= b->getSize() - b->getDownloadedBytes()) {
					resumeBundles.push_back(b);
				}
			}
		}
	}

	for (const auto& b : resumeBundles) {

		if (b->getStatus() == Bundle::STATUS_DOWNLOAD_ERROR)
			setBundleStatus(b, Bundle::STATUS_QUEUED);

		setBundlePriority(b, Priority::DEFAULT, false);
	}
}

void QueueManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	tasks.addTask([aTick, this] {
		if ((lastXmlSave + 10000) < aTick) {
			saveQueue(false);
			lastXmlSave = aTick;
		}

		QueueItemList runningItems;

		{
			RLock l(cs);
			for (const auto& q : fileQueue.getPathQueue() | views::values) {
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
	tasks.addTask([aTick, this] {
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
	auto maxSpeed = static_cast<double>(ranges::max_element(speedSourceMap)->second.first);
	if (maxSpeed > 0) {
		factorSpeed = 100 / maxSpeed;
	}

	auto maxSources = ranges::max_element(speedSourceMap)->second.second;
	if (maxSources > 0) {
		factorSource = 100 / maxSources;
	}

	multimap<double, T> finalMap;
	int uniqueValues = 0;
	for (auto& [item, speedSourcePair] : speedSourceMap) {
		auto points = (static_cast<double>(speedSourcePair.first) * factorSpeed) + (speedSourcePair.second * factorSource);
		if (!finalMap.contains(points)) {
			uniqueValues++;
		}
		finalMap.emplace(points, item);
	}

	int prioGroup = 1;
	if (uniqueValues <= 1) {
		if (verbose) {
			LogManager::getInstance()->message("Not enough items with unique points to perform the priorization!", LogMessage::SEV_INFO, "Debug");
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

	for (auto& [points, item] : finalMap) {
		Priority newItemPrio;

		if (lastPoints == points) {
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
			lastPoints = points;
		}

		if (verbose) {
			LogManager::getInstance()->message(item->getTarget() + " points: " + Util::toString(points) + " using prio " + Util::formatPriority(newItemPrio), LogMessage::SEV_INFO, "Debug");
		}

		if (item->getPriority() != newItemPrio) {
			priorities.emplace_back(item, newItemPrio);
		}
	}
}

void QueueManager::calculateBundlePriorities(bool verbose) noexcept {
	multimap<BundlePtr, pair<int64_t, double>> bundleSpeedSourceMap;

	/* Speed and source maps for files in each bundle */
	vector<multimap<QueueItemPtr, pair<int64_t, double>>> qiMaps;

	{
		RLock l (cs);
		for (const auto& b: bundleQueue.getBundles() | views::values) {
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

	for (const auto& [b, p] : bundlePriorities) {
		setBundlePriority(b, p, true);
	}


	if (SETTING(QI_AUTOPRIO)) {

		vector<pair<QueueItemPtr, Priority>> qiPriorities;
		for (auto& s: qiMaps) {
			calculateBalancedPriorities<QueueItemPtr>(qiPriorities, s, verbose);
		}

		for (const auto& [q, p] : qiPriorities) {
			setQIPriority(q, p, true);
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

void QueueManager::getPartialInfo(const QueueItemPtr& aQI, PartsInfo& partialInfo_) const noexcept {
	auto blockSize = aQI->getBlockSize();

	RLock l(cs);
	aQI->getPartialInfo(partialInfo_, blockSize);
}

bool QueueManager::addPartialSourceHooked(const HintedUser& aUser, const QueueItemPtr& aQI, const PartsInfo& aInPartialInfo) noexcept {
	bool wantConnection = false;

	// Check source
	try {
		checkSourceHooked(aUser, nullptr);
	} catch (const QueueException& e) {
		log(STRING_F(SOURCE_ADD_ERROR, e.what()), LogMessage::SEV_WARNING);
		return false;
	}

	// Get my parts info
	auto blockSize = aQI->getBlockSize();

	{
		WLock l(cs);
		
		// Any parts for me?
		wantConnection = aQI->isNeededPart(aInPartialInfo, blockSize);

		// If this user isn't a source and has no parts needed, ignore it
		auto si = aQI->getSource(aUser);
		if(si == aQI->getSources().end()) {
			si = aQI->getBadSource(aUser);

			if (si != aQI->getBadSources().end() && si->isSet(QueueItem::Source::FLAG_TTH_INCONSISTENCY)) {
				return false;
			}

			if (!wantConnection) {
				if (si == aQI->getBadSources().end()) {
					return false;
				}
			} else {
				// add this user as partial file sharing source
				aQI->addSource(aUser);
				si = aQI->getSource(aUser);
				si->setFlag(QueueItem::Source::FLAG_PARTIAL);

				userQueue.addQI(aQI, aUser);
				dcassert(si != aQI->getSources().end());
			}
		}

		// Update source's parts info
		si->setPartsInfo(aInPartialInfo);
	}
	
	// Connect to this user
	if (wantConnection) {
		fire(QueueManagerListener::ItemSources(), aQI);

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

DupeType QueueManager::getAdcDirectoryDupe(const string& aDir, int64_t aSize) const noexcept {
	RLock l(cs);
	return bundleQueue.getAdcDirectoryDupe(aDir, aSize);
}

StringList QueueManager::getAdcDirectoryDupePaths(const string& aDirName) const noexcept {
	RLock l(cs);
	return bundleQueue.getAdcDirectoryDupePaths(aDirName);
}

void QueueManager::getBundlePaths(OrderedStringSet& retBundles) const noexcept {
	RLock l(cs);
	for (const auto& b : bundleQueue.getBundles() | views::values) {
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
		for (const auto& b : bundleQueue.getBundles() | views::values) {
			if (b->isCompleted() && PathUtil::isParentOrExactLocal(aPath, b->getTarget())) {
				bundles.push_back(b);
			}
		}
	}

	for (const auto& b: bundles) {
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

bool QueueManager::isChunkDownloaded(const TTHValue& tth, const Segment* aSegment, int64_t& fileSize_, string& target_) noexcept {
	QueueItemList ql;

	RLock l(cs);
	fileQueue.findFiles(tth, ql);

	if(ql.empty()) return false;


	auto qi = ql.front();
	if (!qi->hasPartialSharingTarget())
		return false;

	fileSize_ = qi->getSize();
	target_ = qi->isDownloaded() ? qi->getTarget() : qi->getTempTarget();

	if (!aSegment) {
		return qi->isDownloaded();
	}

	return qi->isChunkDownloaded(*aSegment);
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
		ranges::copy_if(aItems, back_inserter(addedItems), [&](const QueueItemPtr& q) {
			if (q->getBundle() && ranges::find(matchingBundles_, q->getBundle()) == matchingBundles_.end()) {
				matchingBundles_.push_back(q->getBundle());
			}

			try {
				if (addValidatedSource(q, aUser, aAddBad)) {
					wantConnection = true;
				}

				return true;
			} catch (const QueueException&) {
				// Ignore...
			}

			return false;
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

	return static_cast<int>(addedItems.size());
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
	auto files = aBundle->getFinishedFiles(); // copy is needed
	for (const auto& qi: files) {
		if (!PathUtil::fileExists(qi->getTarget())) {
			bundleQueue.removeBundleItem(qi, false);
			fileQueue.remove(qi);
		}
	}

	aBundle->setTimeFinished(0);
	bundleQueue.searchQueue.addSearchPrio(aBundle);

	aBundle->setDirty();
	log(STRING_F(BUNDLE_READDED, aBundle->getName().c_str()), LogMessage::SEV_INFO);
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
	return static_cast<int>(aBundle->getQueueItems().size()); 
}

int QueueManager::getFinishedItemCount(const BundlePtr& aBundle) const noexcept { 
	RLock l(cs); 
	return (int)aBundle->getFinishedFiles().size(); 
}

int QueueManager::getFinishedBundlesCount() const noexcept {
	RLock l(cs);
	return static_cast<int>(ranges::count_if(bundleQueue.getBundles() | views::values, [&](const BundlePtr& b) { return b->isDownloaded(); }));
}

void QueueManager::addBundleUpdate(const BundlePtr& aBundle) noexcept{
	/*
	Add as Task to fix Deadlock!!
	handleBundleUpdate(..) has a Lock and this function is called inside a Lock, while delayEvents has its own locking for add/execute functions.
	*/
	tasks.addTask([aBundle, this] {
		delayEvents.addEvent(aBundle->getToken(), [aBundle, this] {
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
			// DownloadManager::getInstance()->sendSizeUpdate(b);
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
				bundleQueue.searchQueue.removeSearchPrio(bundle);
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
		delayEvents.addEvent(bundle->getToken(), [bundle, this] {
			tasks.addTask([bundle, this] {
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

		auto finishedItems = aBundle->getFinishedFiles(); // copy is needed
		for (const auto& qi : finishedItems) {
			fileQueue.remove(qi);
			bundleQueue.removeBundleItem(qi, false);
			if (aRemoveFinishedFiles) {
				UploadManager::getInstance()->abortUpload(qi->getTarget());
				deleteFiles.push_back(qi->getTarget());
			}
		}

		auto queueItems = aBundle->getQueueItems(); // copy is needed
		for (const auto& qi : queueItems) {
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
		if (!PathUtil::removeDirectoryIfEmpty(aBundle->getTarget(), 10) && !aRemoveFinishedFiles) {
			log(STRING_F(DIRECTORY_NOT_REMOVED, aBundle->getTarget()), LogMessage::SEV_INFO);
		}
	}

	if (!isCompleted) {
		log(STRING_F(BUNDLE_X_REMOVED, aBundle->getName()), LogMessage::SEV_INFO);
	}

	for (const auto& aUser: sources)
		fire(QueueManagerListener::SourceFilesUpdated(), aUser);

	removeBundleLists(aBundle);
}

void QueueManager::removeBundleLists(const BundlePtr& aBundle) noexcept{
	QueueItemList removed;
	{
		RLock l(cs);
		//erase all lists related to this bundle
		auto listings = matchLists.left.equal_range(aBundle->getToken());
		for (const auto& list: listings | pair_to_range) {
			auto q = fileQueue.findFile(list.get_right());
			if (q) {
				removed.push_back(q);
			}
		}
	}

	for (const auto& qi : removed)
		removeQI(qi);
}

MemoryInputStream* QueueManager::generateTTHList(QueueToken aBundleToken, bool isInSharingHub, BundlePtr& bundle_) const {
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
			for (const auto& q: bundle_->getFinishedFiles()) {
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

void QueueManager::addBundleTTHListHooked(const HintedUser& aUser, const BundlePtr& aBundle, const string& aRemoteBundleToken) {
	dcassert(!aUser.hint.empty());
	auto info = FilelistAddData(aUser, this, aRemoteBundleToken);
	addListHooked(info, (QueueItem::FLAG_TTHLIST_BUNDLE | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_MATCH_QUEUE), aBundle);
}

void QueueManager::addSourceHooked(const HintedUser& aUser, const TTHValue& aTTH) noexcept {
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
		bundle = bundleQueue.searchQueue.maybePopSearchItem(aTick);
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
				bundleQueue.searchQueue.recalculateSearchTimes(aBundle->isRecent(), true, aTick);

			nextSearchTick = aBundle->isRecent() ? bundleQueue.searchQueue.getNextSearchRecent() : bundleQueue.searchQueue.getNextSearchNormal();
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
	auto ql = b->getQueueItems(); // copy is required
	for (const auto& q: ql) {
		if (!q->isPausedPrio()) {
			userQueue.removeQI(q, false);
			userQueue.addQI(q);
		}
	}
}

} // namespace dcpp
