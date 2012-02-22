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

#include "stdinc.h"
#include "QueueManager.h"

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>

#include "AirUtil.h"
#include "Bundle.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "DirectoryListing.h"
#include "Download.h"
#include "DownloadManager.h"
#include "FileReader.h"
#include "format.h"
#include "HashManager.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "SearchManager.h"
#include "ShareScannerManager.h"
#include "ShareManager.h"
#include "SimpleXMLReader.h"
#include "StringTokenizer.h"
#include "Transfer.h"
#include "UploadManager.h"
#include "UserConnection.h"
#include "version.h"
#include "Wildcards.h"
#include "SearchResult.h"

#include <limits>

#if !defined(_WIN32) && !defined(PATH_MAX) // Extra PATH_MAX check for Mac OS X
#include <sys/syslimits.h>
#endif

#ifdef ff
#undef ff
#endif

namespace dcpp {

using boost::adaptors::map_values;
using boost::range::for_each;

class DirectoryItem {
public:
	DirectoryItem() : priority(QueueItem::DEFAULT) { }
	DirectoryItem(const UserPtr& aUser, const string& aName, const string& aTarget, 
		QueueItem::Priority p) : name(aName), target(aTarget), priority(p), user(aUser) { }
	~DirectoryItem() { }
	
	UserPtr& getUser() { return user; }
	void setUser(const UserPtr& aUser) { user = aUser; }
	
	GETSET(string, name, Name);
	GETSET(string, target, Target);
	GETSET(QueueItem::Priority, priority, Priority);
private:
	UserPtr user;
};

void QueueManager::FileMover::moveFile(const string& source, const string& target, BundlePtr aBundle) {
	Lock l(cs);
	files.push_back(make_pair(aBundle, make_pair(source, target)));
	if(!active) {
		active = true;
		start();
	}
}

int QueueManager::FileMover::run() {
	for(;;) {
		FileBundlePair next;
		{
			Lock l(cs);
			if(files.empty()) {
				active = false;
				return 0;
			}
			next = files.back();
			files.pop_back();
		}
		moveFile_(next.second.first, next.second.second, next.first);
	}
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

			q = qm->fileQueue.find(file);
			if(!q || q->isSet(QueueItem::FLAG_USER_LIST))
				continue;

			qm->fire(QueueManagerListener::RecheckStarted(), q->getTarget());
			dcdebug("Rechecking %s\n", file.c_str());

			tempSize = File::getSize(q->getTempTarget());

			if(tempSize == -1) {
				qm->fire(QueueManagerListener::RecheckNoFile(), q->getTarget());
				continue;
			}

			if(tempSize < 64*1024) {
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
			q = qm->fileQueue.find(file);
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
			q = qm->fileQueue.find(file);
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
					q->addSegment(Segment(pos, tt.getBlockSize()), false);
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
	udp(Socket::TYPE_UDP)
{ 
	TimerManager::getInstance()->addListener(this); 
	SearchManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);
	DownloadManager::getInstance()->addListener(this);

	File::ensureDirectory(Util::getListPath());
	File::ensureDirectory(Util::getBundlePath());
}

QueueManager::~QueueManager() noexcept { 
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this); 
	ClientManager::getInstance()->removeListener(this);
	HashManager::getInstance()->removeListener(this);
	DownloadManager::getInstance()->removeListener(this);

	saveQueue(true);

	if(!BOOLSETTING(KEEP_LISTS)) {
		string path = Util::getListPath();

		std::sort(protectedFileLists.begin(), protectedFileLists.end());

		StringList filelists = File::findFiles(path, "*.xml.bz2");
		std::sort(filelists.begin(), filelists.end());
		std::for_each(filelists.begin(), std::set_difference(filelists.begin(), filelists.end(),
			protectedFileLists.begin(), protectedFileLists.end(), filelists.begin()), &File::deleteFile);

		filelists = File::findFiles(path, "*.DcLst");
		std::sort(filelists.begin(), filelists.end());
		std::for_each(filelists.begin(), std::set_difference(filelists.begin(), filelists.end(),
			protectedFileLists.begin(), protectedFileLists.end(), filelists.begin()), &File::deleteFile);
	}
}

bool QueueManager::getTTH(const string& name, TTHValue& tth) noexcept {
	RLock l(cs);
	QueueItemPtr qi = fileQueue.find(name);
	if(qi) {
		tth = qi->getTTH();
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
		WLock l(cs);

		//find max 10 pfs sources to exchange parts
		//the source basis interval is 5 minutes
		FileQueue::PFSSourceList sl;
		fileQueue.findPFSSources(sl);

		for(auto i = sl.begin(); i != sl.end(); i++){
			QueueItem::PartialSource::Ptr source = (*i->first).getPartialSource();
			const QueueItemPtr qi = i->second;

			PartsInfoReqParam* param = new PartsInfoReqParam;
			
			int64_t blockSize = HashManager::getInstance()->getBlockSize(qi->getTTH());
			if(blockSize == 0)
				blockSize = qi->getSize();			
			qi->getPartialInfo(param->parts, blockSize);
			
			param->tth = qi->getTTH().toBase32();
			param->ip  = source->getIp();
			param->udpPort = source->getUdpPort();
			param->myNick = source->getMyNick();
			param->hubIpPort = source->getHubIpPort();

			params.push_back(param);

			source->setPendingQueryCount(source->getPendingQueryCount() + 1);
			source->setNextQueryTime(aTick + 300000);		// 5 minutes

		}

		bundle = bundleQueue.findSearchBundle(aTick); //may modify the recent search queue
	}

	if(bundle != NULL) {
		searchBundle(bundle, false, false);
	}

	// Request parts info from partial file sharing sources
	for(auto i = params.begin(); i != params.end(); i++){
		const PartsInfoReqParam* param = *i;
		//dcassert(param->udpPort > 0);
		
		try {
			AdcCommand cmd = SearchManager::getInstance()->toPSR(true, param->myNick, param->hubIpPort, param->tth, param->parts);
			udp.writeTo(param->ip, param->udpPort, cmd.toString(ClientManager::getInstance()->getMyCID()));
		} catch(...) {
			dcdebug("Partial search caught error\n");		
		}
		
		delete param;
	}
}

void QueueManager::addList(const HintedUser& aUser, Flags::MaskType aFlags, const string& aInitialDir /* = Util::emptyString */) throw(QueueException, FileException) {
	add(aInitialDir, -1, TTHValue(), aUser, (Flags::MaskType)(QueueItem::FLAG_USER_LIST | aFlags));
}

string QueueManager::getListPath(const HintedUser& user) {
	StringList nicks = ClientManager::getInstance()->getNicks(user);
	string nick = nicks.empty() ? Util::emptyString : Util::cleanPathChars(nicks[0]) + ".";
	return checkTarget(Util::getListPath() + nick + user.user->getCID().toBase32(), /*checkExistence*/ false);
}

void QueueManager::add(const string& aTarget, int64_t aSize, const TTHValue& root, const HintedUser& aUser,
					   Flags::MaskType aFlags /* = 0 */, bool addBad /* = true */, QueueItem::Priority aPrio, BundlePtr aBundle /*NULL*/) throw(QueueException, FileException)
{
	bool wantConnection = true;

	// Check that we're not downloading from ourselves...
	if(aUser == ClientManager::getInstance()->getMe()) {
		throw QueueException(STRING(NO_DOWNLOADS_FROM_SELF));
	}
	
	string target;
	string tempTarget;
	if((aFlags & QueueItem::FLAG_USER_LIST) == QueueItem::FLAG_USER_LIST) {
		if((aFlags & QueueItem::FLAG_PARTIAL_LIST) && !aTarget.empty()) {
			StringList nicks = ClientManager::getInstance()->getNicks(aUser);
			if (nicks.empty())
				throw QueueException(STRING(INVALID_TARGET_FILE));
			target = Util::getListPath() + nicks[0] + ".partial" + Util::toString(Util::rand());
		} else {
			target = getListPath(aUser);
		}
		tempTarget = aTarget;
	} else {
		target = aTarget;
		if (!(aFlags & QueueItem::FLAG_CLIENT_VIEW)) {
			if (!aBundle)
				target = Util::formatTime(aTarget, time(NULL));

			if (BOOLSETTING(DONT_DL_ALREADY_SHARED) && ShareManager::getInstance()->isFileShared(root, Util::getFileName(aTarget))) {
				// Check if we're not downloading something already in our share
				LogManager::getInstance()->message(STRING(FILE_ALREADY_SHARED) + " " + aTarget );
				throw QueueException(STRING(TTH_ALREADY_SHARED));
			}

			if(BOOLSETTING(DOWNLOAD_SKIPLIST_USE_REGEXP) ? AirUtil::stringRegexMatch(SETTING(SKIPLIST_DOWNLOAD), Util::getFileName(aTarget)) :
				Wildcard::patternMatch(Text::utf8ToAcp(Util::getFileName(aTarget)), Text::utf8ToAcp(SETTING(SKIPLIST_DOWNLOAD)), '|')) {
				throw QueueException(STRING(DOWNLOAD_SKIPLIST_MATCH));
			}
		}
		
		//we can check the existence and throw even with FTPlogger support, if the file exists already the directory must exist too.
		target = checkTarget(target, /*checkExistence*/ true);

		if(BOOLSETTING(USE_FTP_LOGGER)) {
			AirUtil::fileEvent(target);
		} 
	}

	// Check if it's a zero-byte file, if so, create and return...
	if(aSize == 0) {
		if(!BOOLSETTING(SKIP_ZERO_BYTE)) {
			File::ensureDirectory(target);
			File f(target, File::WRITE, File::CREATE);
		}
		return;
	}

	bool bundleFinished = false;
	QueueItemPtr q = nullptr;
	{
		WLock l(cs);
		q = fileQueue.find(target);
		if(q) {
			if(q->isFinished()) {
				/* the target file doesn't exist, add our item */
				dcassert(q->getBundle());
				if (q->getBundle()) {
					bundleQueue.removeFinishedItem(q);
					fileQueue.remove(q);
					q = nullptr;
				} else {
					throw QueueException(STRING(FILE_ALREADY_FINISHED));
				}
			} else {
				/* try to add the source for the existing item */
				if(q->getSize() != aSize) {
					throw QueueException(STRING(FILE_WITH_DIFFERENT_SIZE));
				}
				if(!(root == q->getTTH())) {
					throw QueueException(STRING(FILE_WITH_DIFFERENT_TTH));
				}

				q->setFlag(aFlags);
				wantConnection = aUser.user && addSource(q, aUser, (Flags::MaskType)(addBad ? QueueItem::Source::FLAG_MASK : 0), aBundle);
				goto connect;
			}
		}

		if((aFlags & QueueItem::FLAG_USER_LIST) != QueueItem::FLAG_USER_LIST && (aFlags & QueueItem::FLAG_CLIENT_VIEW) != QueueItem::FLAG_CLIENT_VIEW && BOOLSETTING(DONT_DL_ALREADY_QUEUED)) {
			q = fileQueue.getQueuedFile(root, Util::getFileName(aTarget));
			if (q) {
				if (q->isFinished()) {
					/* the target file doesn't exist, add it */
					dcassert(q->getBundle());
					if (q->getBundle()) {
						bundleQueue.removeFinishedItem(q);
						fileQueue.remove(q);
						q = nullptr;
					}
				} else { 
					if(!q->isSource(aUser)) {
						try {
							if (addSource(q, aUser, addBad ? QueueItem::Source::FLAG_MASK : 0)) {
								wantConnection = true;
								goto connect;
							}
						} catch(const Exception&) {
							//...
						}
					}
				}

				if (q) {
					LogManager::getInstance()->message(STRING(FILE_WITH_SAME_TTH) + " " + aTarget );
					throw QueueException(STRING(FILE_WITH_SAME_TTH));
				}
			}
		}

		q = fileQueue.add( target, aSize, aFlags, aPrio, tempTarget, GET_TIME(), root);

		/* Bundles */
		if (aBundle) {
			if (aBundle->getPriority() == Bundle::PAUSED && q->getPriority() == QueueItem::HIGHEST) {
				q->setPriority(QueueItem::HIGH);
			}
			bundleQueue.addBundleItem(q, aBundle);
		} else if ((aFlags & QueueItem::FLAG_USER_LIST) != QueueItem::FLAG_USER_LIST && (aFlags & QueueItem::FLAG_CLIENT_VIEW) != QueueItem::FLAG_CLIENT_VIEW) {
			aBundle = bundleQueue.getMergeBundle(q->getTarget());
			if (aBundle) {
				//finished bundle but failed hashing/scanning?
				bundleFinished = aBundle->isFinished();

				bundleQueue.addBundleItem(q, aBundle);
				aBundle->setDirty(true);
			} else {
				aBundle = new Bundle(q);
			}
		}
		/* Bundles end */

		try {
			wantConnection = aUser.user && addSource(q, aUser, (Flags::MaskType)(addBad ? QueueItem::Source::FLAG_MASK : 0), aBundle);
		} catch(const Exception&) {
			//...
		}
	}

	if (aBundle) {
		if (aBundle->isSet(Bundle::FLAG_NEW)) {
			if (aBundle->isFileBundle()) {
				addBundle(aBundle);
			}
			/* Connect in addBundle */
			return;
		} else {
			if (!bundleFinished) {
				/* Merged into an existing dir bundle */
				LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_ITEM_ADDED)) % 
					q->getTarget().c_str() %
					aBundle->getName().c_str()));

				fire(QueueManagerListener::Added(), q);
				addBundleUpdate(aBundle->getToken());
			} else {
				readdBundle(aBundle);
			}
		}
	} else {
		fire(QueueManagerListener::Added(), q);
	}
connect:
	bool smallSlot = (q->isSet(QueueItem::FLAG_PARTIAL_LIST) || (q->getSize() <= 65792 && !q->isSet(QueueItem::FLAG_USER_LIST) && q->isSet(QueueItem::FLAG_CLIENT_VIEW)));
	if(!aUser.user || !aUser.user->isOnline())
		return;

	if(wantConnection || smallSlot) {
		ConnectionManager::getInstance()->getDownloadConnection(aUser, smallSlot);
	}
}

