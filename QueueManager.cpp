/* 
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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
#include "version.h"

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

/*QueueManager::FileMover::FileMover() { 
	start();
	setThreadPriority(Thread::LOW);
}

QueueManager::FileMover::~FileMover() { 
	join();
}

struct MoverTask : public Task {
	MoverTask(const string& aSource, const string& aTarget, QueueItemPtr aQI) : target(aTarget), source(aSource), qi(aQI) { }

	string target, source;
	QueueItemPtr qi;
};

void QueueManager::FileMover::removeDir(const string& aDir) {
	tasks.add(REMOVE_DIR, unique_ptr<StringTask>(new StringTask(aDir)));
	s.signal();
}

void QueueManager::FileMover::shutdown() {
	tasks.add(SHUTDOWN, nullptr);
	s.signal();
}

int QueueManager::FileMover::run() {
	for(;;) {
		s.wait();
		TaskQueue::TaskPair t;
		if (!tasks.getFront(t)) {
			continue;
		}

		if (t.first == MOVE_FILE) {
			auto mv = static_cast<MoverTask*>(t.second);
			moveFile_(mv->source, mv->target, mv->qi);
		} else if (t.first == REMOVE_DIR) {
			auto dir = static_cast<StringTask*>(t.second);
			AirUtil::removeDirectoryIfEmpty(dir->str, 10, false);
		} else if (t.first == SHUTDOWN) {
			break;
		}

		tasks.pop_front();
	}

	return 0;
}*/

void QueueManager::shutdown() {
	saveQueue(false);
}

void QueueManager::Rechecker::add(const string& file) {
	Lock l(cs);
	files.push_back(file);
	if(!active) {
		active = true;
		start();
	}
}

int QueueManager::Rechecker::run() {
	while(true) {
		string file;
		{
			Lock l(cs);
			auto i = files.begin();
			if(i == files.end()) {
				active = false;
				return 0;
			}
			file = *i;
			files.erase(i);
		}

		QueueItemPtr q;
		int64_t tempSize;
		TTHValue tth;

		{
			RLock l(qm->cs);

			q = qm->fileQueue.findFile(file);
			if(!q || q->isSet(QueueItem::FLAG_USER_LIST))
				continue;

			qm->fire(QueueManagerListener::RecheckStarted(), q->getTarget());
			dcdebug("Rechecking %s\n", file.c_str());

			tempSize = File::getSize(q->getTempTarget());

			if(tempSize == -1) {
				qm->fire(QueueManagerListener::RecheckNoFile(), q->getTarget());
				continue;
			}

			if(tempSize < Util::convertSize(64, Util::KB)) {
				qm->fire(QueueManagerListener::RecheckFileTooSmall(), q->getTarget());
				continue;
			}

			if(tempSize != q->getSize()) {
				File(q->getTempTarget(), File::WRITE, File::OPEN).setSize(q->getSize());
			}

			if(q->isRunning()) {
				qm->fire(QueueManagerListener::RecheckDownloadsRunning(), q->getTarget());
				continue;
			}

			tth = q->getTTH();
		}

		TigerTree tt;
		bool gotTree = HashManager::getInstance()->getTree(tth, tt);

		string tempTarget;

		{
			RLock l(qm->cs);

			// get q again in case it has been (re)moved
			q = qm->fileQueue.findFile(file);
			if(!q)
				continue;

			if(!gotTree) {
				qm->fire(QueueManagerListener::RecheckNoTree(), q->getTarget());
				continue;
			}

			//Clear segments
			q->resetDownloaded();

			tempTarget = q->getTempTarget();
		}

		TigerTree ttFile(tt.getBlockSize());

		try {
			FileReader().read(tempTarget, [&](const void* x, size_t n) {
				return ttFile.update(x, n), true;
			});
		} catch(const FileException & e) {
			dcdebug("Error while reading file: %s\n", e.what());
		}

		{
			RLock l(qm->cs);
			// get q again in case it has been (re)moved
			q = qm->fileQueue.findFile(file);
		}

		if(!q)
			continue;

		ttFile.finalize();

		if(ttFile.getRoot() == tth) {
			//If no bad blocks then the file probably got stuck in the temp folder for some reason
			qm->moveStuckFile(q);
			continue;
		}

		size_t pos = 0;

		{
			WLock l(qm->cs);
			boost::for_each(tt.getLeaves(), ttFile.getLeaves(), [&](const TTHValue& our, const TTHValue& file) {
				if(our == file) {
					q->addFinishedSegment(Segment(pos, tt.getBlockSize()));
				}

				pos += tt.getBlockSize();
			});
		}

		qm->rechecked(q);
	}
	return 0;
}

QueueManager::QueueManager() : 
	lastSave(0),
	lastAutoPrio(0),
	rechecker(this),
	udp(Socket::TYPE_UDP),
	tasks(true)
{ 
	//add listeners in loadQueue
	File::ensureDirectory(Util::getListPath());
	File::ensureDirectory(Util::getBundlePath());
}

QueueManager::~QueueManager() { 
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this); 
	ClientManager::getInstance()->removeListener(this);
	HashManager::getInstance()->removeListener(this);

	saveQueue(false);

	if(!SETTING(KEEP_LISTS)) {
		string path = Util::getListPath();

		std::sort(protectedFileLists.begin(), protectedFileLists.end());

		StringList filelists = File::findFiles(path, "*.xml.bz2", File::TYPE_FILE);
		std::sort(filelists.begin(), filelists.end());
		std::for_each(filelists.begin(), std::set_difference(filelists.begin(), filelists.end(),
			protectedFileLists.begin(), protectedFileLists.end(), filelists.begin()), &File::deleteFile);
	}
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

void QueueManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	BundlePtr bundle;
	vector<const PartsInfoReqParam*> params;

	{
		RLock l(cs);

		//find max 10 pfs sources to exchange parts
		//the source basis interval is 5 minutes
		FileQueue::PFSSourceList sl;
		fileQueue.findPFSSources(sl);

		for(auto& i: sl) {
			auto source = i.first->getPartialSource();
			const QueueItemPtr qi = i.second;

			PartsInfoReqParam* param = new PartsInfoReqParam;
				
			qi->getPartialInfo(param->parts, qi->getBlockSize());
			
			param->tth = qi->getTTH().toBase32();
			param->ip  = source->getIp();
			param->udpPort = source->getUdpPort();
			param->myNick = source->getMyNick();
			param->hubIpPort = source->getHubIpPort();

			params.push_back(param);

			source->setPendingQueryCount((uint8_t)(source->getPendingQueryCount() + 1));
			source->setNextQueryTime(aTick + 300000);		// 5 minutes
		}

		if (SETTING(AUTO_SEARCH) && SETTING(AUTO_ADD_SOURCE))
			bundle = bundleQueue.findSearchBundle(aTick); //may modify the recent search queue
	}

	if(bundle) {
		searchBundle(bundle, false);
	}

	// Request parts info from partial file sharing sources
	for(auto& param: params){
		//dcassert(param->udpPort > 0);
		
		try {
			AdcCommand cmd = SearchManager::getInstance()->toPSR(true, param->myNick, param->hubIpPort, param->tth, param->parts);
			COMMAND_DEBUG(cmd.toString(), DebugManager::TYPE_CLIENT_UDP, DebugManager::OUTGOING, param->ip);
			udp.writeTo(param->ip, param->udpPort, cmd.toString(ClientManager::getInstance()->getMyCID()));
		} catch(...) {
			dcdebug("Partial search caught error\n");		
		}
		
		delete param;
	}
}

bool QueueManager::hasDownloadedBytes(const string& aTarget) throw(QueueException) {
	RLock l(cs);
	auto q = fileQueue.findFile(aTarget);
	if (!q)
		throw QueueException(STRING(TARGET_REMOVED));

	return q->getDownloadedBytes() > 0;
}

void QueueManager::addList(const HintedUser& aUser, Flags::MaskType aFlags, const string& aInitialDir /* = Util::emptyString */, BundlePtr aBundle /*nullptr*/) throw(QueueException, FileException) {
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
	{
		WLock l(cs);
		auto ret = fileQueue.add(target, -1, (Flags::MaskType)(QueueItem::FLAG_USER_LIST | aFlags), QueueItem::HIGHEST, aInitialDir, GET_TIME(), TTHValue());
		if (!ret.second) {
			//exists already
			throw QueueException(STRING(LIST_ALREADY_QUEUED));
		}

		auto q = move(ret.first);
		addSource(q, aUser, true, false, false);
		if (aBundle) {
			q->setFlag(QueueItem::FLAG_MATCH_BUNDLE);
			matchLists.insert(StringMultiBiMap::value_type(aBundle->getToken(), q->getTarget()));
		}

		fire(QueueManagerListener::Added(), q);
	}

	//connect
	ConnectionManager::getInstance()->getDownloadConnection(aUser, (aFlags & QueueItem::FLAG_PARTIAL_LIST) || (aFlags & QueueItem::FLAG_TTHLIST_BUNDLE));
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
			bundleQueue.removeFinishedItem(q);
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

void QueueManager::validateBundleFile(const string& aBundleDir, string& aBundleFile, const TTHValue& aTTH, QueueItemBase::Priority& aPrio) const throw(QueueException, FileException, DupeException) {

	//check the skiplist
	string::size_type i = 0;
	string::size_type j = i + 1;

	auto matchSkipList = [&] (string&& aName) -> void {
		if(skipList.match(aName)) {
			throw QueueException(STRING(DOWNLOAD_SKIPLIST_MATCH));
		}
	};

	//match the file name
	matchSkipList(Util::getFileName(aBundleFile));

	//match all dirs (if any)
	while((i = aBundleFile.find(PATH_SEPARATOR, j)) != string::npos) {
		matchSkipList(aBundleFile.substr(j, i - j));
		j = i + 1;
	}


	//validate the target and check the existance
	aBundleFile = checkTarget(aBundleFile, aBundleDir);

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
		if (q && q->getTarget() != aBundleDir + aBundleFile) {
			auto path = AirUtil::subtractCommonDirs(aBundleDir, q->getFilePath(), PATH_SEPARATOR);
			throw DupeException(STRING_F(FILE_ALREADY_QUEUED, path));
		}
	}

	if(SETTING(USE_FTP_LOGGER)) {
		AirUtil::fileEvent(aBundleDir + aBundleFile);
	}


	//valid file

	//set the prio
	if (highPrioFiles.match(Util::getFileName(aBundleFile))) {
		aPrio = SETTING(PRIO_LIST_HIGHEST) ? QueueItem::HIGHEST : QueueItem::HIGH;
	}
}

void QueueManager::addOpenedItem(const string& aFileName, int64_t aSize, const TTHValue& aTTH, const HintedUser& aUser, bool isClientView) throw(QueueException, FileException) {
	//check the source
	if (aUser.user)
		checkSource(aUser);

	//check the size
	if (aSize == 0) {
		//can't view this...
		throw QueueException(STRING(CANT_OPEN_EMPTY_FILE));
	} else if (isClientView && aSize > Util::convertSize(1, Util::MB)) {
		auto msg = STRING_F(VIEWED_FILE_TOO_BIG, aFileName % Util::formatBytes(aSize));
		LogManager::getInstance()->message(msg, LogManager::LOG_ERROR);
		throw QueueException(msg);
	}

	//check the target
	string target = Util::getOpenPath(Util::validatePath(aFileName));

	//add in queue
	QueueItemPtr qi = nullptr;
	bool wantConnection = false;
	{
		WLock l(cs);
		auto ret = fileQueue.add(target, aSize, (Flags::MaskType)(isClientView ? (QueueItem::FLAG_TEXT | QueueItem::FLAG_CLIENT_VIEW) : QueueItem::FLAG_OPEN), QueueItem::HIGHEST, Util::emptyString, GET_TIME(), aTTH);
		qi = move(ret.first);
		wantConnection = addSource(qi, aUser, true, false, false);

		if (ret.second)
			fire(QueueManagerListener::Added(), qi);
	}

	//connect
	if(wantConnection || qi->usesSmallSlot()) {
		ConnectionManager::getInstance()->getDownloadConnection(aUser, qi->usesSmallSlot());
	}
}