void QueueManager::readdQISource(const string& target, const HintedUser& aUser) throw(QueueException) {
	bool wantConnection = false;
	{
		WLock l(cs);
		QueueItemPtr q = fileQueue.find(target);
		if(q && q->isBadSource(aUser)) {
			wantConnection = addSource(q, aUser, QueueItem::Source::FLAG_MASK);
		}
	}
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

void QueueManager::readdBundleSource(BundlePtr aBundle, const HintedUser& aUser) throw(QueueException) {
	bool wantConnection = false;
	if (!aBundle)
		return;
	{
		WLock l(cs);
		for_each(aBundle->getQueueItems(), [&](QueueItemPtr q) {
			dcassert(!q->isSource(aUser));
			//LogManager::getInstance()->message("READD, BAD SOURCES: " + Util::toString(q->getBadSources().size()));
			if(q && q->isBadSource(aUser.user)) {
				try {
					if (addSource(q, aUser, QueueItem::Source::FLAG_MASK)) {
						wantConnection = true;
					}
				} catch(...) {
					LogManager::getInstance()->message("Failed to add the source for " + q->getTarget());
				}
				//LogManager::getInstance()->message("SOURCE ADDED FOR: " + q->getTarget() + " CID " + aUser.user->getCID().toBase32());
			} else {
				//LogManager::getInstance()->message("SOURCE NOT ADDED3 FOR: " + q->getTarget() + " CID " + aUser.user->getCID().toBase32());
			}
		});
		aBundle->removeBadSource(aUser);
	}
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

string QueueManager::checkTarget(const string& aTarget, bool checkExistence, BundlePtr aBundle) throw(QueueException, FileException) {
#ifdef _WIN32
	if(aTarget.length() > UNC_MAX_PATH) {
		throw QueueException(STRING(TARGET_FILENAME_TOO_LONG));
	}
	// Check that target starts with a drive or is an UNC path
	if( (aTarget[1] != ':' || aTarget[2] != '\\') &&
		(aTarget[0] != '\\' && aTarget[1] != '\\') ) {
		throw QueueException(STRING(INVALID_TARGET_FILE));
	}
#else
	if(aTarget.length() > PATH_MAX) {
		throw QueueException(STRING(TARGET_FILENAME_TOO_LONG));
	}
	// Check that target contains at least one directory...we don't want headless files...
	if(aTarget[0] != '/') {
		throw QueueException(STRING(INVALID_TARGET_FILE));
	}
#endif

	string target = Util::validateFileName(aTarget);

	// Check that the file doesn't already exist...
	int64_t size = File::getSize(target);
	if(checkExistence && size != -1) {
		if (aBundle) {
			/* TODO: add for recheck */
			aBundle->increaseSize(size);
			aBundle->addSegment(size, false);
		}
		throw FileException(target + ": " + STRING(TARGET_FILE_EXISTS));
	}
	return target;	
}

/** Add a source to an existing queue item */
bool QueueManager::addSource(QueueItemPtr qi, const HintedUser& aUser, Flags::MaskType addBad, bool newBundle) throw(QueueException, FileException) {
	
	if (!aUser.user) //atleast magnet links can cause this to happen.
		throw QueueException("Can't find Source user to add For Target: " + qi->getTargetFileName());

	if(qi->isFinished()) //no need to add source to finished item.
		throw QueueException("Already Finished: " + Util::getFileName(qi->getTarget()));
	
	bool wantConnection = qi->getPriority() != QueueItem::PAUSED;
	if (qi->getBundle()) {
		if (qi->getPriority() != QueueItem::HIGHEST && qi->getBundle()->getPriority() == Bundle::PAUSED) {
			wantConnection = false;
		}
	} else {
		dcassert(qi->getPriority() == QueueItem::HIGHEST);
	}

	if(qi->isSource(aUser)) {
		if(qi->isSet(QueueItem::FLAG_USER_LIST)) {
			return wantConnection;
		}
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + Util::getFileName(qi->getTarget()));
	}

	if(qi->isBadSourceExcept(aUser, addBad)) {
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + Util::getFileName(qi->getTarget()));
	}

		qi->addSource(aUser);
		userQueue.add(qi, aUser, newBundle);

		if ((!SETTING(SOURCEFILE).empty()) && (!BOOLSETTING(SOUNDS_DISABLED)))
			PlaySound(Text::toT(SETTING(SOURCEFILE)).c_str(), NULL, SND_FILENAME | SND_ASYNC);
	
	if (!newBundle) {
		fire(QueueManagerListener::SourcesUpdated(), qi);
	}
	if (qi->getBundle()) {
		qi->getBundle()->setDirty(true);
	}

	return wantConnection;
	
}

void QueueManager::addDirectory(const string& aDir, const HintedUser& aUser, const string& aTarget, QueueItem::Priority p /* = QueueItem::DEFAULT */, bool useFullList) noexcept {
	bool adc=false;
	if (!aUser.user->isSet(User::NMDC)) {
		adc=true;
	}
	bool needList;
	{
		WLock l(cs);
		
		auto dp = directories.equal_range(aUser);
		
		for(auto i = dp.first; i != dp.second; ++i) {
			if(stricmp(aTarget.c_str(), i->second->getName().c_str()) == 0)
				return;
		}
		
		// Unique directory, fine...
		directories.insert(make_pair(aUser, new DirectoryItem(aUser, aDir, aTarget, p)));
		needList = (dp.first == dp.second);
	}
	if(needList || adc) {
		try {
			if (adc && !useFullList) {
				addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_RECURSIVE_LIST, aDir);
			} else {
				addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD, aDir);
			}
		} catch(const Exception&) {
			// Ignore, we don't really care...
		}
	}
}

QueueItem::Priority QueueManager::hasDownload(const UserPtr& aUser, bool smallSlot, string& bundleToken) noexcept {
	RLock l(cs);
	QueueItemPtr qi = userQueue.getNext(aUser, QueueItem::LOWEST, 0, 0, smallSlot);
	if(qi) {
		if (qi->getBundle()) {
			bundleToken = qi->getBundle()->getToken();
		}
		return qi->getPriority() == QueueItem::HIGHEST ? QueueItem::HIGHEST : (QueueItem::Priority)qi->getBundle()->getPriority();
	}
	return QueueItem::PAUSED;
}

void QueueManager::matchListing(const DirectoryListing& dl, int& matches, int& newFiles, BundleList& bundles) noexcept {
	bool wantConnection = false;
	QueueItemList ql;
	//uint64_t start = GET_TICK();
	{
		RLock l(cs);
		fileQueue.matchDir(dl.getRoot(), ql);
	}

	{
		WLock l(cs);
		for_each(ql, [&](QueueItemPtr qi) {
			try {
				if (addSource(qi, dl.getHintedUser(), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
					wantConnection = true;
				}
				newFiles++;
			} catch(const Exception&) {
				//...
			}
			if (qi->getBundle() && find(bundles.begin(), bundles.end(), qi->getBundle()) == bundles.end()) {
				bundles.push_back(qi->getBundle());
			}
		});
	}
	//uint64_t end = GET_TICK();
	//LogManager::getInstance()->message("List matched in " + Util::toString(end-start) + " ms WITHOUT buildMap");

	matches = (int)ql.size();
	if(wantConnection)
		ConnectionManager::getInstance()->getDownloadConnection(dl.getHintedUser());
}

bool QueueManager::getQueueInfo(const UserPtr& aUser, string& aTarget, int64_t& aSize, int& aFlags, string& bundleToken) noexcept {
	RLock l(cs);
	QueueItemPtr qi = userQueue.getNext(aUser);
	if(qi == NULL)
		return false;

	aTarget = qi->getTarget();
	aSize = qi->getSize();
	aFlags = qi->getFlags();
	if (qi->getBundle()) {
		bundleToken = qi->getBundle()->getToken();
	}

	return true;
}

void QueueManager::onSlowDisconnect(const string& aToken) {
	RLock l(cs);
	auto b = bundleQueue.find(aToken);
	if(b) {
		if(b->isSet(Bundle::FLAG_AUTODROP)) {
			b->unsetFlag(Bundle::FLAG_AUTODROP);
		} else {
			b->setFlag(Bundle::FLAG_AUTODROP);
		}
	}
}

bool QueueManager::getAutoDrop(const string& aToken) {
	RLock l(cs);
	auto b = bundleQueue.find(aToken);
	if(b) {
		return b->isSet(Bundle::FLAG_AUTODROP);
	}
	return false;
}

string QueueManager::getTempTarget(const string& aTarget) {
	RLock l(cs);
	auto qi = fileQueue.find(aTarget);
	if(qi) {
		return qi->getTempTarget();
	}
	return Util::emptyString;
}

StringList QueueManager::getTargets(const TTHValue& tth) {
	QueueItemList ql;
	StringList sl;

	RLock l(cs);
	fileQueue.find(tth, ql);
	for(auto i = ql.begin(); i != ql.end(); ++i) {
		sl.push_back((*i)->getTarget());
	}
	return sl;
}

Download* QueueManager::getDownload(UserConnection& aSource, string& aMessage, bool smallSlot) noexcept {
	QueueItemPtr q = NULL;
	Download* d = NULL;
	const UserPtr& u = aSource.getUser();
	{
		RLock l(cs);
		dcdebug("Getting download for %s...", u->getCID().toBase32().c_str());

		q = userQueue.getNext(u, QueueItem::LOWEST, aSource.getChunkSize(), aSource.getSpeed(), smallSlot);
		if (q) {
			//check partial sources
			auto source = q->getSource(u);
			if(source->isSet(QueueItem::Source::FLAG_PARTIAL)) {
				int64_t blockSize = HashManager::getInstance()->getBlockSize(q->getTTH());
				if(blockSize == 0)
					blockSize = q->getSize();
					
				Segment segment = q->getNextSegment(blockSize, aSource.getChunkSize(), aSource.getSpeed(), source->getPartialSource(), false);
				if(segment.getStart() != -1 && segment.getSize() == 0) {
					goto removePartial;
				}
			}
		} else {
			aMessage = userQueue.getLastError();
			dcdebug("none\n");
			return nullptr;
		}

		// Check that the file we will be downloading to exists
		if(q->getDownloadedBytes() > 0) {
			if(!Util::fileExists(q->getTempTarget())) {
				// Temp target gone?
				q->resetDownloaded();
			}
		}
	}

	{
		WLock l(cs);
		d = new Download(aSource, *q);
		userQueue.addDownload(q, d);
	}

	fire(QueueManagerListener::SourcesUpdated(), q);
	dcdebug("found %s\n", q->getTarget().c_str());
	return d;

removePartial:
	WLock l(cs);
	// no other partial chunk from this user, remove him from queue
	removeQI(q, u);
	q->removeSource(u, QueueItem::Source::FLAG_NO_NEED_PARTS);
	aMessage = STRING(NO_NEEDED_PART);
	return nullptr;
}

void QueueManager::moveFile(const string& source, const string& target, BundlePtr aBundle) {
	File::ensureDirectory(target);
	if(File::getSize(source) > MOVER_LIMIT) {
		mover.moveFile(source, target, aBundle);
	} else {
		moveFile_(source, target, aBundle);
	}
}

void QueueManager::moveFile_(const string& source, const string& target, BundlePtr aBundle) {
	try {
		File::renameFile(source, target);
		//getInstance()->fire(QueueManagerListener::FileMoved(), target);
	} catch(const FileException& /*e1*/) {
		// Try to just rename it to the correct name at least
		string newTarget = Util::getFilePath(source) + Util::getFileName(target);
		try {
			File::renameFile(source, newTarget);
			LogManager::getInstance()->message(source + " " + STRING(RENAMED_TO) + " " + newTarget);
		} catch(const FileException& e2) {
			LogManager::getInstance()->message(STRING(UNABLE_TO_RENAME) + " " + source + ": " + e2.getError());
		}
	}
	if(BOOLSETTING(USE_FTP_LOGGER))
		AirUtil::fileEvent(target, true);

	if (!aBundle) {
		return;
	}

	aBundle->increaseMoved();
	if (aBundle->getQueueItems().empty() && (static_cast<size_t>(aBundle->getMoved()) == aBundle->getFinishedFiles().size())) {
		if (!SETTING(SCAN_DL_BUNDLES) || aBundle->isFileBundle()) {
			LogManager::getInstance()->message(str(boost::format(STRING(DL_BUNDLE_FINISHED)) % 
				aBundle->getName().c_str()));
		} else if (SETTING(SCAN_DL_BUNDLES) && !ShareScannerManager::getInstance()->scanBundle(aBundle)) {
			aBundle->setFlag(Bundle::FLAG_SCAN_FAILED);
			return;
		} 

		if (BOOLSETTING(ADD_FINISHED_INSTANTLY)) {
			getInstance()->hashBundle(aBundle);
		} else {
			LogManager::getInstance()->message(CSTRING(INSTANT_SHARING_DISABLED));
		}
	}
}

void QueueManager::hashBundle(BundlePtr aBundle) {
	if(ShareManager::getInstance()->allowAddDir(aBundle->getTarget())) {
		aBundle->setFlag(Bundle::FLAG_HASH);
		QueueItemList hash;
		{
			WLock l(cs);
			for (auto i = aBundle->getFinishedFiles().begin(); i != aBundle->getFinishedFiles().end();) {
				QueueItemPtr qi = *i;
				if (AirUtil::checkSharedName(Text::toLower(qi->getTarget()), false, false, qi->getSize()) && Util::fileExists(qi->getTarget())) {
					hash.push_back(qi);
					++i;
					continue;
				}
				//erase failed items
				bundleQueue.removeFinishedItem(qi);
				fileQueue.remove(qi);
			}
		}

		for_each(hash, [&](QueueItemPtr q) {
			try {
				// Schedule for hashing, it'll be added automatically later on...
				if (!HashManager::getInstance()->checkTTH(q->getTarget(), q->getSize(), AirUtil::getLastWrite(q->getTarget()))) {
					//..
				} else {
					//fine, it's there already..
					aBundle->increaseHashed();
				}
			} catch(const Exception&) {
				//...
			}
		});

		//all files have been hashed already?
		if(aBundle->getFinishedFiles().size() == static_cast<size_t>(aBundle->getHashed())) {
			bundleHashed(aBundle);
		}
	} else if (BOOLSETTING(ADD_FINISHED_INSTANTLY)) {
		LogManager::getInstance()->message(str(boost::format(STRING(NOT_IN_SHARED_DIR)) % 
			aBundle->getName().c_str()));
	} else {
		LogManager::getInstance()->message(CSTRING(INSTANT_SHARING_DISABLED));
	}
}

void QueueManager::onFileHashed(const string& fname, const TTHValue& root, bool failed) {
	QueueItemPtr qi = NULL;
	{
		RLock l(cs);
		if (failed) {
			string file = Util::getFileName(fname);
			for (auto s = fileQueue.getTTHIndex().begin(); s != fileQueue.getTTHIndex().end(); ++s) {
				if (s->second->getTargetFileName() == Util::getFileName(file)) {
					qi = s->second;
					if (qi->getTarget() == fname) { //prefer exact matches
						break;
					}
				}
			}
		} else {
			QueueItemList ql;
			fileQueue.find(root, ql);
			for (auto s = ql.begin(); s != ql.end(); ++s) {
				if ((*s)->getTargetFileName() == Util::getFileName(fname)) {
					qi = (*s);
					break;
				}
			}
		}
	}

	if (!qi) {
		if (!failed) {
			fire(QueueManagerListener::FileHashed(), fname, root);
		}
		return;
	}

	BundlePtr b = qi->getBundle();
	if (!b) {
		LogManager::getInstance()->message("No bundle for a hashed QI " + qi->getTarget());
		if (!failed) {
			fire(QueueManagerListener::FileHashed(), fname, root);
		}

		WLock l(cs);
		fileQueue.remove(qi);
		return;
	}

	b->increaseHashed();

	if (failed) {
		b->setFlag(Bundle::FLAG_HASH_FAILED);
	}

	if (b->getHashed() == (int)b->getFinishedFiles().size()) {
		bundleHashed(b);
	}
}

void QueueManager::bundleHashed(BundlePtr b) {
	WLock l(cs);
	if (!b->getQueueItems().empty()) {
		//new items have been added while it was being hashed
		b->resetHashed();
		b->unsetFlag(Bundle::FLAG_HASH);
		return;
	}

	if (b->isSet(Bundle::FLAG_HASH)) {
		if (!b->isSet(Bundle::FLAG_HASH_FAILED)) {
			if (!b->isFileBundle()) {
				fire(QueueManagerListener::BundleHashed(), b->getTarget());
			} else {
				fire(QueueManagerListener::FileHashed(), b->getFinishedFiles().front()->getTargetFileName(), b->getFinishedFiles().front()->getTTH());
			}
		} else {
			b->resetHashed(); //for the next attempts
			LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_HASH_FAILED)) % 
				b->getName().c_str()));
			return;
		}
	} else {
		//instant sharing disabled/the folder wasn't shared when the bundle finished
		LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_HASHED)) % 
			b->getName().c_str()));
	}

	for_each(b->getFinishedFiles(), [&] (QueueItemPtr qi) { fileQueue.remove(qi); } );
	bundleQueue.remove(b);
}

void QueueManager::moveStuckFile(QueueItemPtr qi) {

	moveFile(qi->getTempTarget(), qi->getTarget(), qi->getBundle());

	if(qi->isFinished()) {
		WLock l(cs);
		userQueue.removeQI(qi);
	}

	string target = qi->getTarget();

	if(!BOOLSETTING(KEEP_FINISHED_FILES)) {
		fire(QueueManagerListener::Removed(), qi);
		fileQueue.remove(qi);
		removeBundleItem(qi, true);
	 } else {
		qi->addSegment(Segment(0, qi->getSize()), false);
		fire(QueueManagerListener::StatusUpdated(), qi);
	}

	fire(QueueManagerListener::RecheckAlreadyFinished(), target);
}

void QueueManager::rechecked(QueueItemPtr qi) {
	fire(QueueManagerListener::RecheckDone(), qi->getTarget());
	fire(QueueManagerListener::StatusUpdated(), qi);
	if (qi->getBundle()) {
		qi->getBundle()->setDirty(true);
	}
}

void QueueManager::putDownload(Download* aDownload, bool finished, bool reportFinish) noexcept {
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
		q = fileQueue.find(d->getPath());
		if(!q) {
			// Target has been removed, clean up the mess
			auto hasTempTarget = !d->getTempTarget().empty();
			auto isFullList = d->getType() == Transfer::TYPE_FULL_LIST;
			auto isFile = d->getType() == Transfer::TYPE_FILE && d->getTempTarget() != d->getPath();

			if(hasTempTarget && (isFullList || isFile)) {
				File::deleteFile(d->getTempTarget());
			}

			return;
		}

		if(!finished) {
			if(d->getType() == Transfer::TYPE_FULL_LIST && !d->getTempTarget().empty()) {
				// No use keeping an unfinished file list...
				File::deleteFile(d->getTempTarget());
			}

			if(d->getType() != Transfer::TYPE_TREE && q->getDownloadedBytes() == 0) {
				q->setTempTarget(Util::emptyString);
			}

			if(d->getType() == Transfer::TYPE_FILE) {
				// mark partially downloaded chunk, but align it to block size
				int64_t downloaded = d->getPos();
				downloaded -= downloaded % d->getTigerTree().getBlockSize();

				if(downloaded > 0) {
					q->addSegment(Segment(d->getStartPos(), downloaded), true);
					if (q->getBundle()) {
						q->getBundle()->setDirty(true);
					}
				}
			}

			if(q->getPriority() != QueueItem::PAUSED) {
				q->getOnlineUsers(getConn);
			}

			userQueue.removeDownload(q, d->getUser());
			fire(QueueManagerListener::StatusUpdated(), q);
		} else { // Finished
			if(d->getType() == Transfer::TYPE_PARTIAL_LIST) {
				if( (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) && directories.find(d->getUser()) != directories.end()) ||
					(q->isSet(QueueItem::FLAG_MATCH_QUEUE)) ||
					(q->isSet(QueueItem::FLAG_VIEW_NFO)))
				{					
					fl_fname = d->getPFS();
					fl_flag = (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) ? (QueueItem::FLAG_DIRECTORY_DOWNLOAD) : 0)
						| (q->isSet(QueueItem::FLAG_PARTIAL_LIST) ? (QueueItem::FLAG_PARTIAL_LIST) : 0)
						| (q->isSet(QueueItem::FLAG_MATCH_QUEUE) ? QueueItem::FLAG_MATCH_QUEUE : 0) | QueueItem::FLAG_TEXT
						| (q->isSet(QueueItem::FLAG_VIEW_NFO) ? QueueItem::FLAG_VIEW_NFO : 0);
				} else {
					fire(QueueManagerListener::PartialList(), d->getHintedUser(), d->getPFS());
				}
				userQueue.removeQI(q);
				fire(QueueManagerListener::Removed(), q);
				fileQueue.remove(q);
			} else if(d->getType() == Transfer::TYPE_TREE) {
				// Got a full tree, now add it to the HashManager
				dcassert(d->getTreeValid());
				HashManager::getInstance()->addTree(d->getTigerTree());

				userQueue.removeDownload(q, d->getUser());
				fire(QueueManagerListener::StatusUpdated(), q);
			} else if(d->getType() == Transfer::TYPE_FULL_LIST) {
				if(d->isSet(Download::FLAG_XML_BZ_LIST)) {
					q->setFlag(QueueItem::FLAG_XML_BZLIST);
				} else {
					q->unsetFlag(QueueItem::FLAG_XML_BZLIST);
				}

				auto dir = q->getTempTarget(); // We cheated and stored the initial display directory here (when opening lists from search)
				q->addSegment(Segment(0, q->getSize()), true);

				// Now, let's see if this was a directory download filelist...
				if( (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) && directories.find(d->getUser()) != directories.end()) ||
					(q->isSet(QueueItem::FLAG_MATCH_QUEUE)) )
				{
					fl_fname = q->getListName();
					fl_flag = (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) ? QueueItem::FLAG_DIRECTORY_DOWNLOAD : 0)
						| (q->isSet(QueueItem::FLAG_MATCH_QUEUE) ? QueueItem::FLAG_MATCH_QUEUE : 0);
				}

				userQueue.removeQI(q);
				fire(QueueManagerListener::Finished(), q, dir, d->getHintedUser(), d->getAverageSpeed());

				fire(QueueManagerListener::Removed(), q);
				fileQueue.remove(q);
			} else if(d->getType() == Transfer::TYPE_FILE) {
				d->setOverlapped(false);
				q->addSegment(d->getSegment(), true);

				if(q->isFinished()) {
					// Disconnect all possible overlapped downloads
					for(auto i = q->getDownloads().begin(); i != q->getDownloads().end(); ++i) {
						if(compare((*i)->getToken(), d->getToken()) != 0)
							(*i)->getUserConnection().disconnect();
					}

					removeFinished = true;
					userQueue.removeQI(q);
					fire(QueueManagerListener::Finished(), q, Util::emptyString, d->getHintedUser(), d->getAverageSpeed());

					if(BOOLSETTING(KEEP_FINISHED_FILES)) {
						fire(QueueManagerListener::StatusUpdated(), q);
					} else {
						fire(QueueManagerListener::Removed(), q);
						if (!d->getBundle())
							fileQueue.remove(q);
					}
				} else {
					userQueue.removeDownload(q, d->getUser());
					fire(QueueManagerListener::StatusUpdated(), q);
				}
			} else {
				dcassert(0);
			}

			if (q->getBundle()) {
				q->getBundle()->setDirty(true);
			}
		}
	}

	if (removeFinished) {
		if (q->getBundle())
			fileQueue.decreaseSize(q->getSize());
		UploadManager::getInstance()->abortUpload(q->getTempTarget());

		if (q->getBundle()) {
			removeBundleItem(q, true);
		}

		// Check if we need to move the file
		if(!d->getTempTarget().empty() && (Util::stricmp(d->getPath().c_str(), d->getTempTarget().c_str()) != 0) ) {
			moveFile(d->getTempTarget(), d->getPath(), d->getBundle());
		}

		if(BOOLSETTING(LOG_DOWNLOADS)) {
			ParamMap params;
			d->getParams(d->getUserConnection(), params);
			LOG(LogManager::DOWNLOAD, params);
		}
	}

	for(auto i = getConn.begin(); i != getConn.end(); ++i) {
		if ((*i).user != d->getUser())
			ConnectionManager::getInstance()->getDownloadConnection(*i);
	}

	if(!fl_fname.empty()) {
		if (d->isSet(Download::FLAG_TTHLIST)) {	 
			matchTTHList(fl_fname, d->getHintedUser(), fl_flag);	 
		} else {	 
			processList(fl_fname, d->getHintedUser(), d->getTempTarget(), fl_flag); 
		}
	}
}

void QueueManager::matchTTHList(const string& name, const HintedUser& user, int flags) {	 
	dcdebug("matchTTHList");
	if(flags & QueueItem::FLAG_MATCH_QUEUE) {
		bool wantConnection = false;
		int matches = 0;
		 
		typedef vector<TTHValue> TTHList;
		TTHList tthList;
 	 
		size_t start = 0;
		while (start+39 < name.length()) {
			tthList.push_back(TTHValue(name.substr(start, 39)));
			start = start+40;
		}
 	 
		if(tthList.empty())
			return;

 		{	 
			WLock l(cs);
			for (auto s = tthList.begin(); s != tthList.end(); ++s) {
				QueueItemList ql;
				fileQueue.find(*s, ql);
				for_each(ql, [&](QueueItemPtr qi) {
					try {
						if (addSource(qi, user, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
							wantConnection = true;
						}
					} catch(...) {
						// Ignore...
					}
					matches++;
				});
			}
		}

		if((matches > 0) && wantConnection)
			ConnectionManager::getInstance()->getDownloadConnection(user); 
	}
 }

void QueueManager::processList(const string& name, const HintedUser& user, const string& path, int flags) {
	DirectoryListing dirList(user, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0);
	try {
		if(flags & QueueItem::FLAG_TEXT) {
			MemoryInputStream mis(name);
			dirList.loadXML(mis, true, false);
		} else {
			dirList.loadFile(name, false);
		}
	} catch(const Exception&) {
		LogManager::getInstance()->message(STRING(UNABLE_TO_OPEN_FILELIST) + " " + name);
		return;
	}

	if(flags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		if ((flags & QueueItem::FLAG_PARTIAL_LIST) && !path.empty()) {
			//partial list
			DirectoryItem* d = NULL;
			{
				WLock l(cs);
				auto dp = directories.equal_range(user);
				auto udp = find_if(dp.first, dp.second, [&](pair<UserPtr, DirectoryItemPtr> ud) { return stricmp(path.c_str(), ud.second->getName().c_str()) == 0; });
				if (udp != dp.second) {
					d = udp->second;
					directories.erase(udp);
				}
			}

			if(d != NULL){
				dirList.download(d->getName(), d->getTarget(), false, d->getPriority(), true);
				delete d;
			}
		} else {
			//full filelist
			vector<DirectoryItemPtr> dl;
			{
				WLock l(cs);
				auto dpf = directories.equal_range(user) | map_values;
				dl.assign(boost::begin(dpf), boost::end(dpf));
				directories.erase(user);
			}
			for(auto i = dl.begin(); i != dl.end(); ++i) {
				DirectoryItem* di = *i;
				dirList.download(di->getName(), di->getTarget(), false, di->getPriority());
				delete di;
			}
		}
	}

	if(flags & QueueItem::FLAG_MATCH_QUEUE) {
		int matches=0, newFiles=0;
		BundleList bundles;
		matchListing(dirList, matches, newFiles, bundles);
		if ((flags & QueueItem::FLAG_PARTIAL_LIST) && (!SETTING(REPORT_ADDED_SOURCES) || newFiles == 0 || bundles.empty())) {
			return;
		}
		LogManager::getInstance()->message(Util::toString(ClientManager::getInstance()->getNicks(user)) + ": " + AirUtil::formatMatchResults(matches, newFiles, bundles, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0));
	} else if((flags & QueueItem::FLAG_VIEW_NFO) && (flags & QueueItem::FLAG_PARTIAL_LIST)) {
		dirList.findNfo(path);
	}
}

void QueueManager::recheck(const string& aTarget) {
	rechecker.add(aTarget);
}

void QueueManager::remove(const string aTarget) noexcept {
	QueueItemPtr qi = NULL;
	{
		RLock l(cs);
		qi = fileQueue.find(aTarget);
	}
	if (qi) {
		removeQI(qi);
	}
}

void QueueManager::removeQI(QueueItemPtr q, bool moved /*false*/) noexcept {
	UserConnectionList x;
	dcassert(q);
	{

		// For partial-share
		UploadManager::getInstance()->abortUpload(q->getTempTarget());

		WLock l(cs);

		if(q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD)) {
			dcassert(q->getSources().size() == 1);
			auto dp = directories.equal_range(q->getSources()[0].getUser());
			for(auto i = dp.first; i != dp.second; ++i) {
				delete i->second;
			}
			directories.erase(q->getSources()[0].getUser());
		}

		if(q->isRunning()) {
			for_each(q->getDownloads(), [&] (Download* d) { x.push_back(&d->getUserConnection()); } );
		} else if(!q->getTempTarget().empty() && q->getTempTarget() != q->getTarget()) {
			File::deleteFile(q->getTempTarget());
		}

		if(!q->isFinished()) {
			userQueue.removeQI(q);
		}

		if (!moved) {
			fire(QueueManagerListener::Removed(), q);
		}
		fileQueue.remove(q);
	}

	removeBundleItem(q, false, moved);
	for_each(x, [] (UserConnection* u) { u->disconnect(true); } );
}