BundlePtr QueueManager::getBundle(const string& aTarget, QueueItemBase::Priority aPrio, time_t aDate, bool isFileBundle) noexcept {
	auto b = bundleQueue.getMergeBundle(aTarget);
	if (!b) {
		// create a new bundle
		b = BundlePtr(new Bundle(aTarget, GET_TIME(), aPrio, aDate, Util::emptyString, true, isFileBundle));
	} else {
		// use an existing one

		if (AirUtil::isSub(b->getTarget(), aTarget)) {
			//the target bundle is a sub directory of the source bundle

			string oldTarget = b->getTarget();
			int mergedBundles = changeBundleTarget(b, aTarget);

			fire(QueueManagerListener::BundleMerged(), b, oldTarget);

			//report
			string tmp = STRING_F(BUNDLE_CREATED, b->getName().c_str() % b->getQueueItems().size()) + " (" + CSTRING_F(TOTAL_SIZE, Util::formatBytes(b->getSize()).c_str()) + ")";
			if (mergedBundles > 0)
				tmp += ", " + STRING_F(EXISTING_BUNDLES_MERGED, mergedBundles);

			LogManager::getInstance()->message(tmp, LogManager::LOG_INFO);
		}
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

	void setErrorMsg(string& errorMsg_) {
		if (!errors.empty()) {
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

			//throw (long errors will crash the debug build.. fix maybe)
			errorMsg_ = Util::toString(", ", msg);
		}
	}

private:
	int fileCount;
	unordered_multimap<string, Error> errors;
};

BundlePtr QueueManager::createDirectoryBundle(const string& aTarget, const HintedUser& aUser, BundleFileList& aFiles, QueueItemBase::Priority aPrio, time_t aDate, string& errorMsg_) throw(QueueException, FileException) {
	string target = formatBundleTarget(aTarget, aDate);

	int fileCount = aFiles.size();

	//check the source
	if (aUser.user) {
		//it's up to the caller to catch this one
		checkSource(aUser);
	}

	ErrorReporter errors(fileCount);

	int existingFiles = 0, smallDupes=0;

	//check the files
	for (auto i = aFiles.begin(); i != aFiles.end(); ) {
		try {
			validateBundleFile(target, (*i).file, (*i).tth, (*i).prio);
			i++;
			continue;
		} catch(QueueException& e) {
			errors.add(e.getError(), (*i).file, false);
		} catch(FileException& /*e*/) {
			existingFiles++;
		} catch(DupeException& e) {
			bool isSmall = (*i).size < Util::convertSize(SETTING(MIN_DUPE_CHECK_SIZE), Util::KB);
			errors.add(e.getError(), (*i).file, isSmall);
			if (isSmall) {
				smallDupes++;
				i++;
				continue;
			}
		}

		i = aFiles.erase(i);
	}


	//check the errors
	if (aFiles.empty()) {
		//report existing files only if all files exist to prevent useless spamming
		if (existingFiles == fileCount) {
			errorMsg_ = STRING_F(ALL_BUNDLE_FILES_EXIST, existingFiles);
			return nullptr;
		}

		errors.setErrorMsg(errorMsg_);
		return nullptr;
	} else if (smallDupes > 0) {
		if (smallDupes == static_cast<int>(aFiles.size())) {
			//no reason to continue if all remaining files are dupes
			errors.setErrorMsg(errorMsg_);
			return nullptr;
		} else {
			//those will get queued, don't report
			errors.clearMinor();
		}
	}



	BundlePtr b = nullptr;
	bool wantConnection = false;
	int added = 0;

	{
		WLock l(cs);
		//get the bundle
		b = getBundle(target, aPrio, aDate, false);

		//add the files
		for (auto& bfi: aFiles) {
			try {
				if (addFile(target + bfi.file, bfi.size, bfi.tth, aUser, 0, true, bfi.prio, wantConnection, b))
					added++;
			} catch(QueueException& e) {
				errors.add(e.getError(), bfi.file, false);
			} catch(FileException& /*e*/) {
				//the file has finished after we made the initial target check, don't add error for those
				existingFiles++;
			}
		}

		if (!addBundle(b, target, added)) {
			if (existingFiles == fileCount) {
				errorMsg_ = STRING_F(ALL_BUNDLE_FILES_EXIST, existingFiles);
				return nullptr;
			}

			b = nullptr;
		}
	}

	if (wantConnection) {
		//connect to the source (we must have an user in this case)
		fire(QueueManagerListener::SourceFilesUpdated(), aUser);
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
	}

	if (b)
		errors.clearMinor();

	errors.setErrorMsg(errorMsg_);
	return b;
}

string QueueManager::formatBundleTarget(const string& aPath, time_t aRemoteDate) noexcept {
	return Util::validatePath(Util::formatTime(aPath, (SETTING(FORMAT_DIR_REMOTE_TIME) && aRemoteDate > 0) ? aRemoteDate : GET_TIME()));
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
	{
		WLock l(cs);
		//get the bundle
		b = getBundle(target, aPrio, aDate, true);

		//add the file
		bool added = addFile(target, aSize, aTTH, aUser, aFlags, true, aPrio, wantConnection, b);

		if (!addBundle(b, target, added ? 1 : 0)) {
			b = nullptr;
		}
	}

	if (wantConnection) {
		//connect to the source (we must have an user in this case)
		fire(QueueManagerListener::SourceFilesUpdated(), aUser);
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
	}

	return b;
}

bool QueueManager::addFile(const string& aTarget, int64_t aSize, const TTHValue& root, const HintedUser& aUser, Flags::MaskType aFlags /* = 0 */, 
								   bool addBad /* = true */, QueueItemBase::Priority aPrio, bool& wantConnection, BundlePtr& aBundle) throw(QueueException, FileException)
{
	//handle zero byte items
	if(aSize == 0) {
		if(!SETTING(SKIP_ZERO_BYTE)) {
			File::ensureDirectory(aTarget);
			File f(aTarget, File::WRITE, File::CREATE);
			if (aBundle) {
				auto qi = QueueItemPtr(new QueueItem(aTarget, aSize, aPrio, aFlags, GET_TIME(), root, aTarget));
				aBundle->addFinishedItem(qi, false);
			}
			return true;
		}
		return false;
	}

	// add the file
	auto ret = fileQueue.add(aTarget, aSize, aFlags, aPrio, Util::emptyString, GET_TIME(), root);

	if(!ret.second) {
		// exists already
		if (replaceItem(ret.first, aSize, root)) {
			ret = move(fileQueue.add(aTarget, aSize, aFlags, aPrio, Util::emptyString, GET_TIME(), root));
		}
	}
	
	//add in the bundle
	if (ret.second && aBundle) {
		//doesn't matter if the bundle is forced paused
		if (aBundle->getPriority() == Bundle::PAUSED && ret.first->getPriority() == QueueItem::HIGHEST) {
			ret.first->setPriority(QueueItem::HIGH);
		}
		bundleQueue.addBundleItem(ret.first, aBundle);

		//an old bundle?
		if (aBundle->getStatus() != Bundle::STATUS_NEW && ret.first)
			fire(QueueManagerListener::Added(), ret.first);
	}

	//add the source
	if (aUser.user) {
		try {
			if (addSource(ret.first, aUser, (Flags::MaskType)(addBad ? QueueItem::Source::FLAG_MASK : 0), true, false)) {
				wantConnection = true;
				if (!ret.second)
					fire(QueueManagerListener::SourcesUpdated(), ret.first);
			}
		} catch(const Exception&) {
			dcassert(!ret.second);
			//This should never fail for new items, and for existing items it doesn't matter (useless spam)
		}
	}

	return ret.second;
}

void QueueManager::readdQISource(const string& target, const HintedUser& aUser) throw(QueueException) {
	bool wantConnection = false;
	{
		WLock l(cs);
		QueueItemPtr q = fileQueue.findFile(target);
		if(q && q->isBadSource(aUser)) {
			wantConnection = addSource(q, aUser, QueueItem::Source::FLAG_MASK);
		}
	}

	fire(QueueManagerListener::SourceFilesUpdated(), aUser.user);
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

void QueueManager::readdBundleSource(BundlePtr aBundle, const HintedUser& aUser) noexcept {
	bool wantConnection = false;
	{
		WLock l(cs);
		for(auto& q: aBundle->getQueueItems()) {
			dcassert(!q->isSource(aUser));
			if(q && q->isBadSource(aUser.user)) {
				try {
					if (addSource(q, aUser, QueueItem::Source::FLAG_MASK)) {
						wantConnection = true;
					}
				} catch(...) {
					LogManager::getInstance()->message("Failed to add the source for " + q->getTarget(), LogManager::LOG_WARNING);
				}
			}
		}
	}

	fire(QueueManagerListener::SourceFilesUpdated(), aUser.user);
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
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
	int64_t size = File::getSize(aParentDir + target);
	if(size != -1) {
		/* TODO: add for recheck */
		throw FileException(STRING(TARGET_FILE_EXISTS));
	}
	return target;	
}

/** Add a source to an existing queue item */
bool QueueManager::addSource(QueueItemPtr& qi, const HintedUser& aUser, Flags::MaskType addBad, bool newBundle /*false*/, bool checkTLS /*true*/) throw(QueueException, FileException) {
	if (!aUser.user) //atleast magnet links can cause this to happen.
		throw QueueException("Can't find Source user to add For Target: " + qi->getTargetFileName());

	if (checkTLS && !aUser.user->isSet(User::NMDC) && !aUser.user->isSet(User::TLS) && SETTING(TLS_MODE) == SettingsManager::TLS_FORCED) {
		throw QueueException(STRING(SOURCE_NO_ENCRYPTION));
	}

	if(qi->isFinished()) //no need to add source to finished item.
		throw QueueException("Already Finished: " + Util::getFileName(qi->getTarget()));
	
	bool wantConnection = qi->startDown();
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
	userQueue.addQI(qi, aUser, newBundle, isBad);

#ifdef _WIN32
	if ((!SETTING(SOURCEFILE).empty()) && (!SETTING(SOUNDS_DISABLED)))
		PlaySound(Text::toT(SETTING(SOURCEFILE)).c_str(), NULL, SND_FILENAME | SND_ASYNC);
#endif
	
	if (!newBundle) {
		fire(QueueManagerListener::SourcesUpdated(), qi);
	}
	if (qi->getBundle()) {
		qi->getBundle()->setDirty();
	}

	return wantConnection;
	
}

Download* QueueManager::getDownload(UserConnection& aSource, const StringSet& runningBundles, const OrderedStringSet& onlineHubs, string& lastError_, string& newUrl, QueueItemBase::DownloadType aType) noexcept{
	QueueItemPtr q = nullptr;
	const UserPtr& u = aSource.getUser();
	bool hasDownload = false;
	{
		WLock l(cs);
		dcdebug("Getting download for %s...", u->getCID().toBase32().c_str());

		q = userQueue.getNext(aSource.getUser(), runningBundles, onlineHubs, lastError_, hasDownload, QueueItem::LOWEST, aSource.getChunkSize(), aSource.getSpeed(), aType);
		if (q) {
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
		} else {
			dcdebug("none\n");
			return nullptr;
		}

		// Check that the file we will be downloading to exists
		if (q->getDownloadedBytes() > 0) {
			if (!Util::fileExists(q->getTempTarget())) {
				// Temp target gone?
				q->resetDownloaded();
			}
		}

		Download* d = new Download(aSource, *q);
		userQueue.addDownload(q, d);

		fire(QueueManagerListener::SourcesUpdated(), q);
		dcdebug("found %s for %s (" I64_FMT ", " I64_FMT ")\n", q->getTarget().c_str(), d->getToken().c_str(), d->getSegment().getStart(), d->getSegment().getEnd());
		return d;
	}
}

bool QueueManager::allowStartQI(const QueueItemPtr& aQI, const StringSet& runningBundles, string& lastError_, bool mcn /*false*/) noexcept{
	// nothing to download?
	if (!aQI)
		return false;

	// override the slot settings for partial lists and small files
	if (aQI->usesSmallSlot())
		return true;

	// paused?
	if (aQI->isPausedPrio() || (aQI->getBundle() && aQI->getBundle()->isPausedPrio()))
		return false;

	size_t downloadCount = DownloadManager::getInstance()->getDownloadCount();
	bool slotsFull = (AirUtil::getSlots(true) != 0) && (downloadCount >= (size_t) AirUtil::getSlots(true));
	bool speedFull = (AirUtil::getSpeedLimit(true) != 0) && (DownloadManager::getInstance()->getRunningAverage() >= Util::convertSize(AirUtil::getSpeedLimit(true), Util::KB));
	//LogManager::getInstance()->message("Speedlimit: " + Util::toString(Util::getSpeedLimit(true)*1024) + " slots: " + Util::toString(Util::getSlots(true)) + " (avg: " + Util::toString(getRunningAverage()) + ")");

	if (slotsFull | speedFull) {
		bool extraFull = (AirUtil::getSlots(true) != 0) && (downloadCount >= (size_t) (AirUtil::getSlots(true) + SETTING(EXTRA_DOWNLOAD_SLOTS)));
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

bool QueueManager::startDownload(const UserPtr& aUser, const StringSet& runningBundles, const OrderedStringSet& onlineHubs, 
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
	string& bundleToken, bool& allowUrlChange, bool& hasDownload, string& lastError_) noexcept{

	StringSet runningBundles;
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

void QueueManager::matchListing(const DirectoryListing& dl, int& matches, int& newFiles, BundleList& bundles) noexcept {
	if (dl.getUser() == ClientManager::getInstance()->getMe())
		return;

	bool wantConnection = false;
	QueueItem::StringItemList ql;

	{
		RLock l(cs);
		fileQueue.matchListing(dl, ql);
	}

	{
		WLock l(cs);
		for(auto& sqp: ql) {
			try {
				if (addSource(sqp.second, dl.getHintedUser(), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
					wantConnection = true;
				}
				newFiles++;
			} catch(const Exception&) {
				//...
			}
			if (sqp.second->getBundle() && find(bundles.begin(), bundles.end(), sqp.second->getBundle()) == bundles.end()) {
				bundles.push_back(sqp.second->getBundle());
			}
		}
	}

	if (newFiles > 0)
		fire(QueueManagerListener::SourceFilesUpdated(), dl.getUser());

	matches = (int)ql.size();
	if(wantConnection)
		ConnectionManager::getInstance()->getDownloadConnection(dl.getHintedUser());
}

bool QueueManager::getQueueInfo(const HintedUser& aUser, string& aTarget, int64_t& aSize, int& aFlags, string& bundleToken) noexcept {
	OrderedStringSet hubs;
	hubs.insert(aUser.hint);

	StringSet runningBundles;
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

void QueueManager::onSlowDisconnect(const string& aToken) noexcept {
	RLock l(cs);
	auto b = bundleQueue.findBundle(aToken);
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

void QueueManager::readLockedOperation(const function<void (const QueueItem::StringMap&)>& currentQueue) {
	RLock l(cs);
	if(currentQueue) currentQueue(fileQueue.getQueue());
}

void QueueManager::moveFile(const string& source, const string& target, const QueueItemPtr& q) {
	tasks.addTask(new DispatcherQueue::Callback([=] { moveFileImpl(source, target, q); }));
}

void QueueManager::moveFileImpl(const string& source, const string& target, QueueItemPtr qi) {
	try {
		File::ensureDirectory(target);
		UploadManager::getInstance()->abortUpload(source);
		File::renameFile(source, target);
		
		if (SETTING(DCTMP_STORE_DESTINATION) && qi->getBundle() && !qi->getBundle()->isFileBundle() && compare(Util::getFilePath(source), Util::getFilePath(target)) != 0) {
			// the bundle was moved? try to remove the old main bundle dir
			auto p = source.find(qi->getBundle()->getName()); //was the bundle dir renamed?
			auto dir = p != string::npos ? source.substr(0, p + qi->getBundle()->getName().length() + 1) : AirUtil::subtractCommonDirs(Util::getFilePath(target), Util::getFilePath(source), PATH_SEPARATOR);
			AirUtil::removeDirectoryIfEmpty(dir, 3, true);
		}
	} catch(const FileException& e1) {
		// Try to just rename it to the correct name at least
		string newTarget = Util::getFilePath(source) + Util::getFileName(target);
		try {
			File::renameFile(source, newTarget);
			LogManager::getInstance()->message(STRING_F(MOVE_FILE_FAILED, newTarget % Util::getFilePath(target) % e1.getError()), LogManager::LOG_ERROR);
		} catch(const FileException& e2) {
			LogManager::getInstance()->message(STRING(UNABLE_TO_RENAME) + " " + source + ": " + e2.getError(), LogManager::LOG_ERROR);
		}
	}
	if(SETTING(USE_FTP_LOGGER))
		AirUtil::fileEvent(target, true);

	if (qi && qi->getBundle()) {
		getInstance()->handleMovedBundleItem(qi);
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
		auto s = find_if(b->getFinishedFiles(), [qi](QueueItemPtr aQI) { return aQI->getTarget() == qi->getTarget(); });
		if (s != b->getFinishedFiles().end()) {
			qi->setFlag(QueueItem::FLAG_MOVED);
		} else if (b->getFinishedFiles().empty() && b->getQueueItems().empty()) {
			//the bundle was removed while the file was being moved?
			return;
		}
	}

	checkBundleFinished(b, qi->isSet(QueueItem::FLAG_PRIVATE));
}


void QueueManager::checkBundleFinished(BundlePtr& aBundle, bool isPrivate) noexcept {
	bool hasNotifications = false;
	{
		RLock l (cs);
		//check if there are queued or non-moved files remaining
		if (!aBundle->allowHash()) 
			return;

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
		LogManager::getInstance()->message(STRING_F(DL_BUNDLE_FINISHED, aBundle->getName().c_str()), LogManager::LOG_INFO);
		setBundleStatus(aBundle, Bundle::STATUS_FINISHED);
	} else if (!scanBundle(aBundle)) {
		return;
	} 

	if (SETTING(ADD_FINISHED_INSTANTLY)) {
		hashBundle(aBundle);
	} else if (!isPrivate) {
		if (ShareManager::getInstance()->allowAddDir(aBundle->getTarget())) {
			LogManager::getInstance()->message(CSTRING(INSTANT_SHARING_DISABLED), LogManager::LOG_INFO);
		} else {
			LogManager::getInstance()->message(STRING_F(NOT_IN_SHARED_DIR, aBundle->getTarget().c_str()), LogManager::LOG_INFO);
		}
	} else {
		removeFinishedBundle(aBundle);
	}
}

bool QueueManager::scanBundle(BundlePtr& aBundle) noexcept {
	string error_;
	auto newStatus = ShareScannerManager::getInstance()->onScanBundle(aBundle, error_);
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
				if (ShareManager::getInstance()->checkSharedName(qi->getTarget(), Text::toLower(qi->getTarget()), false, false, qi->getSize()) && Util::fileExists(qi->getTarget())) {
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
				bundleQueue.removeFinishedItem(q);
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
						if (HashManager::getInstance()->addFile(Text::toLower(q->getTarget()), fi)) {
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
			LogManager::getInstance()->message(STRING_F(BUNDLE_ADDED_FOR_HASH, aBundle->getName() % Util::formatBytes(hashSize)), LogManager::LOG_INFO);
		} else {
			//all files have been hashed already?
			checkBundleHashed(aBundle);
		}
	} else if (!aBundle->getQueueItems().empty() && !aBundle->getQueueItems().front()->isSet(QueueItem::FLAG_PRIVATE)) {
		//if (SETTING(ADD_FINISHED_INSTANTLY)) {
			LogManager::getInstance()->message(STRING_F(NOT_IN_SHARED_DIR, aBundle->getTarget().c_str()), LogManager::LOG_INFO);
		//} else {
		//	LogManager::getInstance()->message(CSTRING(INSTANT_SHARING_DISABLED), LogManager::LOG_INFO);
		//}
	} else {
		removeFinishedBundle(aBundle);
	}
}

void QueueManager::removeFinishedBundle(BundlePtr& aBundle) noexcept {
	WLock l(cs);
	for(auto i = aBundle->getFinishedFiles().begin(); i != aBundle->getFinishedFiles().end(); ) {
		fileQueue.remove(*i);
		i = aBundle->getFinishedFiles().erase(i);
	}

	bundleQueue.removeBundle(aBundle);
	fire(QueueManagerListener::BundleRemoved(), aBundle);
}

void QueueManager::onFileHashed(const string& aPath, HashedFile& aFileInfo, bool failed) noexcept {
	QueueItemPtr q;
	{
		RLock l(cs);

		//prefer the exact path match
		q = fileQueue.findFile(aPath);
		if (!q) {
			//also remove bundles that haven't been removed in a shared directories... remove this when the bundles are shown correctly in GUI

			auto tpi = make_pair(fileQueue.getTTHIndex().begin(), fileQueue.getTTHIndex().end());
			if (!failed) {
				//we have the tth so we can limit the range
				tpi = fileQueue.getTTHIndex().equal_range(const_cast<TTHValue*>(&aFileInfo.getRoot()));
			}

			if (tpi.first != tpi.second) {
				int64_t size = 0;
				if (failed) {
					size = File::getSize(aPath);
				}

				auto file = Util::getFileName(aPath);
				auto p = find_if(tpi | map_values, [&](QueueItemPtr aQI) { return (!failed || size == aQI->getSize()) && aQI->getBundle() && aQI->getBundle()->getStatus() == Bundle::STATUS_HASHING && 
					aQI->getTargetFileName() == file && aQI->isFinished() && !aQI->isSet(QueueItem::FLAG_HASHED); });

				if (p.base() != tpi.second) {
					q = *p;
				}
			}
		}
	}

	if (!q) {
		if (!failed) {
			fire(QueueManagerListener::FileHashed(), aPath, aFileInfo);
		}
		return;
	}

	BundlePtr b = q->getBundle();
	if (!b)
		return;


	q->setFlag(QueueItem::FLAG_HASHED);
	if (failed) {
		setBundleStatus(b, Bundle::STATUS_HASH_FAILED);
	} else if (b->getStatus() != Bundle::STATUS_HASHING && b->getStatus() != Bundle::STATUS_HASH_FAILED) {
		//instant sharing disabled/the folder wasn't shared when the bundle finished
		fire(QueueManagerListener::FileHashed(), aPath, aFileInfo);
	}

	checkBundleHashed(b);
}

void QueueManager::checkBundleHashed(BundlePtr& b) noexcept {
	bool fireHashed = false;
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
				LogManager::getInstance()->message(STRING_F(BUNDLE_HASH_FAILED, b->getTarget().c_str()), LogManager::LOG_ERROR);
				return;
			} else if (b->getStatus() == Bundle::STATUS_HASHING) {
				fireHashed = true;
			} else {
				//instant sharing disabled/the folder wasn't shared when the bundle finished
			}
		}
	}

	if (fireHashed) {
		if (!b->isFileBundle()) {
			setBundleStatus(b, Bundle::STATUS_HASHED);
		} else {
			try {
				HashedFile fi;
				HashManager::getInstance()->getFileInfo(Text::toLower(b->getFinishedFiles().front()->getTarget()), b->getFinishedFiles().front()->getTarget(), fi);
				fire(QueueManagerListener::FileHashed(), b->getFinishedFiles().front()->getTarget(), fi);
				LogManager::getInstance()->message(STRING_F(SHARED_FILE_ADDED, b->getTarget()), LogManager::LOG_INFO);
			} catch (...) { dcassert(0); }
		}
	}

	removeFinishedBundle(b);
}

void QueueManager::moveStuckFile(QueueItemPtr& qi) {
	moveFile(qi->getTempTarget(), qi->getTarget(), nullptr);

	if(qi->isFinished()) {
		WLock l(cs);
		userQueue.removeQI(qi);
	}

	string target = qi->getTarget();

	if(!SETTING(KEEP_FINISHED_FILES)) {
		fire(QueueManagerListener::Removed(), qi, true);
		fileQueue.remove(qi);
		removeBundleItem(qi, true);
	 } else {
		qi->addFinishedSegment(Segment(0, qi->getSize()));
		fire(QueueManagerListener::StatusUpdated(), qi);
	}

	fire(QueueManagerListener::RecheckAlreadyFinished(), target);
}

void QueueManager::rechecked(QueueItemPtr& qi) {
	fire(QueueManagerListener::RecheckDone(), qi->getTarget());
	fire(QueueManagerListener::StatusUpdated(), qi);
	if (qi->getBundle()) {
		qi->getBundle()->setDirty();
	}
}

void QueueManager::putDownload(Download* aDownload, bool finished, bool noAccess /*false*/, bool rotateQueue /*false*/) throw(HashException) {
	HintedUserList getConn;
 	string fl_fname;
	int fl_flag = 0;
	QueueItemPtr q = nullptr;
	bool removeFinished = false;

	// Make sure the download gets killed
	unique_ptr<Download> d(aDownload);
	aDownload = nullptr;

	d->close();

	{
		WLock l(cs);
		q = fileQueue.findFile(d->getPath());
		if(!q) {
			// Target has been removed, clean up the mess
			auto hasTempTarget = !d->getTempTarget().empty();
			auto isFullList = d->getType() == Transfer::TYPE_FULL_LIST;
			auto isFile = d->getType() == Transfer::TYPE_FILE && d->getTempTarget() != d->getPath();

			if(hasTempTarget && (isFullList || isFile)) {
				File::deleteFileEx(d->getTempTarget());
			}

			return;
		}

		if (q->isSet(QueueItem::FLAG_FINISHED)) {
			return;
		}

		if(!finished) {
			if(d->getType() == Transfer::TYPE_FULL_LIST && !d->getTempTarget().empty()) {
				// No use keeping an unfinished file list...
				File::deleteFile(d->getTempTarget());
			}

			if(d->getType() != Transfer::TYPE_TREE && q->getDownloadedBytes() == 0) {
				if(d->getType() == Transfer::TYPE_FILE)
					File::deleteFile(d->getTempTarget());
				q->setTempTarget(Util::emptyString);
			}

			if(d->getType() == Transfer::TYPE_FILE) {
				// mark partially downloaded chunk, but align it to block size
				int64_t downloaded = d->getPos();
				downloaded -= downloaded % d->getTigerTree().getBlockSize();

				if(downloaded > 0) {
					q->addFinishedSegment(Segment(d->getStartPos(), downloaded));
				}

				if (rotateQueue && q->getBundle()) {
					q->getBundle()->rotateUserQueue(q, d->getUser());
				}
			}

			if (noAccess) {
				q->blockSourceHub(d->getHintedUser());
			}

			if(!q->isPausedPrio()) {
				q->getOnlineUsers(getConn);
			}

			userQueue.removeDownload(q, d->getToken());
			fire(QueueManagerListener::StatusUpdated(), q);
		} else { // Finished
			if(d->getType() == Transfer::TYPE_PARTIAL_LIST) {
				if( (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD)) ||
					(q->isSet(QueueItem::FLAG_MATCH_QUEUE)) ||
					(q->isSet(QueueItem::FLAG_VIEW_NFO)))
				{					
					fl_fname = d->getPath();
					fl_flag = (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) ? (QueueItem::FLAG_DIRECTORY_DOWNLOAD) : 0)
						| (q->isSet(QueueItem::FLAG_PARTIAL_LIST) ? (QueueItem::FLAG_PARTIAL_LIST) : 0)
						| (q->isSet(QueueItem::FLAG_MATCH_QUEUE) ? QueueItem::FLAG_MATCH_QUEUE : 0) | QueueItem::FLAG_TEXT
						| (q->isSet(QueueItem::FLAG_VIEW_NFO) ? QueueItem::FLAG_VIEW_NFO : 0);
				} else {
					fire(QueueManagerListener::PartialList(), d->getHintedUser(), d->getPFS(), q->getTempTarget());
				}
				userQueue.removeQI(q);
				fire(QueueManagerListener::Removed(), q, true);
				fileQueue.remove(q);
			} else if(d->getType() == Transfer::TYPE_TREE) {
				//add it in hashmanager outside the lock
				userQueue.removeDownload(q, d->getToken());
				fire(QueueManagerListener::StatusUpdated(), q);
			} else if(d->getType() == Transfer::TYPE_FULL_LIST) {
				if(d->isSet(Download::FLAG_XML_BZ_LIST)) {
					q->setFlag(QueueItem::FLAG_XML_BZLIST);
				} else {
					q->unsetFlag(QueueItem::FLAG_XML_BZLIST);
				}

				auto dir = q->getTempTarget(); // We cheated and stored the initial display directory here (when opening lists from search)
				q->addFinishedSegment(Segment(0, q->getSize()));

				// Now, let's see if this was a directory download filelist...
				if( (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD)) ||
					(q->isSet(QueueItem::FLAG_MATCH_QUEUE)) )
				{
					fl_fname = q->getListName();
					fl_flag = (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) ? QueueItem::FLAG_DIRECTORY_DOWNLOAD : 0)
						| (q->isSet(QueueItem::FLAG_MATCH_QUEUE) ? QueueItem::FLAG_MATCH_QUEUE : 0);
				}

				fire(QueueManagerListener::Finished(), q, dir, d->getHintedUser(), d->getAverageSpeed());
				userQueue.removeQI(q);

				fire(QueueManagerListener::Removed(), q, true);
				fileQueue.remove(q);
			} else if(d->getType() == Transfer::TYPE_FILE) {
				d->setOverlapped(false);
				q->addFinishedSegment(d->getSegment());
				//dcdebug("Finish segment");
				dcdebug("Finish segment for %s (" I64_FMT ", " I64_FMT ")\n", d->getToken().c_str(), d->getSegment().getStart(), d->getSegment().getEnd());

				if(q->isFinished()) {
					// Disconnect all possible overlapped downloads
					for(auto aD: q->getDownloads()) {
						if(compare(aD->getToken(), d->getToken()) != 0)
							aD->getUserConnection().disconnect();
					}

					removeFinished = true;
					q->setFlag(QueueItem::FLAG_FINISHED);
					userQueue.removeQI(q);

					if(SETTING(KEEP_FINISHED_FILES)) {
						fire(QueueManagerListener::StatusUpdated(), q);
					} else {
						fire(QueueManagerListener::Removed(), q, true);
						if (!d->getBundle())
							fileQueue.remove(q);
					}
				} else {
					userQueue.removeDownload(q, d->getToken());
					fire(QueueManagerListener::StatusUpdated(), q);
				}
			} else {
				dcassert(0);
			}
		}
	}
	
	if (d->getType() == Transfer::TYPE_TREE && finished) {
		// Got a full tree, now add it to the HashManager
		dcassert(d->getTreeValid());
		HashManager::getInstance()->addTree(d->getTigerTree());
	}

	if (removeFinished) {
		if (q->getBundle()) {
			removeBundleItem(q, true);
		}

		if(SETTING(LOG_DOWNLOADS)) {
			ParamMap params;
			d->getParams(d->getUserConnection(), params);
			LOG(LogManager::DOWNLOAD, params);
		}

		// Check if we need to move the file
		if(!d->getTempTarget().empty() && (Util::stricmp(d->getPath().c_str(), d->getTempTarget().c_str()) != 0) ) {
			moveFile(d->getTempTarget(), q->getTarget(), q);
		}

		fire(QueueManagerListener::Finished(), q, Util::emptyString, d->getHintedUser(), d->getAverageSpeed());
	}

	for(const auto& u: getConn) {
		if (u.user != d->getUser())
			ConnectionManager::getInstance()->getDownloadConnection(u);
	}

	if(!fl_fname.empty()) {
		if (d->isSet(Download::FLAG_TTHLIST)) {	 
			matchTTHList(d->getPFS(), d->getHintedUser(), fl_flag);	 
		} else {	 
			DirectoryListingManager::getInstance()->processList(fl_fname, d->getPFS(), d->getHintedUser(), d->getTempTarget(), fl_flag); 
		}

		if (q->isSet(QueueItem::FLAG_MATCH_BUNDLE)) {
			WLock l(cs);
			matchLists.right.erase(q->getTarget());
		}
	}
}

void QueueManager::setSegments(const string& aTarget, uint8_t aSegments) noexcept {
	RLock l (cs);
	auto qi = fileQueue.findFile(aTarget);
	if (qi) {
		qi->setMaxSegments(aSegments);
	}
}

void QueueManager::matchTTHList(const string& name, const HintedUser& user, int flags) noexcept {
	dcdebug("matchTTHList");
	if(flags & QueueItem::FLAG_MATCH_QUEUE) {
		bool wantConnection = false;
		int matches = 0;
		 
		typedef vector<TTHValue> TTHList;
		TTHList tthList;
 	 
		size_t start = 0;
		while (start+39 < name.length()) {
			tthList.emplace_back(name.substr(start, 39));
			start = start+40;
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

		if (!ql.empty()) {
			WLock l (cs);
			for(auto& qi: ql) {
				try {
					if (addSource(qi, user, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
						wantConnection = true;
					}
				} catch(...) {
					// Ignore...
				}
				matches++;
			}

			fire(QueueManagerListener::SourceFilesUpdated(), user.user);
		}

		if((matches > 0) && wantConnection)
			ConnectionManager::getInstance()->getDownloadConnection(user); 
	}
}

void QueueManager::recheck(const string& aTarget) {
	rechecker.add(aTarget);
}

void QueueManager::removeFile(const string aTarget) noexcept {
	QueueItemPtr qi = nullptr;
	{
		RLock l(cs);
		qi = fileQueue.findFile(aTarget);
	}

	if (qi) {
		removeQI(qi);
	}
}

void QueueManager::removeQI(QueueItemPtr& q, bool moved /*false*/) noexcept {
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

	if (!moved) {
		fire(QueueManagerListener::Removed(), q, false);
	}

	removeBundleItem(q, false, moved);
	for (auto& token : x)
		ConnectionManager::getInstance()->disconnect(token);
}

void QueueManager::removeFileSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType reason, bool removeConn /* = true */) noexcept {
	QueueItemPtr qi = nullptr;
	{
		RLock l(cs);
		qi = fileQueue.findFile(aTarget);
	}

	if (qi) {
		removeFileSource(qi, aUser, reason, removeConn);
		fire(QueueManagerListener::SourceFilesUpdated(), aUser);
	}
}

#define MAX_SIZE_WO_TREE 20*1024*1024

void QueueManager::removeFileSource(QueueItemPtr& q, const UserPtr& aUser, Flags::MaskType reason, bool removeConn /* = true */) noexcept {
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

		if (reason == QueueItem::Source::FLAG_NO_TREE) {
			q->getSource(aUser)->setFlag(reason);
			if (q->getSize() < MAX_SIZE_WO_TREE) {
				return;
			}
		}

		if(q->isRunning()) {
			isRunning = true;
		}
		userQueue.removeQI(q, aUser, false, true, true);
		q->removeSource(aUser, reason);
		
		fire(QueueManagerListener::SourcesUpdated(), q);

		if (q->getBundle()) {
			q->getBundle()->setDirty();
		}
	}
endCheck:
	if(isRunning && removeConn) {
		DownloadManager::getInstance()->abortDownload(q->getTarget(), aUser);
	}

	if(removeCompletely) {
		removeQI(q);
	}
}

void QueueManager::removeSource(const UserPtr& aUser, Flags::MaskType reason, std::function<bool (const QueueItemPtr&) > excludeF /*nullptr*/) noexcept {
	// @todo remove from finished items
	QueueItemList ql;

	{
		RLock l(cs);
		userQueue.getUserQIs(aUser, ql);

		if (excludeF) {
			ql.erase(remove_if(ql.begin(), ql.end(), excludeF), ql.end());
		}
	}

	for (auto& qi : ql) {
		removeFileSource(qi, aUser, reason);
	}

	fire(QueueManagerListener::SourceFilesUpdated(), aUser);
}

void QueueManager::setBundlePriority(const string& bundleToken, QueueItemBase::Priority p) noexcept {
	BundlePtr bundle = nullptr;
	{
		RLock l(cs);
		bundle = bundleQueue.findBundle(bundleToken);
	}

	setBundlePriority(bundle, p, false);
}

void QueueManager::setBundlePriority(BundlePtr& aBundle, QueueItemBase::Priority p, bool isAuto) noexcept {
	if (!aBundle)
		return;

	QueueItemBase::Priority oldPrio = aBundle->getPriority();
	if (oldPrio == p) {
		return;
	}

	{
		WLock l(cs);
		bundleQueue.removeSearchPrio(aBundle);
		userQueue.setBundlePriority(aBundle, p);
		bundleQueue.addSearchPrio(aBundle);
		bundleQueue.recalculateSearchTimes(aBundle->isRecent(), true);
		if (!isAuto) {
			aBundle->setAutoPriority(false);
		}

		fire(QueueManagerListener::BundlePriority(), aBundle);
		if (aBundle->isFileBundle()) {
			auto qi = aBundle->getQueueItems().front();
			userQueue.setQIPriority(qi, p);
			qi->setAutoPriority(aBundle->getAutoPriority());

			fire(QueueManagerListener::StatusUpdated(), qi);
		}
	}

	aBundle->setDirty();

	if(p == QueueItemBase::PAUSED_FORCE) {
		DownloadManager::getInstance()->disconnectBundle(aBundle);
	} else if (oldPrio <= QueueItemBase::LOWEST) {
		connectBundleSources(aBundle);
	}

	dcassert(!aBundle->isFileBundle() || aBundle->getPriority() == aBundle->getQueueItems().front()->getPriority());
}

void QueueManager::setBundleAutoPriority(const string& bundleToken) noexcept {
	BundlePtr b = nullptr;
	{
		RLock l(cs);
		b = bundleQueue.findBundle(bundleToken);
		if (b) {
			b->setAutoPriority(!b->getAutoPriority());
			if (b->isFileBundle()) {
				b->getQueueItems().front()->setAutoPriority(b->getAutoPriority());
			}

			b->setDirty();
		}
	}

	if (b) {
		if (SETTING(AUTOPRIO_TYPE) == SettingsManager::PRIO_BALANCED) {
			calculateBundlePriorities(false);
			if (b->isPausedPrio()) {
				//can't count auto priorities with the current bundles, but we don't want this one to stay paused
				setBundlePriority(b, Bundle::LOW, true);
			}
		} else if (SETTING(AUTOPRIO_TYPE) == SettingsManager::PRIO_PROGRESS) {
			setBundlePriority(b, b->calculateProgressPriority(), true);
		}
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

void QueueManager::setQIPriority(QueueItemPtr& q, QueueItemBase::Priority p, bool isAP /*false*/) noexcept {
	HintedUserList getConn;
	bool running = false;
	if (!q || !q->getBundle()) {
		//items without a bundle should always use the highest prio
		return;
	}

	BundlePtr b = q->getBundle();
	if (b->isFileBundle()) {
		dcassert(!isAP);
		setBundlePriority(b, p, false);
		return;
	}

	{
		WLock l(cs);
		if(q->getPriority() != p && !q->isFinished() ) {
			if((q->isPausedPrio() && !b->isPausedPrio()) || (p == QueueItem::HIGHEST && b->getPriority() != QueueItemBase::PAUSED_FORCE)) {
				// Problem, we have to request connections to all these users...
				q->getOnlineUsers(getConn);
			}

			running = q->isRunning();

			if (!isAP)
				q->setAutoPriority(false);

			userQueue.setQIPriority(q, p);
			fire(QueueManagerListener::StatusUpdated(), q);
		}
	}

	b->setDirty();
	if(p == QueueItem::PAUSED_FORCE && running) {
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
		setBundleAutoPriority(q->getBundle()->getToken());
		return;
	}

	q->setAutoPriority(!q->getAutoPriority());
	fire(QueueManagerListener::StatusUpdated(), q);

	q->getBundle()->setDirty();

	if(q->getAutoPriority()) {
		if (SETTING(AUTOPRIO_TYPE) == SettingsManager::PRIO_PROGRESS) {
			setQIPriority(q, q->calculateAutoPriority());
		} else if (q->isPausedPrio()) {
			setQIPriority(q, QueueItem::LOW);
		}
	}
}

void QueueManager::handleSlowDisconnect(const UserPtr& aUser, const string& aTarget, const BundlePtr& aBundle) noexcept {
	switch (SETTING(DL_AUTO_DISCONNECT_MODE)) {
		case SettingsManager::QUEUE_FILE: removeFileSource(aTarget, aUser, QueueItem::Source::FLAG_SLOW_SOURCE); break;
		case SettingsManager::QUEUE_BUNDLE: removeBundleSource(aBundle, aUser, QueueItem::Source::FLAG_SLOW_SOURCE); break;
		case SettingsManager::QUEUE_ALL: removeSource(aUser, QueueItem::Source::FLAG_SLOW_SOURCE, [](const QueueItemPtr& aQI) { return aQI->getSources().size() <= 1; }); break;
	}
}

void QueueManager::removeBundleSource(const string& bundleToken, const UserPtr& aUser, Flags::MaskType reason) noexcept {
	BundlePtr bundle = nullptr;
	{
		RLock l(cs);
		bundle = bundleQueue.findBundle(bundleToken);
	}
	removeBundleSource(bundle, aUser, reason);
}

void QueueManager::removeBundleSource(BundlePtr aBundle, const UserPtr& aUser, Flags::MaskType reason) noexcept {
	if (aBundle) {
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
			removeFileSource(qi, aUser, reason);
		}

		fire(QueueManagerListener::SourceFilesUpdated(), aUser);
	}
}

void QueueManager::sendRemovePBD(const HintedUser& aUser, const string& aRemoteToken) noexcept {
	AdcCommand cmd(AdcCommand::CMD_PBD, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", aUser.hint);
	cmd.addParam("BU", aRemoteToken);
	cmd.addParam("RM1");
	ClientManager::getInstance()->sendUDP(cmd, aUser.user->getCID(), false, true);
}

void QueueManager::saveQueue(bool force) noexcept {
	RLock l(cs);	
	bundleQueue.saveQueue(force);

	// Put this here to avoid very many saves tries when disk is full...
	lastSave = GET_TICK();
}

class QueueLoader : public SimpleXMLReader::CallBack {
public:
	QueueLoader() : curFile(nullptr), inDownloads(false), inBundle(false), inFile(false), qm(QueueManager::getInstance()), version(0) { }
	~QueueLoader() { }
	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name);
	QueueItemBase::Priority validatePrio(const string& aPrio);
	void resetBundle() {
		curFile = nullptr;
		curBundle = nullptr;
		inFile = false;
		inBundle = false;
		inDownloads = false;
		curToken.clear();
		target.clear();
	}
private:
	string target;

	QueueItemPtr curFile;
	BundlePtr curBundle;
	bool inDownloads;
	bool inBundle;
	bool inFile;
	string curToken;
	time_t bundleDate = 0;

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
					LogManager::getInstance()->message(STRING_F(BUNDLE_LOAD_FAILED, path % e.getError().c_str()), LogManager::LOG_ERROR);
					File::deleteFile(path);
				}
			}
			loaded++;
			progressF(static_cast<float>(loaded) / static_cast<float>(fileList.size()));
		});
	} catch (std::exception& e) {
		LogManager::getInstance()->message("Loading the queue failed: " + string(e.what()), LogManager::LOG_INFO);
	}

	try {
		//load the old queue file and delete it
		auto path = Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml";
		Util::migrate(path);

		File f(path, File::READ, File::OPEN);
		QueueLoader loader;
		SimpleXMLReader(&loader).parse(f);
		f.close();
		File::copyFile(Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml", Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml.bak");
		File::deleteFile(Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml");
	} catch(const Exception&) {
		// ...
	}

	TimerManager::getInstance()->addListener(this); 
	SearchManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);
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

void QueueLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	if(!inDownloads && name == "Downloads") {
		inDownloads = true;
	} else if (!inFile && name == sFile) {
		curToken = getAttrib(attribs, sToken, 1);
		bundleDate = Util::toInt64(getAttrib(attribs, sDate, 2));
		inFile = true;
		version = Util::toInt(getAttrib(attribs, sVersion, 0));
		if (version == 0 || version > Util::toInt(FILE_BUNDLE_VERSION))
			throw Exception("Non-supported file bundle version");
	} else if (!inBundle && name == sBundle) {
		version = Util::toInt(getAttrib(attribs, sVersion, 0));
		if (version == 0 || version > Util::toInt(DIR_BUNDLE_VERSION))
			throw Exception("Non-supported directory bundle version");

		const string& bundleTarget = getAttrib(attribs, sTarget, 1);
		const string& token = getAttrib(attribs, sToken, 1);
		if(token.empty())
			throw Exception("Missing bundle token");

		time_t dirDate = static_cast<time_t>(Util::toInt(getAttrib(attribs, sDate, 2)));
		time_t added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 3)));
		const string& prio = getAttrib(attribs, sPriority, 4);
		if(added == 0) {
			added = GET_TIME();
		}

		if (ConnectionManager::getInstance()->tokens.addToken(token))
			curBundle = new Bundle(bundleTarget, added, !prio.empty() ? validatePrio(prio) : Bundle::DEFAULT, dirDate, token, false);
		else
			throw Exception("Duplicate bundle token");

		inBundle = true;		
	} else if(inDownloads || inBundle || inFile) {
		if(!curFile && name == sDownload) {
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			if(size == 0)
				return;
			try {
				const string& tgt = getAttrib(attribs, sTarget, 0);
				// @todo do something better about existing files
				target = QueueManager::checkTarget(tgt);
				if(target.empty())
					return;
			} catch(const Exception&) {
				return;
			}

			if (curBundle && inBundle && !AirUtil::isParentOrExact(curBundle->getTarget(), target)) {
				//the file isn't inside the main bundle dir, can't add this
				return;
			}

			QueueItemBase::Priority p = validatePrio(getAttrib(attribs, sPriority, 3));

			time_t added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 4)));
			const string& tthRoot = getAttrib(attribs, sTTH, 5);
			if(tthRoot.empty())
				return;

			string tempTarget = getAttrib(attribs, sTempTarget, 5);
			uint8_t maxSegments = (uint8_t)Util::toInt(getAttrib(attribs, sMaxSegments, 5));

			if(added == 0)
				added = GET_TIME();

			if (Util::toInt(getAttrib(attribs, sAutoPriority, 6)) == 1) {
				p = QueueItem::DEFAULT;
			}

			WLock l (qm->cs);
			auto ret = qm->fileQueue.add(target, size, 0, p, tempTarget, added, TTHValue(tthRoot));
			if(ret.second) {
				auto& qi = ret.first;
				qi->setMaxSegments(max((uint8_t)1, maxSegments));

				//bundles
				if (curBundle && inBundle) {
					//LogManager::getInstance()->message("itemtoken exists: " + bundleToken);
					qm->bundleQueue.addBundleItem(qi, curBundle);
				} else if (inDownloads) {
					//assign bundles for the items in the old queue file
					curBundle = new Bundle(qi, 0);
				} else if (inFile && !curToken.empty()) {
					if (ConnectionManager::getInstance()->tokens.addToken(curToken)) {
						curBundle = new Bundle(qi, bundleDate, curToken, false);
					} else {
						qm->fileQueue.remove(qi);
						throw Exception("Duplicate token");
					}
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
				}
				/*if (SettingsManager::lanMode) {
					const string& remotePath = getAttrib(attribs, sRemotePath, 2);
					if (remotePath.empty())
						return;

					qm->addSource(curFile, hintedUser, 0, remotePath) && user->isOnline();
				} else {*/
					WLock l (qm->cs);
					qm->addSource(curFile, hintedUser, 0, true, false) && user->isOnline();
				//}
			} catch(const Exception&) {
				return;
			}
		} else if(inBundle && curBundle && name == sFinished) {
			//LogManager::getInstance()->message("FOUND FINISHED TTH");
			const string& tth = getAttrib(attribs, sTTH, 0);
			const string& target = getAttrib(attribs, sTarget, 0);
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			time_t added = static_cast<time_t>(Util::toInt64(getAttrib(attribs, sAdded, 4)));
			if(size == 0 || tth.empty() || target.empty() || added == 0)
				return;
			if(!Util::fileExists(target))
				return;
			qm->addFinishedItem(TTHValue(tth), curBundle, target, size, added);
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
			if (curBundle->getQueueItems().empty()) {
				throw Exception(STRING_F(NO_FILES_WERE_LOADED, curBundle->getTarget()));
			} else {
				qm->addLoadedBundle(curBundle);
			}
		} else if(name == sFile) {
			curToken = Util::emptyString;
			inFile = false;
			if (!curBundle || curBundle->getQueueItems().empty())
				throw Exception(STRING(NO_FILES_FROM_FILE));
		} else if(name == sDownload) {
			if (curBundle && curBundle->isFileBundle()) {
				/* Only for file bundles and when migrating an old queue */
				qm->addLoadedBundle(curBundle);
			}
			curFile = nullptr;
		}
	}
}