void QueueManager::removeSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType reason, bool removeConn /* = true */) noexcept {
	QueueItemPtr qi = NULL;
	{
		RLock l(cs);
		qi = fileQueue.find(aTarget);
	}
	if (qi)
		removeSource(qi, aUser, reason, removeConn);
}

void QueueManager::removeSource(QueueItemPtr q, const UserPtr& aUser, Flags::MaskType reason, bool removeConn /* = true */) noexcept {
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

		if(reason == QueueItem::Source::FLAG_NO_TREE) {
			q->getSource(aUser)->setFlag(reason);
			return;
		}

		if(q->isRunning()) {
			isRunning = true;
		}
		userQueue.removeQI(q, aUser, false, true);
		q->removeSource(aUser, reason);
		
		fire(QueueManagerListener::SourcesUpdated(), q);

		BundlePtr b = q->getBundle();
		if (b) {
			if (!b->isSource(aUser)) {
				b->sendRemovePBD(aUser);
			}
			b->setDirty(true);
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

void QueueManager::removeSource(const UserPtr& aUser, Flags::MaskType reason) noexcept {
	// @todo remove from finished items
	QueueItemList items;

	{
		RLock l(cs);
		userQueue.getUserQIs(aUser, items);
	}

	for_each(items, [&](QueueItemPtr qi) { removeSource(qi, aUser, reason); });
}

void QueueManager::setBundlePriority(const string& bundleToken, Bundle::Priority p) noexcept {
	BundlePtr bundle = NULL;
	{
		RLock l(cs);
		bundle = bundleQueue.find(bundleToken);
	}

	setBundlePriority(bundle, p, false);
}

void QueueManager::setBundlePriority(BundlePtr aBundle, Bundle::Priority p, bool isAuto, bool isQIChange /*false*/) noexcept {
	QueueItemPtr fileBundleQI = NULL;
	Bundle::Priority oldPrio = aBundle->getPriority();
	//LogManager::getInstance()->message("Changing priority to: " + Util::toString(p));
	if (oldPrio == p) {
		//LogManager::getInstance()->message("Prio not changed: " + Util::toString(oldPrio));
		return;
	}

	{
		WLock l(cs);
		bundleQueue.removeSearchPrio(aBundle);
		userQueue.setBundlePriority(aBundle, p);
		bundleQueue.addSearchPrio(aBundle);
		bundleQueue.recalculateSearchTimes(aBundle, true);
		if (!isAuto) {
			aBundle->setAutoPriority(false);
		}

		fire(QueueManagerListener::BundlePriority(), aBundle);
		if (aBundle->isFileBundle() && !isQIChange) {
			fileBundleQI = aBundle->getQueueItems().front();
		}
	}

	aBundle->setDirty(true);

	if(p == Bundle::PAUSED) {
		DownloadManager::getInstance()->disconnectBundle(aBundle);
	} else if (oldPrio == Bundle::PAUSED || oldPrio == Bundle::LOWEST) {
		connectBundleSources(aBundle);
	}

	if (fileBundleQI) {
		setQIPriority(fileBundleQI, (QueueItem::Priority)p, isAuto, true);
		if (aBundle->getAutoPriority() != fileBundleQI->getAutoPriority()) {
			setQIAutoPriority(fileBundleQI->getTarget(), aBundle->getAutoPriority(), true);
		}
	}

	//LogManager::getInstance()->message("Prio changed to: " + Util::toString(bundle->getPriority()));
}

void QueueManager::setBundleAutoPriority(const string& bundleToken, bool isQIChange /*false*/) noexcept {
	BundlePtr b = NULL;
	{
		RLock l(cs);
		b = bundleQueue.find(bundleToken);
		if (b) {
			b->setAutoPriority(!b->getAutoPriority());
			b->setDirty(true);
		}
	}

	if (b) {
		if (!isQIChange && b->isFileBundle()) {
			QueueItemPtr qi = nullptr;
			{
				RLock l (cs);
				qi = b->getQueueItems().front();
			}

			if (qi->getAutoPriority() != b->getAutoPriority()) {
				setQIAutoPriority(qi->getTarget(), b->getAutoPriority(), true);
			}
		}

		if (SETTING(DOWNLOAD_ORDER) == SettingsManager::ORDER_BALANCED) {
			calculateBundlePriorities(false);
			if (b->getPriority() == Bundle::PAUSED) {
				//failed to count, but we don't want it to stay paused
				setBundlePriority(b, Bundle::LOW, true);
			}
		} else if (SETTING(DOWNLOAD_ORDER) == SettingsManager::ORDER_PROGRESS) {
			setBundlePriority(b, b->calculateProgressPriority(), true);
		}
	}
}

void QueueManager::getBundleSources(BundlePtr aBundle, Bundle::SourceInfoList& sources, Bundle::SourceInfoList& badSources) noexcept {
	RLock l(cs);
	sources = aBundle->getSources();
	badSources = aBundle->getBadSources();
}

void QueueManager::removeBundleSources(BundlePtr aBundle) noexcept {
	Bundle::SourceInfoList tmp;
	{
		RLock l(cs);
		tmp = aBundle->getSources();
	}

	for(auto si = tmp.begin(); si != tmp.end(); si++) {
		removeBundleSource(aBundle, get<Bundle::SOURCE_USER>(*si).user);
	}
}

void QueueManager::removeBundleSource(const string& bundleToken, const UserPtr& aUser) noexcept {
	BundlePtr bundle = NULL;
	{
		RLock l(cs);
		bundle = bundleQueue.find(bundleToken);
	}
	removeBundleSource(bundle, aUser);
}

void QueueManager::removeBundleSource(BundlePtr aBundle, const UserPtr& aUser) noexcept {
	if (aBundle) {
		QueueItemList ql;
		{
			RLock l(cs);
			aBundle->getItems(aUser, ql);
		}

		for_each(ql, [&](QueueItemPtr qi) {
			//LogManager::getInstance()->message("Remove bundle source: " + aUser->getCID().toBase32());
			removeSource(qi, aUser, QueueItem::Source::FLAG_REMOVED);
			dcassert(qi->isBadSource(aUser));
			dcassert(!qi->isSource(aUser));
			//LogManager::getInstance()->message("SOURCE REMOVED FROM: " + (*i)->getTarget());
			//LogManager::getInstance()->message("REMOVE, BAD SOURCES: " + Util::toString((*i)->getBadSources().size()));
		});

		//LogManager::getInstance()->message("Source removed: " + aUser->getCID().toBase32());
		dcassert(!aBundle->isSource(aUser));
	}
}

void QueueManager::setQIPriority(const string& aTarget, QueueItem::Priority p) noexcept {
	QueueItemPtr q = NULL;
	{
		RLock l(cs);
		q = fileQueue.find(aTarget);
	}
	setQIPriority(q, p);
}

void QueueManager::setQIPriority(QueueItemPtr q, QueueItem::Priority p, bool isAP /*false*/, bool isBundleChange /*false*/) noexcept {
	HintedUserList getConn;
	bool running = false;
	BundlePtr b = q->getBundle();

	{
		WLock l(cs);
		if((q != NULL) && (q->getPriority() != p) && !q->isFinished() ) {
			if (!b) {
				//those should always use the highest prio
				return;
			}

			if((q->getPriority() == QueueItem::PAUSED && b->getPriority() != Bundle::PAUSED) || p == QueueItem::HIGHEST) {
				// Problem, we have to request connections to all these users...
				q->getOnlineUsers(getConn);
			}
			running = q->isRunning();
			userQueue.setQIPriority(q, p);
			fire(QueueManagerListener::StatusUpdated(), q);
		}
	}

	if (b->isFileBundle() && !isBundleChange) {
		setBundlePriority(b, (Bundle::Priority)p, isAP, true);
	} else {
		b->setDirty(true);
	}

	if(p == QueueItem::PAUSED && running) {
		DownloadManager::getInstance()->abortDownload(q->getTarget());
	} else {
		for_each(getConn, [&](HintedUser& u) {
			ConnectionManager::getInstance()->getDownloadConnection(u);
		});
	}
}

void QueueManager::setQIAutoPriority(const string& aTarget, bool ap, bool isBundleChange /*false*/) noexcept {
	vector<pair<QueueItemPtr, QueueItem::Priority>> priorities;
	string bundleToken;

	{
		RLock l(cs);
		QueueItemPtr q = fileQueue.find(aTarget);
		if ((q != NULL) && (q->getAutoPriority() != ap)) {
			q->setAutoPriority(ap);
			if (!isBundleChange && q->getBundle()->isFileBundle()) {
				BundlePtr bundle = q->getBundle();
				if (q->getAutoPriority() != bundle->getAutoPriority()) {
					bundleToken = bundle->getToken();
				}
			}
			if(ap && !isBundleChange) {
				if (SETTING(DOWNLOAD_ORDER) == SettingsManager::ORDER_PROGRESS) {
					priorities.push_back(make_pair(q, q->calculateAutoPriority()));
				} else if (q->getPriority() == QueueItem::PAUSED) {
					priorities.push_back(make_pair(q, QueueItem::LOW));
				}
			}
			dcassert(q->getBundle());
			q->getBundle()->setDirty(true);
			fire(QueueManagerListener::StatusUpdated(), q);
		}
	}

	for_each(priorities, [&](pair<QueueItemPtr, QueueItem::Priority> qp) {
		setQIPriority(qp.first, (QueueItem::Priority)qp.second);
	});

	if (!bundleToken.empty()) {
		setBundleAutoPriority(bundleToken, true);
	}
}

void QueueManager::saveQueue(bool force) noexcept {
	RLock l(cs);	
	bundleQueue.saveQueue(force);

	// Put this here to avoid very many saves tries when disk is full...
	lastSave = GET_TICK();
}

class QueueLoader : public SimpleXMLReader::CallBack {
public:
	QueueLoader() : cur(NULL), inDownloads(false), inBundle(false), inFile(false) { }
	~QueueLoader() { }
	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name, const string& data);
private:
	string target;

	QueueItemPtr cur;
	BundlePtr curBundle;
	bool inDownloads;
	bool inBundle;
	bool inFile;
	string curToken;
};

void QueueManager::loadQueue() noexcept {
	QueueLoader l;
	//Util::migrate(Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml");

	StringList fileList = File::findFiles(Util::getPath(Util::PATH_BUNDLES), "Bundle*");
	for (auto i = fileList.begin(); i != fileList.end(); ++i) {
		//LogManager::getInstance()->message("LOADING BUNDLE1: " + *i);
		if ((*i).substr((*i).length()-4, 4) == ".xml") {
			//LogManager::getInstance()->message("LOADING BUNDLE2: " + *i);
			try {
				File f(*i, File::READ, File::OPEN);
				SimpleXMLReader(&l).parse(f);
			} catch(const Exception&) {
				LogManager::getInstance()->message("Failed to load the bundle: " + *i);
			}
		}
	}

	try {
		//load the old queue file and delete it
		File f(Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml", File::READ, File::OPEN);
		SimpleXMLReader(&l).parse(f);
		f.close();
		File::copyFile(Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml", Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml.bak");
		File::deleteFile(Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml");
	} catch(const Exception&) {
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
static const string sSegment = "Segment";
static const string sStart = "Start";
static const string sAutoPriority = "AutoPriority";
static const string sMaxSegments = "MaxSegments";
static const string sBundleToken = "BundleToken";
static const string sFinished = "Finished";



void QueueLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	QueueManager* qm = QueueManager::getInstance();
	if(!inDownloads && name == "Downloads") {
		inDownloads = true;
	} else if (!inFile && name == sFile) {
		curToken = getAttrib(attribs, sToken, 1);
		inFile = true;		
	} else if (!inBundle && name == sBundle) {
		const string& bundleTarget = getAttrib(attribs, sTarget, 0);
		//LogManager::getInstance()->message("BUNDLEFOUND!!!!!!!!!!!!!: " + bundleTarget);
		const string& token = getAttrib(attribs, sToken, 1);
		if(token.empty())
			return;

		const string& prio = getAttrib(attribs, sPriority, 3);
		time_t added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 4)));
		time_t dirDate = static_cast<time_t>(Util::toInt(getAttrib(attribs, sDate, 5)));
		if(added == 0) {
			added = GET_TIME();
		}

		BundlePtr bundle = BundlePtr(new Bundle(bundleTarget, added, !prio.empty() ? (Bundle::Priority)Util::toInt(prio) : Bundle::DEFAULT));
		bundle->setDirDate(dirDate);
		bundle->setToken(token);
		curBundle = bundle;
		inBundle = true;		
	} else if(inDownloads || inBundle || inFile) {
		if(cur == NULL && name == sDownload) {
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			if(size == 0)
				return;
			try {
				const string& tgt = getAttrib(attribs, sTarget, 0);
				// @todo do something better about existing files
				target = QueueManager::checkTarget(tgt, true, curBundle);
				if(target.empty())
					return;
			} catch(const Exception&) {
				return;
			}
			QueueItem::Priority p = (QueueItem::Priority)Util::toInt(getAttrib(attribs, sPriority, 3));
			time_t added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 4)));
			const string& tthRoot = getAttrib(attribs, sTTH, 5);
			if(tthRoot.empty())
				return;

			string tempTarget = getAttrib(attribs, sTempTarget, 5);
			uint8_t maxSegments = (uint8_t)Util::toInt(getAttrib(attribs, sMaxSegments, 5));

			if(added == 0)
				added = GET_TIME();

			QueueItemPtr qi = qm->fileQueue.find(target);

			if(qi == NULL) {
				if (Util::toInt(getAttrib(attribs, sAutoPriority, 6)) == 1) {
					p = QueueItem::DEFAULT;
				}

				qi = qm->fileQueue.add(target, size, 0, p, tempTarget, added, TTHValue(tthRoot));
				qi->setMaxSegments(max((uint8_t)1, maxSegments));

				//bundles
				if (curBundle && inBundle) {
					//LogManager::getInstance()->message("itemtoken exists: " + bundleToken);
					qm->bundleQueue.addBundleItem(qi, curBundle);
				} else if (inDownloads) {
					//assign bundles for the items in the old queue file
					curBundle = new Bundle(qi);
				} else if (inFile && !curToken.empty()) {
					curBundle = new Bundle(qi, curToken);
				}
			}
			if(!simple)
				cur = qi;
		} else if(cur && name == sSegment) {
			int64_t start = Util::toInt64(getAttrib(attribs, sStart, 0));
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			
			if(size > 0 && start >= 0 && (start + size) <= cur->getSize()) {
				cur->addSegment(Segment(start, size), false);
				if (cur->getAutoPriority() && SETTING(DOWNLOAD_ORDER) == SettingsManager::ORDER_PROGRESS) {
					cur->setPriority(cur->calculateAutoPriority());
				}
			}
		} else if(cur && name == sSource) {
			const string& cid = getAttrib(attribs, sCID, 0);
			if(cid.length() != 39) {
				// Skip loading this source - sorry old users
				return;
			}
			UserPtr user = ClientManager::getInstance()->getUser(CID(cid));
			ClientManager::getInstance()->updateNick(user, getAttrib(attribs, sNick, 1));

			try {
				const string& hubHint = getAttrib(attribs, sHubHint, 1);
				HintedUser hintedUser(user, hubHint);
				qm->addSource(cur, hintedUser, 0) && user->isOnline();
			} catch(const Exception&) {
				return;
			}
		} else if(inBundle && curBundle && name == sFinished) {
			//LogManager::getInstance()->message("FOUND FINISHED TTH");
			const string& tth = getAttrib(attribs, sTTH, 0);
			const string& target = getAttrib(attribs, sTarget, 0);
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			time_t added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 4)));
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

void QueueLoader::endTag(const string& name, const string&) {
	
	if(inDownloads || inBundle || inFile) {
		if(name == "Downloads") {
			inDownloads = false;
		} else if(name == sBundle) {
			QueueManager::getInstance()->addBundle(curBundle, true);
			curBundle = NULL;
			inBundle = false;
		} else if(name == sFile) {
			curToken = Util::emptyString;
			inFile = false;
		} else if(name == sDownload) {
			if (curBundle->isFileBundle() && !curBundle->getQueueItems().empty()) {
				QueueManager::getInstance()->bundleQueue.add(curBundle);
			}
			cur = NULL;
		}
	}
}

void QueueManager::addFinishedItem(const TTHValue& tth, BundlePtr aBundle, const string& aTarget, int64_t aSize, time_t aFinished) {
	//LogManager::getInstance()->message("ADD FINISHED TTH: " + tth.toBase32());
	if (fileQueue.find(aTarget)) {
		return;
	}
	QueueItemPtr qi = new QueueItem(aTarget, aSize, QueueItem::DEFAULT, QueueItem::FLAG_NORMAL, aFinished, tth, Util::emptyString);
	qi->addSegment(Segment(0, aSize), false, true); //make it complete

	fileQueue.add(qi, true);
	bundleQueue.addFinishedItem(qi, aBundle);
	//LogManager::getInstance()->message("added finished tth, totalsize: " + Util::toString(aBundle->getFinishedFiles().size()));
}

void QueueManager::noDeleteFileList(const string& path) {
	if(!BOOLSETTING(KEEP_LISTS)) {
		protectedFileLists.push_back(path);
	}
}

// SearchManagerListener
void QueueManager::on(SearchManagerListener::SR, const SearchResultPtr& sr) noexcept {
	if (!BOOLSETTING(AUTO_ADD_SOURCE)) {
		return;
	}

	bool wantConnection = false;
	bool addSources = false;
	BundlePtr bundle = NULL;
	QueueItemPtr qi = NULL;

	{
		QueueItemList matches;

		RLock l(cs);
		fileQueue.find(sr->getTTH(), matches);
		for(auto i = matches.begin(); i != matches.end(); ++i) {
			qi = *i;
			// Size compare to avoid popular spoof
			if(qi->getSize() == sr->getSize() && !qi->isSource(sr->getUser()) && qi->getBundle()) {
				bundle = qi->getBundle();
				if(qi->isFinished() && bundle->isSource(sr->getUser())) {
					//no need to match this bundle
					continue;
				}

				if((bundle->countOnlineUsers() < (size_t)SETTING(MAX_AUTO_MATCH_SOURCES))) {
					addSources = true;
				} 
			}
			break;
		}
	}

	if (addSources && bundle->isFileBundle()) {
		/* No reason to match anything with file bundles */
		WLock l(cs);
		try {	 
			wantConnection = addSource(qi, HintedUser(sr->getUser(), sr->getHubURL()), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
		} catch(...) {
			// Ignore...
		}
	} else if(addSources) {
		string path = bundle->getMatchPath(sr->getFile(), qi->getTarget(), sr->getUser()->isSet(User::NMDC));
		if (!path.empty()) {
			if (sr->getUser()->isSet(User::NMDC)) {
				//A NMDC directory bundle, just add the sources without matching
				QueueItemList ql;
				int newFiles = 0;
				{
					WLock l(cs);
					bundle->getDirQIs(path, ql);
					for (auto i = ql.begin(); i != ql.end(); ++i) {
						try {	 
							if (addSource(*i, HintedUser(sr->getUser(), sr->getHubURL()), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
								wantConnection = true;
							}
							newFiles++;
						} catch(...) {
							// Ignore...
						}
					}
				}
				if (SETTING(REPORT_ADDED_SOURCES) && newFiles > 0) {
					LogManager::getInstance()->message(Util::toString(ClientManager::getInstance()->getNicks(HintedUser(sr->getUser(), sr->getHubURL()))) + ": " + 
						str(boost::format(STRING(MATCH_SOURCE_ADDED)) % 
						newFiles % 
						bundle->getName().c_str()));
				}
			} else {
				//An ADC directory bundle, match recursive partial list
				try {
					addList(HintedUser(sr->getUser(), sr->getHubURL()), QueueItem::FLAG_MATCH_QUEUE | QueueItem::FLAG_RECURSIVE_LIST |(path.empty() ? 0 : QueueItem::FLAG_PARTIAL_LIST), path);
				} catch(...) { }
			}
		} else if (BOOLSETTING(ALLOW_MATCH_FULL_LIST)) {
			//failed, use full filelist
			try {
				addList(HintedUser(sr->getUser(), sr->getHubURL()), QueueItem::FLAG_MATCH_QUEUE);
			} catch(const Exception&) {
				// ...
			}
		}
	}

	if(wantConnection) {
		ConnectionManager::getInstance()->getDownloadConnection(HintedUser(sr->getUser(), sr->getHubURL()));
	}
}

// ClientManagerListener
void QueueManager::on(ClientManagerListener::UserConnected, const UserPtr& aUser) noexcept {
	bool hasDown = false;
	{
		RLock l(cs);
		auto j = userQueue.getBundleList().find(aUser);
		if(j != userQueue.getBundleList().end()) {
			for_each(j->second, [&](BundlePtr b) {
				QueueItemList items;
				b->getItems(aUser, items);
				for(auto s = items.begin(); s != items.end(); ++s) {
					QueueItemPtr qi = *s;
					fire(QueueManagerListener::StatusUpdated(), qi);
					if((b->getPriority() != QueueItem::PAUSED && qi->getPriority() != QueueItem::PAUSED) || qi->getPriority() == QueueItem::HIGHEST) {
						hasDown = true;
					}
				}
			});
		}
	}

	if(hasDown && aUser->isOnline()) { //even tho user just connected, check if he is still online and that we really have a user.
		// the user just came on, so there's only 1 possible hub, no need for a hint
		ConnectionManager::getInstance()->getDownloadConnection(HintedUser(aUser, Util::emptyString));
	}
}

void QueueManager::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser) noexcept {
	RLock l(cs);
	auto j = userQueue.getBundleList().find(aUser);
	if(j != userQueue.getBundleList().end()) {
		for(auto m = j->second.begin(); m != j->second.end(); ++m) {
			BundlePtr bundle = *m;
			QueueItemList items;
			bundle->getItems(aUser, items);
			for_each(items, [&](QueueItemPtr qi) { fire(QueueManagerListener::StatusUpdated(), qi); });
		}
	}
}

void QueueManager::on(DownloadManagerListener::BundleTick, const BundleList& tickBundles) {
	Bundle::PrioList qiPriorities;
	vector<pair<BundlePtr, Bundle::Priority>> bundlePriorities;

	{
		RLock l(cs);
		for (auto i = tickBundles.begin(); i != tickBundles.end(); ++i) {
			BundlePtr bundle = *i;
			if (bundle->isFinished()) {
				continue;
			}
			if (SETTING(DOWNLOAD_ORDER) == SettingsManager::ORDER_PROGRESS && bundle->getAutoPriority()) {
				Bundle::Priority p1 = bundle->getPriority();
				if(p1 != Bundle::PAUSED) {
					Bundle::Priority p2 = bundle->calculateProgressPriority();
					if(p1 != p2) {
						bundlePriorities.push_back(make_pair(bundle, p2));
					}
				}
			}
			for_each(bundle->getQueueItems(), [&](QueueItemPtr q) {
				if(q->isRunning()) {
					fire(QueueManagerListener::StatusUpdated(), q);
					if (q->getAutoPriority() && SETTING(DOWNLOAD_ORDER) == SettingsManager::ORDER_PROGRESS) {
						QueueItem::Priority p1 = q->getPriority();
						if(p1 != QueueItem::PAUSED) {
							QueueItem::Priority p2 = q->calculateAutoPriority();
							if(p1 != p2)
								qiPriorities.push_back(make_pair(q, (int8_t)p2));
						}
					}
				}
			});
		}
	}

	if (SETTING(DOWNLOAD_ORDER) == SettingsManager::ORDER_BALANCED && GET_TICK() > lastAutoPrio + 5000) {
		//LogManager::getInstance()->message("Calculate autoprio");
		calculateBundlePriorities(false);
		setLastAutoPrio(GET_TICK());
	}

	for_each(bundlePriorities, [&](pair<BundlePtr, Bundle::Priority> bp) {
		setBundlePriority(bp.first, bp.second, true);
	});

	for_each(qiPriorities, [&](pair<QueueItemPtr, int8_t> qp) {
		setQIPriority(qp.first, (QueueItem::Priority)qp.second);
	});
}

void QueueManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if((lastSave + 10000) < aTick) {
		saveQueue(false);
	}

	StringList updateTokens;
	{
		WLock l(cs);
		if (!bundleUpdates.empty()) {
			for (auto i = bundleUpdates.begin(); i != bundleUpdates.end();) {
				if (aTick > i->second + 1000) {
					updateTokens.push_back(i->first);
					bundleUpdates.erase(i);
					i = bundleUpdates.begin();
				} else {
					i++;
				}
			}
		}
	}

	for_each(updateTokens, [&](const string& t) { handleBundleUpdate(t); });
}