string QueueManager::getBundlePath(const string& aBundleToken) const noexcept{
	auto b = bundleQueue.findBundle(aBundleToken);
	return b ? b->getTarget() : "Unknown";
}

void QueueManager::addFinishedItem(const TTHValue& tth, BundlePtr& aBundle, const string& aTarget, int64_t aSize, time_t aFinished) noexcept{
	WLock l(cs);
	if (fileQueue.findFile(aTarget)) {
		return;
	}
	QueueItemPtr qi = new QueueItem(aTarget, aSize, QueueItem::DEFAULT, QueueItem::FLAG_NORMAL, aFinished, tth, Util::emptyString);
	qi->addFinishedSegment(Segment(0, aSize)); //make it complete

	bundleQueue.addFinishedItem(qi, aBundle);
	fileQueue.add(qi);
	//LogManager::getInstance()->message("added finished tth, totalsize: " + Util::toString(aBundle->getFinishedFiles().size()));
}

void QueueManager::noDeleteFileList(const string& path) {
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
		delayEvents.addEvent(selQI->getTarget(), [=] { pickMatch(selQI); }, 2000);
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
	bool wantConnection = false;
	if (aQI->getBundle()->isFileBundle()) {
		/* No reason to match anything with file bundles */
		WLock l(cs);
		try {	 
			wantConnection = addSource(aQI, aResult->getUser(), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
		} catch(...) {
			// Ignore...
		}
	} else {
		string path = aQI->getBundle()->getMatchPath(aResult->getPath(), aQI->getTarget(), aResult->getUser().user->isSet(User::NMDC));
		if (!path.empty()) {
			if (aResult->getUser().user->isSet(User::NMDC)) {
				//A NMDC directory bundle, just add the sources without matching
				QueueItemList ql;
				int newFiles = 0;
				{
					WLock l(cs);
					aQI->getBundle()->getDirQIs(path, ql);
					for (auto& q: ql) {
						try {	 
							if (addSource(q, aResult->getUser(), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) { // no SettingsManager::lanMode in NMDC...
								wantConnection = true;
							}
							newFiles++;
						} catch(...) {
							// Ignore...
						}
					}
				}

				fire(QueueManagerListener::SourceFilesUpdated(), aResult->getUser());

				if (SETTING(REPORT_ADDED_SOURCES) && newFiles > 0) {
					LogManager::getInstance()->message(ClientManager::getInstance()->getFormatedNicks(aResult->getUser()) + ": " + 
						STRING_F(MATCH_SOURCE_ADDED, newFiles % aQI->getBundle()->getName().c_str()), LogManager::LOG_INFO);
				}
			} else {
				//An ADC directory bundle, match recursive partial list
				try {
					addList(aResult->getUser(), QueueItem::FLAG_MATCH_QUEUE | QueueItem::FLAG_RECURSIVE_LIST |(path.empty() ? 0 : QueueItem::FLAG_PARTIAL_LIST), path, aQI->getBundle());
				} catch(...) { }
			}
		} else if (SETTING(ALLOW_MATCH_FULL_LIST)) {
			//failed, use full filelist
			try {
				addList(aResult->getUser(), QueueItem::FLAG_MATCH_QUEUE, Util::emptyString, aQI->getBundle());
			} catch(const Exception&) {
				// ...
			}
		}
	}

	if(wantConnection) {
		ConnectionManager::getInstance()->getDownloadConnection(aResult->getUser());
	}
}

// ClientManagerListener
void QueueManager::on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool /*wasOffline*/) noexcept {
	bool hasDown = false;

	{
		QueueItemList ql;
		{
			RLock l(cs);
			userQueue.getUserQIs(aUser.getUser(), ql);
		}

		for(auto& q: ql) {
			fire(QueueManagerListener::StatusUpdated(), q);
			if(!hasDown && q->startDown() && !q->isHubBlocked(aUser.getUser(), aUser.getHubUrl()))
				hasDown = true;
		}
	}

	if(hasDown) { 
		ConnectionManager::getInstance()->getDownloadConnection(HintedUser(aUser.getUser(), aUser.getHubUrl()));
	}
}

void QueueManager::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept {
	if (!wentOffline)
		return;

	QueueItemList ql;
	{
		RLock l(cs);
		userQueue.getUserQIs(aUser, ql);
	}

	for(auto& q: ql)
		fire(QueueManagerListener::StatusUpdated(), q);
}

void QueueManager::runAltSearch() noexcept {
	auto b = bundleQueue.findSearchBundle(GET_TICK(), true);
	if (b) {
		searchBundle(b, false);
	} else {
		LogManager::getInstance()->message("No bundles to search for!", LogManager::LOG_INFO);
	}
}

void QueueManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if((lastSave + 10000) < aTick) {
		saveQueue(false);
	}

	vector<pair<QueueItemPtr, QueueItemBase::Priority>> qiPriorities;
	vector<pair<BundlePtr, QueueItemBase::Priority>> bundlePriorities;
	auto prioType = SETTING(AUTOPRIO_TYPE);
	bool calculate = lastAutoPrio == 0 || (aTick >= lastAutoPrio + (SETTING(AUTOPRIO_INTERVAL)*1000));

	{
		RLock l(cs);
		for (auto& b: bundleQueue.getBundles() | map_values) {
			if (b->isFinished()) {
				continue;
			}

			if (calculate && prioType == SettingsManager::PRIO_PROGRESS && b->getAutoPriority()) {
				auto p2 = b->calculateProgressPriority();
				if(b->getPriority() != p2) {
					bundlePriorities.emplace_back(b, p2);
				}
			}

			if (b->isFileBundle())
				continue;

			for(auto& q: b->getQueueItems()) {
				if(q->isRunning()) {
					fire(QueueManagerListener::StatusUpdated(), q);
					if (calculate && SETTING(QI_AUTOPRIO) && q->getAutoPriority() && prioType == SettingsManager::PRIO_PROGRESS) {
						auto p1 = q->getPriority();
						if(p1 != QueueItemBase::PAUSED && p1 != QueueItemBase::PAUSED_FORCE) {
							auto p2 = q->calculateAutoPriority();
							if(p1 != p2)
								qiPriorities.emplace_back(q, p2);
						}
					}
				}
			}
		}
	}

	if (calculate && prioType != SettingsManager::PRIO_DISABLED) {
		if (prioType == SettingsManager::PRIO_BALANCED) {
			//LogManager::getInstance()->message("Calculate autoprio (balanced)");
			calculateBundlePriorities(false);
			setLastAutoPrio(aTick);
		} else {
			//LogManager::getInstance()->message("Calculate autoprio (progress)");
			for(auto& bp: bundlePriorities)
				setBundlePriority(bp.first, bp.second, true);

			for(auto& qp: qiPriorities)
				setQIPriority(qp.first, qp.second);
		}
	}
}