void QueueManager::calculateBundlePriorities(bool verbose) {
	{
		//prepare to set the prios
		multimap<int, BundlePtr> finalMap;
		int uniqueValues = 0;
		{
			RLock l(cs);
			bundleQueue.getAutoPrioMap(finalMap, uniqueValues);
		}
		int prioGroup = 1;
		if (uniqueValues <= 1) {
			if (verbose) {
				LogManager::getInstance()->message("Not enough bundles with unique points to perform the priotization!");
			}
			goto checkQIprios;
		} else if (uniqueValues > 2) {
			prioGroup = uniqueValues / 3;
		}

		if (verbose) {
			LogManager::getInstance()->message("Unique values: " + Util::toString(uniqueValues) + " prioGroup size: " + Util::toString(prioGroup));
		}

		//priority to set (4-2, high-low)
		int prio = 4;

		//counters for analyzing identical points
		int lastTime = 999;
		int prioSet=0;

		for (auto i = finalMap.begin(); i != finalMap.end(); ++i) {
			if (lastTime==i->first) {
				if (verbose) {
					LogManager::getInstance()->message("Bundle: " + i->second->getName() + " points: " + Util::toString(i->first) + " setting prio " + AirUtil::getPrioText(prio));
				}
				setBundlePriority(i->second, (Bundle::Priority)prio, true);
				//don't increase the prio if two bundles have identical points
				if (prioSet < prioGroup) {
					prioSet++;
				}
			} else {
				if (prioSet == prioGroup && prio != 2) {
					prio--;
					prioSet=0;
				} 
				if (verbose) {
					LogManager::getInstance()->message("Bundle: " + i->second->getName() + " points: " + Util::toString(i->first) + " setting prio " + AirUtil::getPrioText(prio));
				}
				setBundlePriority(i->second, (Bundle::Priority)prio, true);
				prioSet++;
				lastTime=i->first;
			}
		}
	}

checkQIprios:
	if (BOOLSETTING(QI_AUTOPRIO)) {
		{
			/*Bundle::PrioList qiPriorities;
			for (auto i = autoPrioMap.begin(); i != autoPrioMap.end(); ++i) {
				Bundle::SourceSpeedMapQI qiSpeedMap;
				Bundle::SourceSpeedMapQI qiSourceMap;
				{
					WLock l(cs);
					(*i).first->getQIBalanceMaps(qiSpeedMap, qiSourceMap);
				}
				(*i).first->calculateBalancedPriorities(qiPriorities, qiSpeedMap, qiSourceMap, verbose);
			}
			for(auto p = qiPriorities.begin(); p != qiPriorities.end(); p++) {
				setQIPriority((*p).first, (QueueItem::Priority)(*p).second, true);
			} */
		}
	}
}

bool QueueManager::dropSource(Download* d) {
	BundlePtr b = d->getBundle();
	size_t onlineUsers = 0;

	if(b->getRunning() >= SETTING(DISCONNECT_MIN_SOURCES)) {
		size_t iHighSpeed = SETTING(DISCONNECT_FILE_SPEED);
		{
			RLock l (cs);
			onlineUsers = b->countOnlineUsers();
		}

		if((iHighSpeed == 0 || b->getSpeed() > iHighSpeed * 1024) && onlineUsers > 2) {
			d->setFlag(Download::FLAG_SLOWUSER);

			if(d->getAverageSpeed() < SETTING(REMOVE_SPEED)*1024) {
				return true;
			} else {
				d->getUserConnection().disconnect();
			}
		}
	}

	return false;
}

bool QueueManager::handlePartialResult(const HintedUser& aUser, const TTHValue& tth, const QueueItem::PartialSource& partialSource, PartsInfo& outPartialInfo) {
	bool wantConnection = false;
	dcassert(outPartialInfo.empty());
	QueueItemPtr qi = NULL;

	{
		// Locate target QueueItem in download queue
		QueueItemList ql;
		{
			RLock l(cs);
			fileQueue.find(tth, ql);
		
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
		int64_t blockSize = HashManager::getInstance()->getBlockSize(qi->getTTH());
		if(blockSize == 0)
			blockSize = qi->getSize();

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

					userQueue.add(qi, aUser);
					dcassert(si != qi->getSources().end());
					fire(QueueManagerListener::SourcesUpdated(), qi);
				}
			}

			// Update source's parts info
			if(si->getPartialSource()) {
				si->getPartialSource()->setPartialInfo(partialSource.getPartialInfo());
			}
		}
	}
	
	// Connect to this user
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);

	return true;
}

BundlePtr QueueManager::findBundle(const TTHValue& tth) {
	RLock l(cs);
	QueueItemList ql;
	fileQueue.find(tth, ql);
	if (!ql.empty()) {
		return ql.front()->getBundle();
	}
	return NULL;
}

bool QueueManager::handlePartialSearch(const UserPtr& aUser, const TTHValue& tth, PartsInfo& _outPartsInfo, string& _bundle, bool& _reply, bool& _add) {
	//LogManager::getInstance()->message("QueueManager::handlePartialSearch");

	// Locate target QueueItem in download queue
	QueueItemList ql;
	{
		RLock l(cs);
		fileQueue.find(tth, ql);
	}

	if (ql.empty()) {
		//LogManager::getInstance()->message("QL EMPTY, QUIIIIIIIT");
		return false;
	}

	QueueItemPtr qi = ql.front();

	if (aUser) {
		BundlePtr b = qi->getBundle();
		if (b) {
			//LogManager::getInstance()->message("handlePartialSearch: QI AND BUNDLE FOUND");

			//no reports for duplicate or bad sources
			if (b->isFinishedNotified(aUser)) {
				//LogManager::getInstance()->message("handlePartialSearch: ALREADY NOTIFIED");
				return false;
			}
			_bundle = b->getToken();
			if (!b->getQueueItems().empty()) {
				_reply = true;
			}
			if (!b->getFinishedFiles().empty()) {
				_add = true;
			}
		} else {
			//LogManager::getInstance()->message("QI: NO BUNDLE OR FINISHEDTOKEN EXISTS");
		}
	}

	if(qi->getSize() < PARTIAL_SHARE_MIN_SIZE){
		return false;  
	}

	// don't share when file does not exist
	if(!Util::fileExists(qi->isFinished() ? qi->getTarget() : qi->getTempTarget()))
		return false;


	int64_t blockSize = HashManager::getInstance()->getBlockSize(qi->getTTH());
	if(blockSize == 0)
		blockSize = qi->getSize();

	RLock l(cs);
	qi->getPartialInfo(_outPartsInfo, blockSize);

	return !_outPartsInfo.empty();
}

tstring QueueManager::getDirPath(const string& aDir) {
	string dir;
	RLock l(cs);
	BundlePtr b = bundleQueue.findDir(aDir);
	if (b) {
		return Text::toT(b->getDirByRelease(aDir).first);
	}
	return Util::emptyStringT;
}

void QueueManager::getUnfinishedPaths(StringList& retBundles) {
	RLock l(cs);
	for_each(bundleQueue.getBundles(), [&](pair<string, BundlePtr> sp) {
		if (sp.second->isFileBundle() && !sp.second->isFinished()) {
			retBundles.push_back(sp.second->getTarget());
		}
	});
}

void QueueManager::getForbiddenPaths(StringList& retBundles, const StringPairList& paths) {
	BundleList hash;
	{
		RLock l(cs);
		for (auto i = bundleQueue.getBundles().begin(); i != bundleQueue.getBundles().end(); ++i) {
			BundlePtr b = i->second;
			//check the path
			if (find_if(paths.begin(), paths.end(), [&](StringPair sp) { return AirUtil::isParent(sp.second, b->getTarget()); }) == paths.end()) {
				continue;
			}

			if (!b->isFileBundle()) {
				if (b->isSet(Bundle::FLAG_SCAN_FAILED)) {
					if (ShareScannerManager::getInstance()->scanBundle(b)) {
						b->unsetFlag(Bundle::FLAG_SCAN_FAILED);
						hash.push_back(b);
					}
				} else if (b->isSet(Bundle::FLAG_HASH_FAILED)) {
					//schedule for hashing and handle the results elsewhere
					b->unsetFlag(Bundle::FLAG_HASH_FAILED);
					hash.push_back(b);
				} else if (b->isFinished() && !BOOLSETTING(ADD_FINISHED_INSTANTLY)) {
					continue;
				}
				retBundles.push_back(Text::toLower(b->getTarget()));
			}
		}
	}
	for_each(hash, [&](BundlePtr b) { hashBundle(b); });
	sort(retBundles.begin(), retBundles.end());
}

bool QueueManager::isChunkDownloaded(const TTHValue& tth, int64_t startPos, int64_t& bytes, string& target) {
	QueueItemList ql;

	RLock l(cs);
	fileQueue.find(tth, ql);

	if(ql.empty()) return false;

	QueueItemPtr qi = ql.front();

	target = qi->isFinished() ? qi->getTarget() : qi->getTempTarget();

	return qi->isChunkDownloaded(startPos, bytes);
}

bool QueueManager::addBundle(BundlePtr aBundle, bool loading) {
	if (aBundle->getQueueItems().empty()) {
		return false;
	}

	BundlePtr oldBundle = NULL;
	{
		RLock l(cs);
		oldBundle = bundleQueue.getMergeBundle(aBundle->getTarget());
	}

	if (oldBundle) {
		mergeBundle(oldBundle, aBundle);
		return false;
	} else if (!aBundle->isFileBundle()) {
		//check that there are no file bundles inside the bundle that will be created and merge them in that case
		mergeFileBundles(aBundle);
	}

	{
		WLock l(cs);
		bundleQueue.add(aBundle);
		fire(QueueManagerListener::BundleAdded(), aBundle);
		aBundle->updateSearchMode();
	}

	if (loading)
		return true;

	if (BOOLSETTING(AUTO_SEARCH) && aBundle->getPriority() != Bundle::PAUSED) {
		searchBundle(aBundle, true, false);
	} else {
		LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_CREATED)) % 
			aBundle->getName().c_str() % 
			aBundle->getQueueItems().size()) + 
			" (" + CSTRING(SETTINGS_SHARE_SIZE) + " " + Util::formatBytes(aBundle->getSize()).c_str() + ")");
	}

	connectBundleSources(aBundle);
	return true;
}

void QueueManager::connectBundleSources(BundlePtr aBundle) {
	if (aBundle->getPriority() == Bundle::PAUSED)
		return;

	HintedUserList x;
	{
		RLock l(cs);
		aBundle->getSources(x);
	}

	for_each(x, [&](const HintedUser u) { 
		if(u.user && u.user->isOnline())
			ConnectionManager::getInstance()->getDownloadConnection(u, false); 
	});
}

void QueueManager::readdBundle(BundlePtr aBundle) {
	aBundle->unsetFlag(Bundle::FLAG_HASH_FAILED);
	aBundle->unsetFlag(Bundle::FLAG_SCAN_FAILED);

	{
		WLock l(cs);
		//check that the finished files still exist
		for(auto i = aBundle->getFinishedFiles().begin(); i != aBundle->getFinishedFiles().end();) {
			QueueItemPtr q = *i;
			if (!Util::fileExists(q->getTarget())) {
				bundleQueue.removeFinishedItem(q);
				fileQueue.remove(q);
			} else {
				++i;
			}
		}
		fire(QueueManagerListener::BundleAdded(), aBundle);
		bundleQueue.addSearchPrio(aBundle);
	}
}

void QueueManager::mergeFileBundles(BundlePtr targetBundle) {
	BundleList bl;
	{
		RLock l(cs);
		bundleQueue.getSubBundles(targetBundle->getTarget(), bl);
	}
	for_each(bl, [&](BundlePtr sourceBundle) { fire(QueueManagerListener::BundleMoved(), sourceBundle); mergeBundle(targetBundle, sourceBundle); });
}

void QueueManager::mergeBundle(BundlePtr targetBundle, BundlePtr sourceBundle, bool first /*true*/) {
	if (sourceBundle->isFileBundle()) {
		dcassert(sourceBundle->isFileBundle());
		{
			WLock l(cs);
			moveBundleItem(sourceBundle->getQueueItems().front(), targetBundle, false);
		}

		removeBundle(sourceBundle, false, false, true);

		targetBundle->setDirty(true);

		if (first) {
			targetBundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
			addBundleUpdate(targetBundle->getToken());

			LogManager::getInstance()->message(str(boost::format(STRING(FILEBUNDLE_MERGED)) % 
				sourceBundle->getName().c_str() % 
				targetBundle->getName().c_str()));
		}
		return;
	}

	//finished bundle but failed hashing/scanning?
	bool finished = targetBundle->isFinished();

	HintedUserList x;
	//new bundle? we need to connect to sources then
	if (first && sourceBundle->isSet(Bundle::FLAG_NEW)) {
		for_each(sourceBundle->getSources(), [&](const Bundle::SourceTuple st) { x.push_back(get<Bundle::SOURCE_USER>(st)); });
	}

	int added = 0;
	{
		QueueItemList ql;
		{
			RLock l(cs);
			if (!finished) {
				fire(QueueManagerListener::BundleMoved(), targetBundle);
			}
			ql = sourceBundle->getQueueItems();
		}
		added = (int)ql.size();
		moveBundleItems(ql, targetBundle, false);
	}

	if (!first) {
		//we need to move the finished items when merging subdirs (in other cases they don't need to be handled here)
		WLock l(cs);
		for (auto j = sourceBundle->getFinishedFiles().begin(); j != sourceBundle->getFinishedFiles().end();) {
			bundleQueue.addFinishedItem(*j, targetBundle);
			bundleQueue.removeFinishedItem(*j);
		}
		return;
	}

	//the target bundle is a sub directory of the source bundle?
	bool changeTarget = AirUtil::isSub(targetBundle->getTarget(), sourceBundle->getTarget());
	if (changeTarget) {
		added = changeBundleTarget(targetBundle, sourceBundle->getTarget());
		//LogManager::getInstance()->message("MERGE CHANGE TARGET");
	} else if (finished) {
		readdBundle(targetBundle);
	} else {
		targetBundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
		addBundleUpdate(targetBundle->getToken());
		targetBundle->setDirty(true);
	}

	if (!finished) {
		RLock l(cs);
		fire(QueueManagerListener::BundleAdded(), targetBundle);
	}

	for_each(x, [&](const HintedUser u) {
		if(u.user && u.user->isOnline() && (targetBundle->getPriority() != Bundle::PAUSED))
			ConnectionManager::getInstance()->getDownloadConnection(u, false); 
	});

	/* Report */
	string tmp;
	if (changeTarget) {
		string tmp = str(boost::format(STRING(BUNDLE_CREATED)) % 
			targetBundle->getName().c_str() % 
			targetBundle->getQueueItems().size()) + 
			" (" + STRING(SETTINGS_SHARE_SIZE) + " " + Util::formatBytes(targetBundle->getSize()).c_str() + ")";

		if (added > 0)
			tmp += str(boost::format(", " + STRING(EXISTING_BUNDLES_MERGED)) % (added+1));

		LogManager::getInstance()->message(tmp);
	} else if (targetBundle->getTarget() == sourceBundle->getTarget()) {
		LogManager::getInstance()->message(str(boost::format(STRING(X_BUNDLE_ITEMS_ADDED)) % 
			added % 
			targetBundle->getName().c_str()));
	} else {
		LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_MERGED)) % 
			sourceBundle->getName().c_str() % 
			targetBundle->getName().c_str() % 
			added));
	}
}

int QueueManager::changeBundleTarget(BundlePtr aBundle, const string& newTarget) {
	/* In this case we also need check if there are directory bundles inside the subdirectories */
	BundleList mBundles;
	{
		WLock l(cs);
		bundleQueue.move(aBundle, newTarget); //set the new target
		bundleQueue.getSubBundles(newTarget, mBundles);
	}
	for_each(mBundles, [&] (BundlePtr b) { mergeBundle(aBundle, b, false); });

	aBundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
	aBundle->setFlag(Bundle::FLAG_UPDATE_NAME);
	addBundleUpdate(aBundle->getToken());
	aBundle->setDirty(true);

	return (int)mBundles.size();
}

int QueueManager::getDirItemCount(const BundlePtr aBundle, const string& aDir) noexcept { 
	RLock l(cs);
	QueueItemList ql;
	aBundle->getDirQIs(aDir, ql);
	return (int)ql.size();
}

uint8_t QueueManager::isDirQueued(const string& aDir) {
	RLock l(cs);
	BundlePtr b = bundleQueue.findDir(aDir);
	if (b) {
		auto s = b->getDirByRelease(aDir);
		if (s.second.first == 0) //no queued items
			return 2;
		else
			return 1;
	}
	return 0;
}



int QueueManager::getBundleItemCount(const BundlePtr aBundle) noexcept {
	RLock l(cs); 
	return aBundle->getQueueItems().size(); 
}

int QueueManager::getFinishedItemCount(const BundlePtr aBundle) noexcept { 
	RLock l(cs); 
	return (int)aBundle->getFinishedFiles().size(); 
}

void QueueManager::setBundlePriorities(const string& aSource, const BundleList& sourceBundles, Bundle::Priority p, bool autoPrio) {
	if (sourceBundles.empty()) {
		return;
	}

	BundlePtr bundle;

	if (sourceBundles.size() == 1 && AirUtil::isSub(aSource, sourceBundles.front()->getTarget())) {
		//we aren't removing the whole bundle
		bundle = sourceBundles.front();
		QueueItemList ql;
		{
			RLock l(cs);
			bundle->getDirQIs(aSource, ql);
		}

		for (auto i = ql.begin(); i != ql.end(); ++i) {
			if (autoPrio) {
				setQIAutoPriority((*i)->getTarget(), (*i)->getAutoPriority());
			} else {
				setQIPriority((*i), (QueueItem::Priority)p);
			}
		}
	} else {
		for_each(sourceBundles, [&](BundlePtr b) {
			if (autoPrio) {
				setBundleAutoPriority(b->getToken());
			} else {
				setBundlePriority(b, (Bundle::Priority)p);
			}
		});
	}
}

void QueueManager::removeDir(const string aSource, const BundleList& sourceBundles, bool removeFinished) {
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
			for (auto i = bundle->getFinishedFiles().begin(); i != bundle->getFinishedFiles().end();) {
				QueueItemPtr qi = *i;
				if (AirUtil::isSub(qi->getTarget(), aSource)) {
					UploadManager::getInstance()->abortUpload((*i)->getTarget());
					if (removeFinished) {
						File::deleteFile(qi->getTarget());
					}
					fileQueue.remove(qi);
					bundleQueue.removeFinishedItem(qi);
				} else {
					i++;
				}
			}
		}

		AirUtil::removeIfEmpty(aSource);
		for_each(ql, [&] (QueueItemPtr qi) { removeQI(qi, false); });
	} else {
		for_each(sourceBundles, [&] (BundlePtr bundle) { removeBundle(bundle, false, removeFinished); });
	}
}

void QueueManager::moveDir(const string aSource, const string& aTarget, const BundleList& sourceBundles, bool moveFinished) {
	if (sourceBundles.empty()) {
		return;
	}

	string target = Util::validateFileName(aTarget);
	for_each(sourceBundles, [&](BundlePtr sourceBundle) {
		if (!sourceBundle->isFileBundle()) {
			if (AirUtil::isParent(aSource, sourceBundle->getTarget())) {
				//we are moving the root bundle dir or some of it's parents
				moveBundle(aSource, target, sourceBundle, moveFinished);
			} else {
				//we are moving a subfolder, get the list of queueitems we need to move
				splitBundle(aSource, target, sourceBundle, moveFinished);
			}
		} else {
			//move queue items
			moveFileBundle(sourceBundle, target, aSource);
		}
	});
}