template<class T>
static void calculateBalancedPriorities(vector<pair<T, QueueItemBase::Priority>>& priorities, multimap<T, pair<int64_t, double>>& speedSourceMap, bool verbose) noexcept {
	if (speedSourceMap.empty())
		return;

	//scale the priorization maps
	double factorSpeed=0, factorSource=0;
	double max = max_element(speedSourceMap.begin(), speedSourceMap.end())->second.first;
	if (max > 0) {
		factorSpeed = 100 / max;
	}

	max = max_element(speedSourceMap.begin(), speedSourceMap.end())->second.second;
	if (max > 0) {
		factorSource = 100 / max;
	}

	multimap<int, T> finalMap;
	int uniqueValues = 0;
	for (auto& i: speedSourceMap) {
		auto points = (i.second.first * factorSpeed) + (i.second.second * factorSource);
		if (finalMap.find(points) == finalMap.end()) {
			uniqueValues++;
		}
		finalMap.insert(make_pair(points, i.first));
	}

	int prioGroup = 1;
	if (uniqueValues <= 1) {
		if (verbose) {
			LogManager::getInstance()->message("Not enough items with unique points to perform the priotization!", LogManager::LOG_INFO);
		}
		return;
	} else if (uniqueValues > 2) {
		prioGroup = uniqueValues / 3;
	}

	if (verbose) {
		LogManager::getInstance()->message("Unique values: " + Util::toString(uniqueValues) + " prioGroup size: " + Util::toString(prioGroup), LogManager::LOG_INFO);
	}


	//start with the high prio, continue to normal and low
	int8_t prio = QueueItemBase::HIGH;

	//counters for analyzing identical points
	int lastPoints = 999;
	int prioSet=0;

	for (auto& i: finalMap) {
		if (lastPoints==i.first) {
			if (verbose) {
				LogManager::getInstance()->message(i.second->getTarget() + " points: " + Util::toString(i.first) + " setting prio " + AirUtil::getPrioText(prio), LogManager::LOG_INFO);
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
				LogManager::getInstance()->message(i.second->getTarget() + " points: " + Util::toString(i.first) + " setting prio " + AirUtil::getPrioText(prio), LogManager::LOG_INFO);
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
					bundleSpeedSourceMap.insert(make_pair(b, b->getPrioInfo()));
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

bool QueueManager::dropSource(Download* d) noexcept {
	BundlePtr b = d->getBundle();
	size_t onlineUsers = 0;

	if(b->getRunning() >= SETTING(DISCONNECT_MIN_SOURCES)) {
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

				QueueItem::PartialSource* ps = new QueueItem::PartialSource(partialSource.getMyNick(),
					partialSource.getHubIpPort(), partialSource.getIp(), partialSource.getUdpPort());
				si->setPartialSource(ps);

				userQueue.addQI(qi, aUser);
				dcassert(si != qi->getSources().end());
				fire(QueueManagerListener::SourcesUpdated(), qi);
			}
		}

		// Update source's parts info
		if(si->getPartialSource()) {
			si->getPartialSource()->setPartialInfo(partialSource.getPartialInfo());
		}
	}
	
	// Connect to this user
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);

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
			_bundle = b->getToken();

			//should we notify the other user about finished item?
			_reply = !b->getQueueItems().empty() && !b->isFinishedNotified(aUser);

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

StringList QueueManager::getDirPaths(const string& aDirName) const noexcept {
	Bundle::StringBundleList found;
	StringList ret;

	RLock l(cs);
	bundleQueue.findRemoteDirs(aDirName, found);
	for (const auto& p : found) {
		ret.push_back(p.first);
	}

	return ret;
}

void QueueManager::getUnfinishedPaths(StringList& retBundles) noexcept {
	RLock l(cs);
	for(auto& b: bundleQueue.getBundles() | map_values) {
		if (!b->isFinished())
			retBundles.push_back(Text::toLower(b->getTarget()));
	}
}

void QueueManager::checkRefreshPaths(StringList& retBundles, StringList& sharePaths) noexcept {
	BundleList hash;
	{
		RLock l(cs);
		for (auto& b: bundleQueue.getBundles() | map_values) {
			if (b->isFileBundle())
				continue;

			//check the path just to avoid hashing/scanning bundles from dirs that aren't being refreshed
			bool found = false;
			for (auto i = sharePaths.begin(); i != sharePaths.end(); ) {
				if (AirUtil::isParentOrExact(*i, b->getTarget())) {
					if (Util::stricmp(*i, b->getTarget()) == 0) {
						//erase exact matches
						i = sharePaths.erase(i);
					}
					found = true;
					break;
				}

				i++;
			}

			//not inside the refreshed dirs
			if (!found)
				continue;


			if(b->isFinished() && (b->isFailed() || b->allowHash())) {
				hash.push_back(b);
			}

			retBundles.push_back(Text::toLower(b->getTarget()));
		}
	}

	for (auto& b: hash) {
		if(b->isFailed() && !scanBundle(b)) {
			continue;
		}

		hashBundle(b); 
	}

	sort(retBundles.begin(), retBundles.end());
}

void QueueManager::setBundleStatus(BundlePtr& aBundle, Bundle::Status newStatus) noexcept {
	if (aBundle->getStatus() != newStatus) {
		aBundle->setStatus(newStatus);
		fire(QueueManagerListener::BundleStatusChanged(), aBundle);
	}
}

void QueueManager::shareBundle(const string& aName) noexcept {
	Bundle::StringBundleList lst;
	{
		RLock l (cs);
		bundleQueue.findRemoteDirs(aName, lst);
	}

	if (!lst.empty()) {
		auto b = lst.front().second;
		setBundleStatus(b, Bundle::STATUS_FINISHED);
		hashBundle(b); 
		LogManager::getInstance()->message("The bundle " + aName + " has been added for hashing", LogManager::LOG_INFO);
	} else {
		LogManager::getInstance()->message("The bundle " + aName + " wasn't found", LogManager::LOG_WARNING);
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

void QueueManager::addLoadedBundle(BundlePtr& aBundle) noexcept {
	WLock l(cs);
	if (aBundle->getQueueItems().empty())
		return;

	if (bundleQueue.getMergeBundle(aBundle->getTarget()))
		return;

	bundleQueue.addBundle(aBundle);
}

bool QueueManager::addBundle(BundlePtr& aBundle, const string& aTarget, int itemsAdded, bool moving /*false*/) noexcept {
	if (aBundle->getQueueItems().empty() && itemsAdded > 0) {
		// it finished already? (only 0 byte files were added)
		tasks.addTask(new DispatcherQueue::Callback([=] {
			BundlePtr b = aBundle;
			checkBundleFinished(b, false);
		}));

		return false;
	}

	if (itemsAdded == 0)
		return false;

	bool statusChanged = false;
	if (aBundle->getStatus() == Bundle::STATUS_NEW) {
		bundleQueue.addBundle(aBundle);
		fire(QueueManagerListener::BundleAdded(), aBundle);
		statusChanged = true;

		if (aBundle->isFileBundle())
			LogManager::getInstance()->message(STRING_F(FILE_X_QUEUED, aBundle->getName() % Util::formatBytes(aBundle->getSize())), LogManager::LOG_INFO);
		else
			LogManager::getInstance()->message(STRING_F(BUNDLE_CREATED, aBundle->getName() % aBundle->getQueueItems().size()) + " (" + CSTRING_F(TOTAL_SIZE, Util::formatBytes(aBundle->getSize())) + ")", LogManager::LOG_INFO);
	} else {
		//finished bundle but failed hashing/scanning?
		bool finished = static_cast<int>(aBundle->getQueueItems().size()) == itemsAdded;

		if (finished) {
			statusChanged = true;
			readdBundle(aBundle);
		} else {
			aBundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
			addBundleUpdate(aBundle);
			aBundle->setDirty();

			if (moving)
				fire(QueueManagerListener::BundleAdded(), aBundle);
		}


		/* Report */
		if (!aTarget.empty() && aTarget.back() != PATH_SEPARATOR) {
			LogManager::getInstance()->message(STRING_F(BUNDLE_ITEM_ADDED, Util::getFileName(aTarget) % aBundle->getName()), LogManager::LOG_INFO);
		} else if (aBundle->getTarget() == aTarget) {
			LogManager::getInstance()->message(STRING_F(X_BUNDLE_ITEMS_ADDED, itemsAdded % aBundle->getName().c_str()), LogManager::LOG_INFO);
		} else {
			LogManager::getInstance()->message(STRING_F(BUNDLE_MERGED, Util::getLastDir(aTarget) % aBundle->getName() % itemsAdded), LogManager::LOG_INFO);
		}

		aBundle->updateSearchMode();
	}

	if (statusChanged) {
		aBundle->setStatus(Bundle::STATUS_QUEUED);
		tasks.addTask(new DispatcherQueue::Callback([=] {
			auto b = aBundle;
			fire(QueueManagerListener::BundleStatusChanged(), aBundle);
			if (SETTING(AUTO_SEARCH) && SETTING(AUTO_ADD_SOURCE) && !b->isPausedPrio()) {
				b->setFlag(Bundle::FLAG_SCHEDULE_SEARCH);
				addBundleUpdate(b);
			}
		}));
	}

	return true;
}

void QueueManager::connectBundleSources(BundlePtr& aBundle) noexcept {
	if (aBundle->isPausedPrio())
		return;

	HintedUserList x;
	{
		RLock l(cs);
		aBundle->getSources(x);
	}

	for(auto& u: x) { 
		if(u.user && u.user->isOnline())
			ConnectionManager::getInstance()->getDownloadConnection(u, false); 
	}
}

void QueueManager::readdBundle(BundlePtr& aBundle) noexcept {
	//aBundle->setStatus(Bundle::STATUS_QUEUED);

	//check that the finished files still exist
	auto files = aBundle->getFinishedFiles();
	for(auto& qi: files) {
		if (!Util::fileExists(qi->getTarget())) {
			bundleQueue.removeFinishedItem(qi);
			fileQueue.remove(qi);
		}
	}

	bundleQueue.addSearchPrio(aBundle);
	LogManager::getInstance()->message(STRING_F(BUNDLE_READDED, aBundle->getName().c_str()), LogManager::LOG_INFO);
}

int QueueManager::changeBundleTarget(BundlePtr& aBundle, const string& newTarget) noexcept{
	bundleQueue.moveBundle(aBundle, newTarget); //set the new target

	BundleList mBundles;
	if (!aBundle->isFileBundle()) {
		/* In this case we also need check if there are directory bundles inside the subdirectories */
		bundleQueue.getSubBundles(newTarget, mBundles);

		for(auto& b: mBundles) {
			fire(QueueManagerListener::BundleRemoved(), b);
			auto files = b->getFinishedFiles();
			for (auto& qi: files) {
				bundleQueue.removeFinishedItem(qi);
				bundleQueue.addFinishedItem(qi, aBundle);
			}

			auto items = b->getQueueItems();
			for (auto& qi : items)
				moveBundleItem(qi, aBundle);
		}

		aBundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
	}

	aBundle->setFlag(Bundle::FLAG_UPDATE_NAME);
	addBundleUpdate(aBundle);
	aBundle->setDirty();

	return (int)mBundles.size();
}

void QueueManager::getDirItems(const BundlePtr& aBundle, const string& aDir, QueueItemList& aItems) const noexcept { 
	RLock l(cs);
	aBundle->getDirQIs(aDir, aItems);
}

uint8_t QueueManager::isDirQueued(const string& aDir) const noexcept{
	Bundle::StringBundleList lst;

	RLock l(cs);
	bundleQueue.findRemoteDirs(aDir, lst);
	if (!lst.empty()) {
		auto s = lst.front().second->getPathInfo(lst.front().first);
		if (s.first == 0) //no queued items
			return 2;
		else
			return 1;
	}
	return 0;
}



int QueueManager::getBundleItemCount(const BundlePtr& aBundle) const noexcept {
	RLock l(cs); 
	return aBundle->getQueueItems().size(); 
}

int QueueManager::getFinishedItemCount(const BundlePtr& aBundle) const noexcept { 
	RLock l(cs); 
	return (int)aBundle->getFinishedFiles().size(); 
}

void QueueManager::removeDir(const string aSource, const BundleList& sourceBundles, bool removeFinished) noexcept {
	if (sourceBundles.empty()) {
		return;
	}

	if (sourceBundles.size() == 1 && AirUtil::isSub(aSource, sourceBundles.front()->getTarget())) {
		//we aren't removing the whole bundle
		BundlePtr bundle = sourceBundles.front();
		QueueItemList ql;
		{
			WLock l(cs);
			bundle->getDirQIs(aSource, ql);

			auto finishedFiles = bundle->getFinishedFiles();
			for (auto& qi: finishedFiles) {
				if (AirUtil::isSub(qi->getTarget(), aSource)) {
					UploadManager::getInstance()->abortUpload(qi->getTarget());
					if (removeFinished) {
						File::deleteFile(qi->getTarget());
					}
					fileQueue.remove(qi);
					bundleQueue.removeFinishedItem(qi);
				}
			}
		}

		AirUtil::removeDirectoryIfEmpty(aSource, 10, true);
		for(auto& qi: ql) 
			removeQI(qi);
	} else {
		for(auto b: sourceBundles) 
			removeBundle(b, false, removeFinished);
	}
}

void QueueManager::mergeFinishedItems(const string& aSource, const string& aTarget, BundlePtr& sourceBundle, BundlePtr& targetBundle, bool moveFiles) noexcept {
	auto finishedFiles = sourceBundle->getFinishedFiles();
	for (auto& qi : finishedFiles) {
		if (AirUtil::isSub(qi->getTarget(), aSource)) {
			if (moveFiles) {
				string targetPath = AirUtil::convertMovePath(qi->getTarget(), aSource, aTarget);
				if (!fileQueue.findFile(targetPath)) {
					if(!Util::fileExists(targetPath)) {
						qi->unsetFlag(QueueItem::FLAG_MOVED);
						moveFile(qi->getTarget(), targetPath, qi);
						if (targetBundle == sourceBundle) {
							fileQueue.move(qi, targetPath);
						} else {
							bundleQueue.removeFinishedItem(qi);
							fileQueue.move(qi, targetPath);
							bundleQueue.addFinishedItem(qi, targetBundle);
						}
						continue;
					} else {
						/* TODO: add for recheck */
					}
				}
			}
			fileQueue.remove(qi);
			bundleQueue.removeFinishedItem(qi);
		}
	}

	//we may not be able to remove the directory instantly if we have finished files to move (moveFile will handle this)
	//mover.removeDir(sourceBundle->getTarget());
}

void QueueManager::moveBundle(BundlePtr aBundle, const string& aTarget, bool moveFinished) {
	if (aBundle->isFileBundle()) {
		StringPairList fileBundles;
		fileBundles.emplace_back(aBundle->getTarget(), AirUtil::convertMovePath(aBundle->getTarget(), Util::getFilePath(aBundle->getTarget()), aTarget));
		QueueManager::getInstance()->moveFiles(fileBundles);
		LogManager::getInstance()->message(STRING_F(FILEBUNDLE_MOVED, aBundle->getName() % aBundle->getTarget()), LogManager::LOG_INFO);
	} else {
		moveBundleDir(aBundle->getTarget(), aTarget + aBundle->getName() + PATH_SEPARATOR, aBundle, moveFinished);
	}
}

void QueueManager::renameBundle(BundlePtr aBundle, const string& newName) {
	if (aBundle->isFileBundle()) {
		//StringPairList fileBundles;
		//fileBundles.emplace_back(aBundle->getTarget(), AirUtil::convertMovePath(aBundle->getTarget(), Util::getFilePath(aBundle->getTarget()), aTarget));
		//QueueManager::getInstance()->moveFiles(fileBundles);
	} else {
		moveBundleDir(aBundle->getTarget(), Util::getParentDir(aBundle->getTarget()) + newName + PATH_SEPARATOR, aBundle, true);
	}
}

void QueueManager::moveBundleDir(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished) noexcept {
	if (aSource.empty() || aTarget.empty() || !sourceBundle) {
		return;
	}

	QueueItemList ql;
	QueueItemList remove;

	BundlePtr newBundle = nullptr;
	{
		WLock l(cs);
		//get the target bundle

		newBundle = getBundle(aTarget, sourceBundle->getPriority(), sourceBundle->getBundleDate(), false);
		if (newBundle->getStatus() == Bundle::STATUS_NEW && aSource == sourceBundle->getTarget()) {
			//no need to create a new bundle
			newBundle = sourceBundle;
		}

		//handle finished items
		mergeFinishedItems(aSource, aTarget, sourceBundle, newBundle, moveFinished);

		fire(QueueManagerListener::BundleMoved(), sourceBundle);

		//pick the items that we need to move
		sourceBundle->getDirQIs(aSource, ql);

		//convert the QIs
		for (auto i = ql.begin(); i != ql.end();) {
			if (!changeTarget(*i, AirUtil::convertMovePath((*i)->getTarget(), aSource, aTarget))) {
				remove.push_back(*i);
				i = ql.erase(i);
			} else {
				i++;
			}
		}

		if (newBundle != sourceBundle) {
			//we are moving the items to another bundle
			for (auto& qi: ql)
				moveBundleItem(qi, newBundle);

			//add the old bundle (if needed)
			if (!sourceBundle->getQueueItems().empty()) {
				fire(QueueManagerListener::BundleAdded(), sourceBundle);
			}

			addBundle(newBundle, aTarget, ql.size(), true);
		} else {
			int merged = changeBundleTarget(sourceBundle, aTarget);

			fire(QueueManagerListener::BundleAdded(), sourceBundle);

			string tmp = STRING_F(BUNDLE_MOVED, sourceBundle->getName().c_str() % sourceBundle->getTarget().c_str());
			if (merged > 0)
				tmp += " (" + STRING_F(EXISTING_BUNDLES_MERGED, merged) + ")";

			LogManager::getInstance()->message(tmp, LogManager::LOG_INFO);
		}
	}

	for (auto qi: remove)
		removeQI(qi, true);
}

bool QueueManager::changeTarget(QueueItemPtr& qs, const string& aTarget) noexcept {
	//validate the new target
	string target;
	try {
		target = checkTarget(aTarget);
	} catch(...) {
		return false;
	}

	if(qs->getTarget() == aTarget) {
		return false;
	}

	dcassert(qs->getBundle());


	{
		// Let's see if the target exists...then things get complicated...
		QueueItemPtr qt = fileQueue.findFile(target);
		if (qt) {
			//we have something already
			try {
				if (replaceItem(qt, qs->getSize(), qs->getTTH())) {
					qt = nullptr;
				} else {
					//add the sources
					for(auto& s: qs->getSources()) {
						try {
							addSource(qt, s.getUser(), QueueItem::Source::FLAG_MASK);
						} catch(const Exception&) {
							//..
						}
					}
				}
			} catch(...) { 
				//finished or continue to removal
			}
		}

		if(!qt) {
			// Good, update the target and move in the queue...
			if(qs->isRunning()) {
				DownloadManager::getInstance()->setTarget(qs->getTarget(), aTarget);
			}

			string oldTarget = qs->getTarget();
			fileQueue.move(qs, target);
			return true;
		}
	}

	//failed
	return false;
}

void QueueManager::moveFiles(const StringPairList& sourceTargetList) noexcept {
	if (sourceTargetList.empty())
		return;

	QueueItemList removed;

	{
		//convert the items
		WLock l(cs);
		for(auto& sp: sourceTargetList) {
			auto qi = fileQueue.findFile(sp.first);
			if(qi) {
				//remove from GUI
				fire(QueueManagerListener::Removed(), qi, false);
				if (qi->getBundle()->isFileBundle())
					fire(QueueManagerListener::BundleRemoved(), qi->getBundle());

				//change the target
				if (!changeTarget(qi, sp.second)) {
					removed.push_back(qi);
					continue;
				}

				auto oldBundle = qi->getBundle();
				auto b = getBundle(qi->getTarget(), oldBundle->getPriority(), oldBundle->getBundleDate(), true);
				if ((oldBundle->isFileBundle() && b->getStatus() == Bundle::STATUS_NEW) || b == oldBundle) {
					//keep in the same bundle
					if (qi->getBundle()->isFileBundle()) {
						changeBundleTarget(oldBundle, qi->getTarget());
						fire(QueueManagerListener::BundleAdded(), oldBundle);
					} else {
						fire(QueueManagerListener::Added(), qi);
					}
				} else {
					//new bundle
					moveBundleItem(qi, b);
					if (b->getStatus() != Bundle::STATUS_NEW)
						fire(QueueManagerListener::Added(), qi);

					addBundle(b, qi->getTarget(), 1);
				}
			}
		}
	}

	for (auto& qi: removed)
		removeQI(qi, false);
}

void QueueManager::moveBundleItem(QueueItemPtr qi, BundlePtr& targetBundle) noexcept {
	BundlePtr sourceBundle = qi->getBundle();
	bundleQueue.removeBundleItem(qi, false);
	userQueue.removeQI(qi, false); //we definately don't want to remove downloads because the QI will stay the same

	if (qi->isRunning()) {
		//now we need to move the download(s) to correct bundle... the target has been changed earlier, if needed
		DownloadManager::getInstance()->changeBundle(sourceBundle, targetBundle, qi->getTarget());
	}

	qi->setBundle(nullptr);
	if (targetBundle->isFileBundle())
		qi->setPriority(targetBundle->getPriority());

	/* ADDING */
	bundleQueue.addBundleItem(qi, targetBundle);
	userQueue.addQI(qi);

	//check if the source is empty
	if (sourceBundle->getQueueItems().empty()) {
		tasks.addTask(new DispatcherQueue::Callback([=] { 
			auto b = sourceBundle;
			removeBundle(b, false, false, false); 
		}));
	} else {
		sourceBundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
		addBundleUpdate(sourceBundle);
		sourceBundle->setDirty();
	}
}

void QueueManager::addBundleUpdate(const BundlePtr& aBundle) noexcept {
	delayEvents.addEvent(aBundle->getToken(), [this, aBundle] { handleBundleUpdate(aBundle->getToken()); }, aBundle->isSet(Bundle::FLAG_SCHEDULE_SEARCH) ? 10000 : 1000);
}

void QueueManager::handleBundleUpdate(const string& bundleToken) noexcept {
	//LogManager::getInstance()->message("QueueManager::sendBundleUpdate");
	BundlePtr b = nullptr;
	{
		RLock l(cs);
		b = bundleQueue.findBundle(bundleToken);
	}

	if (b) {
		if (b->isSet(Bundle::FLAG_UPDATE_SIZE) || b->isSet(Bundle::FLAG_UPDATE_NAME)) {
			if (b->isSet(Bundle::FLAG_UPDATE_SIZE)) {
				fire(QueueManagerListener::BundleSize(), b);
			} 
			if (b->isSet(Bundle::FLAG_UPDATE_NAME)) {
				fire(QueueManagerListener::BundleTarget(), b);
			}
			DownloadManager::getInstance()->sendSizeNameUpdate(b);
		}
		
		if (b->isSet(Bundle::FLAG_SCHEDULE_SEARCH)) {
			searchBundle(b, false);
		}
	}
}

void QueueManager::removeBundleItem(QueueItemPtr& qi, bool finished, bool moved /*false*/) noexcept {
	BundlePtr bundle = qi->getBundle();
	if (!bundle) {
		return;
	}
	bool emptyBundle = false;

	{
		WLock l(cs);
		bundleQueue.removeBundleItem(qi, finished);
		if (finished) {
			fileQueue.decreaseSize(qi->getSize());
		}

		emptyBundle = bundle->getQueueItems().empty();
	}

	if (emptyBundle) {
		removeBundle(bundle, finished, false, moved);
	} else {
		for (auto& aSource: qi->getSources())
			fire(QueueManagerListener::SourceFilesUpdated(), aSource.getUser());
		bundle->setDirty();
	}
}

void QueueManager::removeBundle(BundlePtr& aBundle, bool finished, bool removeFinished, bool moved /*false*/) noexcept {
	if (aBundle->getStatus() == Bundle::STATUS_NEW) {
		return;
	}

	vector<UserPtr> sources;
	for (auto& aSource: aBundle->getSources())
		sources.push_back(aSource.user.user);


	if (finished) {
		aBundle->finishBundle();
		setBundleStatus(aBundle, Bundle::STATUS_DOWNLOADED);
	} else if (!moved) {
		//LogManager::getInstance()->message("The Bundle " + aBundle->getName() + " has been removed");
		DownloadManager::getInstance()->disconnectBundle(aBundle);
		{
			WLock l(cs);
			for (auto i = aBundle->getFinishedFiles().begin(); i != aBundle->getFinishedFiles().end();) {
				QueueItemPtr q = *i;
				UploadManager::getInstance()->abortUpload(q->getTarget());
				fileQueue.remove(q);
				if (removeFinished) {
					File::deleteFile(q->getTarget());
				}

				i = aBundle->getFinishedFiles().erase(i);
			}

			for (auto i = aBundle->getQueueItems().begin(); i != aBundle->getQueueItems().end();) {
				QueueItemPtr qi = *i;
				UploadManager::getInstance()->abortUpload(qi->getTarget());

				if(!qi->isRunning() && !qi->getTempTarget().empty() && qi->getTempTarget() != qi->getTarget()) {
					File::deleteFile(qi->getTempTarget());
				}

				if(!qi->isFinished()) {
					userQueue.removeQI(qi, true, false);
					fire(QueueManagerListener::Removed(), qi, false);
				}

				fileQueue.remove(qi);
				i = aBundle->getQueueItems().erase(i);
			}

			LogManager::getInstance()->message(STRING_F(BUNDLE_X_REMOVED, aBundle->getName()), LogManager::LOG_INFO);
		}

		fire(QueueManagerListener::BundleRemoved(), aBundle);
		if (!aBundle->isFileBundle()) {
			AirUtil::removeDirectoryIfEmpty(aBundle->getTarget(), 10, false);
		}
	}


	for (const auto& aUser: sources)
		fire(QueueManagerListener::SourceFilesUpdated(), aUser);


	QueueItemList removed;
	{
		WLock l(cs);
		//erase all lists related to this bundle
		auto dl = matchLists.left.equal_range(aBundle->getToken());
		for (auto p = dl.first; p != dl.second; p++) {
			auto q = fileQueue.findFile(p->get_right());
			if (q)
				removed.push_back(q);
		}

		if (!finished) {
			bundleQueue.removeBundle(aBundle);
		} else {
			aBundle->deleteBundleFile();
			bundleQueue.removeSearchPrio(aBundle);
		}
	}

	for (auto& qi: removed)
		removeQI(qi);
}

MemoryInputStream* QueueManager::generateTTHList(const string& bundleToken, bool isInSharingHub) throw(QueueException) {
	if(!isInSharingHub)
		throw QueueException(UserConnection::FILE_NOT_AVAILABLE);

	string tths;
	StringOutputStream tthList(tths);
	{
		RLock l(cs);
		BundlePtr b = bundleQueue.findBundle(bundleToken);
		if (b) {
			//write finished items
			string tmp2;
			for(auto& q: b->getFinishedFiles()) {
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

void QueueManager::addBundleTTHList(const HintedUser& aUser, const string& remoteBundle, const TTHValue& tth) throw(QueueException) {
	//LogManager::getInstance()->message("ADD TTHLIST");
	auto b = findBundle(tth);
	if (b) {
		addList(aUser, QueueItem::FLAG_TTHLIST_BUNDLE | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_MATCH_QUEUE, remoteBundle, b);
	}
}

bool QueueManager::checkPBDReply(HintedUser& aUser, const TTHValue& aTTH, string& _bundleToken, bool& _notify, bool& _add, const string& remoteBundle) noexcept {
	BundlePtr bundle = findBundle(aTTH);
	if (bundle) {
		WLock l(cs);
		//LogManager::getInstance()->message("checkPBDReply: BUNDLE FOUND");
		_bundleToken = bundle->getToken();
		_add = !bundle->getFinishedFiles().empty();

		if (!bundle->getQueueItems().empty()) {
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
		if (!bundle->getQueueItems().empty()) {
			bundle->addFinishedNotify(aUser, remoteBundle);
		}
	}
	//LogManager::getInstance()->message("addFinishedNotify: BUNDLE NOT FOUND");
}

void QueueManager::removeBundleNotify(const UserPtr& aUser, const string& bundleToken) noexcept {
	WLock l(cs);
	BundlePtr bundle = bundleQueue.findBundle(bundleToken);
	if (bundle) {
		bundle->removeFinishedNotify(aUser);
	}
}

void QueueManager::updatePBD(const HintedUser& aUser, const TTHValue& aTTH) noexcept {
	//LogManager::getInstance()->message("UPDATEPBD");
	bool wantConnection = false;
	QueueItemList qiList;
	{
		WLock l(cs);
		fileQueue.findFiles(aTTH, qiList);
		for(auto& q: qiList) {
			try {
				//LogManager::getInstance()->message("ADDSOURCE");
				if (addSource(q, aUser, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
					wantConnection = true;
				}
			} catch(...) {
				// Ignore...
			}
		}
	}

	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

void QueueManager::searchBundle(BundlePtr& aBundle, bool manual) noexcept {
	map<string, QueueItemPtr> searches;
	int64_t nextSearch = 0;
	{
		RLock l(cs);
		bool isScheduled = aBundle->isSet(Bundle::FLAG_SCHEDULE_SEARCH);

		aBundle->unsetFlag(Bundle::FLAG_SCHEDULE_SEARCH);
		if (!manual)
			nextSearch = (bundleQueue.recalculateSearchTimes(aBundle->isRecent(), false) - GET_TICK()) / (60*1000);

		if (isScheduled && !aBundle->allowAutoSearch())
			return;

		aBundle->getSearchItems(searches, manual);
	}

	if (searches.empty()) {
		return;
	}

	if (searches.size() <= 5) {
		aBundle->setSimpleMatching(true);
		for(auto& sqp: searches)
			sqp.second->searchAlternates();
	} else {
		//use an alternative matching, choose random items to search for
		aBundle->setSimpleMatching(false);
		int k = 0;
		while (k < 5) {
			auto pos = searches.begin();
			auto rand = Util::rand(searches.size());
			advance(pos, rand);
			pos->second->searchAlternates();
			searches.erase(pos);
			k++;
		}
	}

	aBundle->setLastSearch(GET_TICK());
	int searchCount = (int)searches.size() <= 4 ? (int)searches.size() : 4;
	if (manual) {
		LogManager::getInstance()->message(STRING_F(BUNDLE_ALT_SEARCH, aBundle->getName().c_str() % searchCount), LogManager::LOG_INFO);
	} else if(SETTING(REPORT_ALTERNATES)) {
		//if (aBundle->getSimpleMatching()) {
			if (aBundle->isRecent()) {
				LogManager::getInstance()->message(STRING_F(BUNDLE_ALT_SEARCH_RECENT, aBundle->getName() % searchCount) + 
					" " + STRING_F(NEXT_RECENT_SEARCH_IN, nextSearch), LogManager::LOG_INFO);
			} else {
				LogManager::getInstance()->message(STRING_F(BUNDLE_ALT_SEARCH, aBundle->getName() % searchCount) +
					" " + STRING_F(NEXT_SEARCH_IN, nextSearch), LogManager::LOG_INFO);
			}
		/*} else {
			if (!aBundle->isRecent()) {
				LogManager::getInstance()->message(STRING(ALTERNATES_SEND) + " " + aBundle->getName() + ", not using partial lists, next search in " + Util::toString(nextSearch) + " minutes", LogManager::LOG_INFO);
			} else {
				LogManager::getInstance()->message(STRING(ALTERNATES_SEND) + " " + aBundle->getName() + ", not using partial lists, next recent search in " + Util::toString(nextSearch) + " minutes", LogManager::LOG_INFO);
			}
		}*/
	}
}

void QueueManager::onUseSeqOrder(BundlePtr& b) noexcept {
	if (b) {
		WLock l (cs);
		b->setSeqOrder(true);
		auto ql = b->getQueueItems();
		for (auto& q: ql) {
			if (!q->isPausedPrio()) {
				userQueue.removeQI(q, false, false);
				userQueue.addQI(q, true);
			}
		}
	}
}

} // namespace dcpp