void QueueManager::moveBundle(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished) {

	string sourceBundleTarget = sourceBundle->getTarget();
	bool hasMergeBundle = false;
	BundlePtr newBundle = NULL;

	//handle finished items
	{
		WLock l(cs);
		//can we merge this with an existing bundle?
		newBundle = bundleQueue.getMergeBundle(AirUtil::convertMovePath(sourceBundleTarget,aSource, aTarget));	
		if (newBundle) {
			hasMergeBundle = true;
		} else {
			newBundle = sourceBundle;
		}

		//handle finished items
		for (auto i = sourceBundle->getFinishedFiles().begin(); i != sourceBundle->getFinishedFiles().end();) {
			QueueItemPtr qi = *i;
			if (moveFinished) {
				UploadManager::getInstance()->abortUpload(qi->getTarget());
				string targetPath = AirUtil::convertMovePath(qi->getTarget(), aSource, aTarget);
				if (!fileQueue.find(targetPath)) {
					if(!Util::fileExists(targetPath)) {
						moveFile(qi->getTarget(), targetPath, newBundle);
						fileQueue.move(qi, targetPath);
						if (hasMergeBundle) {
							bundleQueue.removeFinishedItem(qi);
							bundleQueue.addFinishedItem(qi, newBundle);
						} else {
							//keep in the current bundle
							i++;
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

		AirUtil::removeIfEmpty(aSource);
	}

	//convert the QIs
	QueueItemList ql;
	{
		RLock l(cs);
		fire(QueueManagerListener::BundleMoved(), sourceBundle);
		ql = sourceBundle->getQueueItems();
	}

	for (auto i = ql.begin(); i != ql.end();) {
		QueueItemPtr qi = *i;
		if (!move(qi, AirUtil::convertMovePath(qi->getTarget(), aSource, aTarget))) {
			ql.erase(i);
		} else {
			++i;
		}
	}

	if (ql.empty()) {
		//may happen if all queueitems are being merged with existing ones
		return;
	}

	if (hasMergeBundle) {
		mergeBundle(newBundle, sourceBundle);
	} else {
		//nothing to merge to, move the old bundle
		int merged = changeBundleTarget(sourceBundle, AirUtil::convertMovePath(sourceBundleTarget, aSource, aTarget));

		{
			RLock l(cs);
			fire(QueueManagerListener::BundleAdded(), sourceBundle);
		}

		string tmp = str(boost::format(STRING(BUNDLE_MOVED)) % 
			sourceBundle->getName().c_str() % 
			sourceBundle->getTarget().c_str());
		if (merged > 0)
			tmp += str(boost::format(" (" + STRING(EXISTING_BUNDLES_MERGED) + ")") % merged);

		LogManager::getInstance()->message(tmp);
	}
}

void QueueManager::splitBundle(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished) {
	//first pick the items that we need to move
	QueueItemList ql;
	BundlePtr newBundle = NULL;
	{
		RLock l(cs);
		sourceBundle->getDirQIs(aSource, ql);
		newBundle = bundleQueue.getMergeBundle(aTarget);
	}

	//create a temp bundle for split items
	BundlePtr tempBundle = BundlePtr(new Bundle(aTarget, GET_TIME(), sourceBundle->getPriority(), sourceBundle->getDirDate()));

	//can we merge the split folder?
	bool hasMergeBundle = newBundle;

	{
		WLock l(cs);

		//handle finished items
		for (auto i = sourceBundle->getFinishedFiles().begin(); i != sourceBundle->getFinishedFiles().end();) {
			QueueItemPtr qi = *i;
			if (AirUtil::isSub(qi->getTarget(), aSource)) {
				if (moveFinished) {
					UploadManager::getInstance()->abortUpload(qi->getTarget());
					string targetPath = AirUtil::convertMovePath(qi->getTarget(), aSource, aTarget);
					if (!fileQueue.find(targetPath)) {
						if(!Util::fileExists(targetPath)) {
							moveFile(qi->getTarget(), targetPath, newBundle);
							fileQueue.move(qi, targetPath);
							if (newBundle == sourceBundle) {
								i++;
								continue;
							} else if (hasMergeBundle) {
								bundleQueue.addFinishedItem(qi, newBundle);
							} else {
								bundleQueue.addFinishedItem(qi, tempBundle);
							}
							bundleQueue.removeFinishedItem(qi);
							continue;
						} else {
							/* TODO: add for recheck */
						}
					}
				}
				fileQueue.remove(qi);
				bundleQueue.removeFinishedItem(qi);
			} else {
				i++;
			}
		}

		AirUtil::removeIfEmpty(aSource);

		fire(QueueManagerListener::BundleMoved(), sourceBundle);
	}

	//convert the QIs
	for (auto i = ql.begin(); i != ql.end();) {
		QueueItemPtr qi = *i;
		if (!move(qi, AirUtil::convertMovePath(qi->getTarget(), aSource, aTarget))) {
			ql.erase(i);
		} else {
			i++;
		}
	}

	if (newBundle != sourceBundle) {
		moveBundleItems(ql, tempBundle, false);
	}

	{
		RLock l(cs);
		if (!sourceBundle->getQueueItems().empty()) {
			fire(QueueManagerListener::BundleAdded(), sourceBundle);
		}
	}

	//merge or add the temp bundle

	if (newBundle == sourceBundle) {
		return;
	} else if (hasMergeBundle) {
		//merge the temp bundle
		mergeBundle(newBundle, tempBundle);
	} else {
		addBundle(tempBundle);
	}
}

void QueueManager::moveFileBundle(BundlePtr aBundle, const string& aTarget, const string& aSource) noexcept {
	QueueItemPtr qi = NULL;
	BundlePtr targetBundle = NULL;
	{
		RLock l(cs);
		qi = aBundle->getQueueItems().front();
		fire(QueueManagerListener::BundleMoved(), aBundle);
		targetBundle = bundleQueue.getMergeBundle(qi->getTarget());
	}

	if (!move(qi, aSource.empty() ? aTarget : (AirUtil::convertMovePath(qi->getTarget(), aSource, aTarget)))) {
		return;
	}

	if (targetBundle) {
		mergeBundle(targetBundle, aBundle);
		fire(QueueManagerListener::Added(), qi);
	} else {
		LogManager::getInstance()->message(str(boost::format(STRING(FILEBUNDLE_MOVED)) % aBundle->getName().c_str() % aBundle->getTarget().c_str()));

		{
			RLock l(cs);
			aBundle->setTarget(qi->getTarget());
			fire(QueueManagerListener::BundleAdded(), aBundle);
		}

		/* update the bundle path in transferview */
		fire(QueueManagerListener::BundleTarget(), aBundle);

		aBundle->setDirty(true);
	}
}

bool QueueManager::move(QueueItemPtr qs, const string& aTarget) noexcept {
	string target = Util::validateFileName(aTarget);

	if(qs->getTarget() == target) {
		//LogManager::getInstance()->message("MOVE FILE, TARGET SAME");
		return false;
	}
	dcassert(qs->getBundle());

	// Let's see if the target exists...then things get complicated...
	QueueItemPtr qt = NULL;
	{
		RLock l(cs);
		qt = fileQueue.find(target);
	}

	if(qt == NULL) {
		//Does the file exist already on the disk?
		if(Util::fileExists(target)) {
			removeQI(qs, true);
			return false;
		}
		// Good, update the target and move in the queue...
		{
			WLock l(cs);
			if(qs->isRunning()) {
				DownloadManager::getInstance()->setTarget(qs->getTarget(), aTarget);
			}
			fileQueue.move(qs, target);
		}
		return true;
	} else {
		// Don't move to target of different size
		if(qs->getSize() != qt->getSize() || qs->getTTH() != qt->getTTH())
			return false;

		{
			WLock l(cs);
			if (qt->isFinished()) {
				dcassert(qt->getBundle());
				if (!Util::fileExists(target) && qt->getBundle()) {
					fileQueue.remove(qt);
					bundleQueue.removeFinishedItem(qt);
					fileQueue.move(qs, target);
					return true;
				}
			} else {
				for_each(qs->getSources(), [&](QueueItem::Source& s) {
					try {
						addSource(qt, s.getUser(), QueueItem::Source::FLAG_MASK);
					} catch(const Exception&) {
						//..
					}
				});
			}
		}
		removeQI(qs, true);
	}
	return false;
}

void QueueManager::move(const StringPairList& sourceTargetList) noexcept {
	QueueItemList ql;
	bool movedFired = false;
	for_each(sourceTargetList, [&](StringPair sp) {
		string source = sp.first;
		QueueItemPtr qs = NULL;
		{
			RLock l(cs);
			qs = fileQueue.find(source);
		}
		if(qs) {
			BundlePtr b = qs->getBundle();
			dcassert(b);
			if (b) {
				if (b->isFileBundle()) {
					//the path has been converted already
					moveFileBundle(qs->getBundle(), sp.second, Util::emptyString);
				} else {
					if (!movedFired) {
						RLock l(cs);
						fire(QueueManagerListener::BundleMoved(), qs->getBundle());
						movedFired = true;
					}
					if (this->move(qs, sp.second)) {
						ql.push_back(qs);
					}
				}
			}
		}
	});

	if (!ql.empty()) {
		//all files should be part of the same directory bundle
		QueueItemPtr qi = ql.front();
		BundlePtr sourceBundle = qi->getBundle();
		BundlePtr targetBundle = NULL;
		{
			WLock l(cs);
			targetBundle = bundleQueue.getMergeBundle(qi->getTarget());
		}
		if (targetBundle) {
			if (targetBundle == sourceBundle) {
				{
					RLock l(cs);
					if (!sourceBundle->getQueueItems().empty())
						fire(QueueManagerListener::BundleAdded(), sourceBundle);
				}
				return;
			}
			bool finished = targetBundle->isFinished();
			moveBundleItems(ql, targetBundle, !finished);
			if (finished) {
				readdBundle(targetBundle);
			}
		} else {
			//split into file bundles
			BundleList newBundles;
			{
				WLock l(cs);
				for_each(ql, [&](QueueItemPtr qi) {
					bundleQueue.removeBundleItem(qi, false);
					userQueue.removeQI(qi, false); //we definately don't want to remove downloads because the QI will stay the same
					targetBundle = BundlePtr(new Bundle(qi));
					targetBundle->setPriority(sourceBundle->getPriority());
					targetBundle->setAutoPriority(sourceBundle->getAutoPriority());

					if (qi->isRunning()) {
						//now we need to move the download(s) to correct bundle... the target has been changed earlier, if needed
						DownloadManager::getInstance()->changeBundle(sourceBundle, targetBundle, qi->getTarget());
					}

					/* ADDING */
					qi->setPriority((QueueItem::Priority)sourceBundle->getPriority());
					qi->setAutoPriority(sourceBundle->getAutoPriority());
					userQueue.add(qi);
					newBundles.push_back(targetBundle);
				});
			}
			for_each(newBundles, [&](BundlePtr b) { addBundle(b, false); });
		}

		bool emptyBundle = false;
		{
			RLock l(cs);
			emptyBundle = sourceBundle->getQueueItems().empty();
			if (!emptyBundle)
				fire(QueueManagerListener::BundleAdded(), sourceBundle);
		}

		if (emptyBundle)
			removeBundle(sourceBundle, false, false, true);
	}
}

void QueueManager::moveBundleItems(const QueueItemList& ql, BundlePtr targetBundle, bool fireAdded) {
	/* NO FILEBUNDLES SHOULD COME HERE */
	BundlePtr sourceBundle = NULL;
	if (!ql.empty() && !ql.front()->getBundle()->isFileBundle()) {
		sourceBundle = ql.front()->getBundle();
	} else {
		return;
	}

	{
		WLock l(cs);
		for_each(ql, [&](QueueItemPtr qi) { moveBundleItem(qi, targetBundle, fireAdded); });
	}	

	if (sourceBundle->getQueueItems().empty()) {
		removeBundle(sourceBundle, false, false, true);
	} else {
		targetBundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
		addBundleUpdate(targetBundle->getToken());
		targetBundle->setDirty(true);
		if (fireAdded) {
			sourceBundle->setFlag(Bundle::FLAG_UPDATE_SIZE);
			addBundleUpdate(sourceBundle->getToken());
			sourceBundle->setDirty(true);
		}
	}
}

void QueueManager::moveBundleItem(QueueItemPtr qi, BundlePtr targetBundle, bool fireAdded) {
	BundlePtr sourceBundle = qi->getBundle();
	bundleQueue.removeBundleItem(qi, false);
	userQueue.removeQI(qi, false); //we definately don't want to remove downloads because the QI will stay the same

	if (qi->isRunning()) {
		//now we need to move the download(s) to correct bundle... the target has been changed earlier, if needed
		DownloadManager::getInstance()->changeBundle(sourceBundle, targetBundle, qi->getTarget());
	}

	/* ADDING */
	bundleQueue.addBundleItem(qi, targetBundle);
	userQueue.add(qi);
	if (fireAdded) {
		fire(QueueManagerListener::Added(), qi);
	}
}

void QueueManager::addBundleUpdate(const string& bundleToken) {
	//LogManager::getInstance()->message("QueueManager::addBundleUpdate");
	WLock l(cs);
	auto i = find_if(bundleUpdates.begin(), bundleUpdates.end(), CompareFirst<string, uint64_t>(bundleToken));
	if (i != bundleUpdates.end()) {
		i->second = GET_TICK();
		return;
	}

	bundleUpdates.push_back(make_pair(bundleToken, GET_TICK()));
}

void QueueManager::handleBundleUpdate(const string& bundleToken) {
	//LogManager::getInstance()->message("QueueManager::sendBundleUpdate");
	BundlePtr bundle = NULL;
	{
		RLock l(cs);
		bundle = bundleQueue.find(bundleToken);
	}

	if (bundle) {
		if (bundle->isSet(Bundle::FLAG_UPDATE_SIZE) || bundle->isSet(Bundle::FLAG_UPDATE_NAME)) {
			if (bundle->isSet(Bundle::FLAG_UPDATE_SIZE)) {
				fire(QueueManagerListener::BundleSize(), bundle);
			} 
			if (bundle->isSet(Bundle::FLAG_UPDATE_NAME)) {
				fire(QueueManagerListener::BundleTarget(), bundle);
			}
			DownloadManager::getInstance()->sendSizeNameUpdate(bundle);
		}
	}
}

void QueueManager::removeBundleItem(QueueItemPtr qi, bool finished, bool moved /*false*/) {
	BundlePtr bundle = qi->getBundle();
	if (!bundle) {
		return;
	}
	bool emptyBundle = false;
	HintedUserList notified;

	{
		WLock l(cs);
		bundleQueue.removeBundleItem(qi, finished);
		if (finished) {
			for (auto s = qi->getBundle()->getFinishedNotifications().begin(); s != qi->getBundle()->getFinishedNotifications().end(); ++s) {
				if (!qi->isSource(s->first.user)) {
					notified.push_back(s->first);
				}
			}
		}

		emptyBundle = bundle->getQueueItems().empty();
	}

	//notify users if finished
	for_each(notified, [&](HintedUser u) { sendPBD(u, qi->getTTH(), bundle->getToken()); });

	if (emptyBundle) {
		removeBundle(bundle, finished, false, moved);
	} else {
		bundle->setDirty(true);
	}
}

void QueueManager::removeBundle(BundlePtr aBundle, bool finished, bool removeFinished, bool moved /*false*/) {
	if (aBundle->isSet(Bundle::FLAG_NEW)) {
		return;
	}

	if (finished) {
		aBundle->finishBundle();
		fire(QueueManagerListener::BundleFinished(), aBundle);
	} else if (!moved) {
		//LogManager::getInstance()->message("The Bundle " + aBundle->getName() + " has been removed");
		DownloadManager::getInstance()->disconnectBundle(aBundle);
		{
			WLock l(cs);
			for_each(aBundle->getFinishedFiles(), [&](QueueItemPtr q) {
				UploadManager::getInstance()->abortUpload(q->getTarget());
				fileQueue.remove(q);
				if (removeFinished) {
					File::deleteFile(q->getTarget());
				}
			});

			fire(QueueManagerListener::BundleRemoved(), aBundle);

			for_each(aBundle->getQueueItems(), [&](QueueItemPtr qi) {
				UploadManager::getInstance()->abortUpload(qi->getTarget());

				if(!qi->isRunning() && !qi->getTempTarget().empty() && qi->getTempTarget() != qi->getTarget()) {
					File::deleteFile(qi->getTempTarget());
				}

				if(!qi->isFinished()) {
					userQueue.removeQI(qi, true, true);
				}
				fileQueue.remove(qi);
			});
		}

		if (!aBundle->isFileBundle())
			AirUtil::removeIfEmpty(aBundle->getTarget());
	}

	{
		WLock l(cs);
		if (!finished) {
			bundleQueue.remove(aBundle);
		}
		bundleQueue.removeSearchPrio(aBundle);

		try {
			File::deleteFile(aBundle->getBundleFile() + ".bak");
			File::deleteFile(aBundle->getBundleFile());
		} catch(const FileException& /*e1*/) {
			//..
		}
	}
}

MemoryInputStream* QueueManager::generateTTHList(const string& bundleToken, bool isInSharingHub) {
	if(!isInSharingHub)
		return NULL;

	string tths;
	StringOutputStream tthList(tths);
	{
		RLock l(cs);
		BundlePtr bundle = bundleQueue.find(bundleToken);
		if (bundle) {
			//write finished items
			bundle->getTTHList(tthList);
		}
	}

	if (tths.empty()) {
		return NULL;
	} else {
		return new MemoryInputStream(tths);
	}
}

void QueueManager::addBundleTTHList(const HintedUser& aUser, const string& remoteBundle, const TTHValue& tth) {
	//LogManager::getInstance()->message("ADD TTHLIST");
	if (findBundle(tth)) {
		addList(aUser, QueueItem::FLAG_TTHLIST_BUNDLE | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_MATCH_QUEUE, remoteBundle);
	}
}

bool QueueManager::checkPBDReply(HintedUser& aUser, const TTHValue& aTTH, string& _bundleToken, bool& _notify, bool& _add, const string& remoteBundle) {
	BundlePtr bundle = findBundle(aTTH);
	if (bundle) {
		WLock l(cs);
		//LogManager::getInstance()->message("checkPBDReply: BUNDLE FOUND");
		_bundleToken = bundle->getToken();

		if (!bundle->getFinishedFiles().empty()) {
			_add=true;
		}

		if (!bundle->getQueueItems().empty()) {
			bundle->addFinishedNotify(aUser, remoteBundle);
			_notify = true;
		}
		return true;
	}
	//LogManager::getInstance()->message("checkPBDReply: CHECKNOTIFY FAIL");
	return false;
}

void QueueManager::addFinishedNotify(HintedUser& aUser, const TTHValue& aTTH, const string& remoteBundle) {
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

void QueueManager::removeBundleNotify(const UserPtr& aUser, const string& bundleToken) {
	BundlePtr bundle = bundleQueue.find(bundleToken);
	if (bundle) {
		WLock l(cs);
		//LogManager::getInstance()->message("QueueManager::removeBundleNotify: bundle found");
		bundle->removeFinishedNotify(aUser);
	} else {
		//LogManager::getInstance()->message("QueueManager::removeBundleNotify: bundle NOT found");
	}
}

void QueueManager::sendPBD(HintedUser& aUser, const TTHValue& tth, const string& bundleToken) {
	
	AdcCommand cmd(AdcCommand::CMD_PBD, AdcCommand::TYPE_UDP);

	cmd.addParam("UP1");
	cmd.addParam("HI", aUser.hint);
	cmd.addParam("TH", tth.toBase32());
	//LogManager::getInstance()->message("SENDPBD UPDATE: " + cmd.toString());
	ClientManager::getInstance()->send(cmd, aUser.user->getCID());
}

void QueueManager::updatePBD(const HintedUser& aUser, const TTHValue& aTTH) {
	//LogManager::getInstance()->message("UPDATEPBD");
	bool wantConnection = false;
	QueueItemList qiList;
	{
		WLock l(cs);
		fileQueue.find(aTTH, qiList);
		for_each(qiList, [&](QueueItemPtr q) {
			try {
				//LogManager::getInstance()->message("ADDSOURCE");
				if (addSource(q, aUser, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
					wantConnection = true;
				}
			} catch(...) {
				// Ignore...
			}
		});
	}
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

void QueueManager::searchBundle(BundlePtr aBundle, bool newBundle, bool manual) {
	StringPairList searches;
	int64_t nextSearch = 0;
	{
		RLock l(cs);
		aBundle->getSearchItems(searches, manual);
		if (!manual)
			nextSearch = (bundleQueue.recalculateSearchTimes(aBundle, false) - GET_TICK()) / (60*1000);
	}

	if (searches.size() <= 5) {
		aBundle->setSimpleMatching(true);
		for_each(searches, [&](StringPair& sp) {
			//LogManager::getInstance()->message("QueueManager::searchBundle, searchString: " + i->second);
			SearchManager::getInstance()->search(sp.second, 0, SearchManager::TYPE_TTH, SearchManager::SIZE_DONTCARE, "auto", Search::ALT_AUTO);
		});
	} else {
		//use an alternative matching, choose random items to search for
		aBundle->setSimpleMatching(false);
		int k = 0;
		while (k < 5) {
			auto pos = searches.begin();
			auto rand = Util::rand(searches.size());
			advance(pos, rand);
			//LogManager::getInstance()->message("QueueManager::searchBundle, ALT searchString: " + pos->second);
			SearchManager::getInstance()->search(pos->second, 0, SearchManager::TYPE_TTH, SearchManager::SIZE_DONTCARE, "auto", Search::ALT_AUTO);
			searches.erase(pos);
			k++;
		}
	}

	int searchCount = (int)searches.size() <= 4 ? (int)searches.size() : 4;
	if (newBundle) {
		LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_CREATED_ALT)  + " " + (!aBundle->isRecent() ? STRING(NEXT_SEARCH_IN) : STRING(NEXT_RECENT_SEARCH_IN))) % 
			aBundle->getName().c_str() % 
			aBundle->getQueueItems().size() % 
			Util::formatBytes(aBundle->getSize()).c_str() % 
			searchCount %
			nextSearch));
		return;
	}

	if(BOOLSETTING(REPORT_ALTERNATES) && !manual) {
		//LogManager::getInstance()->message(STRING(ALTERNATES_SEND) + " " + Util::getFileName(qi->getTargetFileName()));
		if (aBundle->getSimpleMatching()) {
			LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_ALT_SEARCH_RECENT) + " " + (!aBundle->isRecent() ? STRING(NEXT_SEARCH_IN) : STRING(NEXT_RECENT_SEARCH_IN))) % 
					aBundle->getName().c_str() % 
					searchCount % 
					nextSearch));
		} else {
			if (!aBundle->isRecent()) {
				LogManager::getInstance()->message(STRING(ALTERNATES_SEND) + " " + aBundle->getName() + ", not using partial lists, next search in " + Util::toString(nextSearch) + " minutes");
			} else {
				LogManager::getInstance()->message(STRING(ALTERNATES_SEND) + " " + aBundle->getName() + ", not using partial lists, next recent search in " + Util::toString(nextSearch) + " minutes");
			}
		}
	} else if (manual) {
		LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_ALT_SEARCH)) % aBundle->getName().c_str() % searchCount));
	}
}

} // namespace dcpp

/**
 * @file
 * $Id: QueueManager.cpp 568 2011-07-24 18:28:43Z bigmuscle $
 */
