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
#include "ConnectionManager.h"
#include "DownloadManager.h"

#include "ResourceManager.h"
#include "QueueManager.h"
#include "HashManager.h"
#include "Download.h"
#include "LogManager.h"
#include "User.h"
#include "UserConnection.h"


#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>


#include "UploadManager.h"
#include "FavoriteManager.h"

#include <limits>
#include <cmath>


// some strange mac definition
#ifdef ff
#undef ff
#endif

namespace dcpp {

using boost::range::for_each;

static const string DOWNLOAD_AREA = "Downloads";

DownloadManager::DownloadManager() {
	TimerManager::getInstance()->addListener(this);
}

DownloadManager::~DownloadManager() {
	TimerManager::getInstance()->removeListener(this);
	while(true) {
		{
			Lock l(cs);
			if(downloads.empty())
				break;
		}
		Thread::sleep(100);
	}
}

void DownloadManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	vector<pair<string, UserPtr> > dropTargets;
	vector<pair<CID, AdcCommand>> UBNList;
	StringList targets;
	BundleList bundleTicks;
	boost::unordered_map<UserPtr, int64_t> userSpeedMap;

	{
		Lock l(cs);
		for (auto i = runningBundles.begin(); i != runningBundles.end(); ++i) {
			BundlePtr bundle = i->second;
			if (bundle->onDownloadTick(UBNList)) {
				bundleTicks.push_back(bundle);
			}
		}

		DownloadList tickList;
		// Tick each ongoing download
		for(auto i = downloads.begin(); i != downloads.end(); ++i) {
			Download* d = *i;
			double speed = d->getAverageSpeed();

			if(d->getPos() > 0) {
				userSpeedMap[d->getUser()] += speed;
				tickList.push_back(d);
				d->tick();
			}

			if (d->getBundle() && d->getBundle()->isSet(Bundle::FLAG_AUTODROP) && d->getStart() > 0)
			{
				if (d->getTigerTree().getFileSize() > (SETTING(DISCONNECT_FILESIZE) * 1048576))
				{
					if((speed < SETTING(DISCONNECT_SPEED) * 1024))
					{
						if(aTick - d->getLastTick() > (uint32_t)SETTING(DISCONNECT_TIME) * 1000)
						{
							if(QueueManager::getInstance()->dropSource(d))
							{
								dropTargets.push_back(make_pair(d->getPath(), d->getUser()));
							}
						}
					} else {
						d->setLastTick(aTick);
					}
				}
			}

			if(SETTING(FAV_DL_SPEED) > 0) {
				UserPtr fstusr = d->getUser();
				if((speed > SETTING(FAV_DL_SPEED)*1024) && ((aTick - d->getStart()) > 7000) && !FavoriteManager::getInstance()->isFavoriteUser(fstusr)) {
					FavoriteManager::getInstance()->addFavoriteUserB(fstusr);
					FavoriteManager::getInstance()->setUserDescription(fstusr, ("!fast user! (" + Util::toString(getRunningAverage()/1024) + "KB/s)"));
				}
			}
		}

		if(tickList.size() > 0) {
			fire(DownloadManagerListener::Tick(), tickList);
		}
	}

	for_each(userSpeedMap.begin(), userSpeedMap.end(), [](pair<UserPtr, int64_t> us) { us.first->setSpeed(us.second); });
	if (!bundleTicks.empty()) {
		fire(DownloadManagerListener::BundleTick(), bundleTicks, aTick);
	}

	for_each(UBNList, [](pair<CID, AdcCommand>& cap) { ClientManager::getInstance()->send(cap.second, cap.first, true, true); } );
	for_each(dropTargets, [](pair<string, UserPtr>& dt) { QueueManager::getInstance()->removeSource(dt.first, dt.second, QueueItem::Source::FLAG_SLOW_SOURCE);});
}

void DownloadManager::sendSizeNameUpdate(BundlePtr aBundle) {
	//LogManager::getInstance()->message("QueueManager::sendBundleUpdate");
	Lock l (cs);
	aBundle->sendSizeNameUpdate();
}

void DownloadManager::startBundle(UserConnection* aSource, BundlePtr aBundle) {
	if (aSource->getLastBundle().empty() || aSource->getLastBundle() != aBundle->getToken()) {
		if (!aSource->getLastBundle().empty()) {
			//LogManager::getInstance()->message("LASTBUNDLE NOT EMPTY, REMOVE");
			removeRunningUser(aSource);
		} 

		{
			Lock l (cs);
			if (aBundle->addRunningUser(aSource)) {
				//this is the first running user for this bundle
				aBundle->setStart(GET_TICK());
				runningBundles[aBundle->getToken()] = aBundle;
			}
		}
		aSource->setLastBundle(aBundle->getToken());
	}
}

bool DownloadManager::checkIdle(const HintedUser& user, bool smallSlot, bool reportOnly) {

	Lock l(cs);
	for(UserConnectionList::const_iterator i = idlers.begin(); i != idlers.end(); ++i) {	
		UserConnection* uc = *i;
		if(uc->getUser() == user.user) {
			//update the hubHint of the connection to the correct one.
			if(stricmp(uc->getHubUrl(), user.hint) != 0) 
				uc->setHubUrl(user.hint);

			if (((!smallSlot && uc->isSet(UserConnection::FLAG_SMALL_SLOT)) || (smallSlot && !uc->isSet(UserConnection::FLAG_SMALL_SLOT))) && uc->isSet(UserConnection::FLAG_MCN1))
				continue;
			if (!reportOnly)
				uc->updated();
			dcdebug("uc updated");
			return true;
		}	
	}
	return false;
}

void DownloadManager::addConnection(UserConnection* conn) {
	if(!conn->isSet(UserConnection::FLAG_SUPPORTS_TTHF) || !conn->isSet(UserConnection::FLAG_SUPPORTS_ADCGET)) {
		// Can't download from these...
		conn->getUser()->setFlag(User::OLD_CLIENT);
		QueueManager::getInstance()->removeSource(conn->getUser(), QueueItem::Source::FLAG_NO_TTHF);
		conn->disconnect();
		return;
	}


	conn->addListener(this);
	checkDownloads(conn);
}

bool DownloadManager::startDownload(QueueItem::Priority prio, bool mcn) {
	size_t downloadCount = getDownloadCount();
	bool full = (AirUtil::getSlots(true) != 0) && (downloadCount >= (size_t)AirUtil::getSlots(true));
	full = full || ((AirUtil::getSpeedLimit(true) != 0) && (getRunningAverage() >= (AirUtil::getSpeedLimit(true)*1024)));
	//LogManager::getInstance()->message("Speedlimit: " + Util::toString(Util::getSpeedLimit(true)*1024) + " slots: " + Util::toString(Util::getSlots(true)) + " (avg: " + Util::toString(getRunningAverage()) + ")");

	if(full) {
		//LogManager::getInstance()->message("Full");
		bool extraFull = (AirUtil::getSlots(true) != 0) && (getDownloadCount() >= (size_t)(AirUtil::getSlots(true)+SETTING(EXTRA_DOWNLOAD_SLOTS)));
		if(extraFull || mcn) {
			return false;
		}
		return prio == QueueItem::HIGHEST;
	}

	if(downloadCount > 0) {
		return prio != QueueItem::LOWEST;
	}

	return true;
}

void DownloadManager::checkDownloads(UserConnection* aConn) {
	dcassert(aConn->getDownload() == NULL);

	bool smallSlot = aConn->isSet(UserConnection::FLAG_SMALL_SLOT);

	string bundleToken;
	QueueItem::Priority prio = QueueManager::getInstance()->hasDownload(aConn->getUser(), smallSlot, bundleToken);
	bool start = startDownload(prio);
	if(!start && !smallSlot) {
		removeRunningUser(aConn);
		removeConnection(aConn);
		return;
	}

	string errorMessage = Util::emptyString;
	Download* d = QueueManager::getInstance()->getDownload(*aConn, errorMessage, smallSlot);

	if(!d) {
		aConn->unsetFlag(UserConnection::FLAG_RUNNING);
		if(!errorMessage.empty()) {
			fire(DownloadManagerListener::Status(), aConn, errorMessage);
		}
		if (!checkIdle(aConn->getHintedUser(), aConn->isSet(UserConnection::FLAG_SMALL_SLOT), true)) {
			aConn->setState(UserConnection::STATE_IDLE);
			removeRunningUser(aConn);
			{
				Lock l(cs);
 				idlers.push_back(aConn);
			}
		} else {
			aConn->disconnect(true);
		}
		return;
	}

	aConn->setState(UserConnection::STATE_SND);
	
	if(aConn->isSet(UserConnection::FLAG_SUPPORTS_XML_BZLIST) && d->getType() == Transfer::TYPE_FULL_LIST) {
		d->setFlag(Download::FLAG_XML_BZ_LIST);
	}
	
	{
		Lock l(cs);
		downloads.push_back(d);
		BundlePtr b = d->getBundle();
		if (b) {
			b->addDownload(d);
		}
	}
	fire(DownloadManagerListener::Requesting(), d);

	dcdebug("Requesting " I64_FMT "/" I64_FMT "\n", d->getStartPos(), d->getSize());

	/*
	find mySID, better ways to get the correct one transferred here?
	the hinturl of the connection is updated to the hub where the connection reguest is coming from,
	so we should be able to find our own SID by finding the hub where the user is at (if we have a hint).
	*/
	string mySID = Util::emptyString;
	if(!aConn->getUser()->isNMDC() && (d->getType() == Transfer::TYPE_FULL_LIST || d->getType() == Transfer::TYPE_PARTIAL_LIST))
		mySID = ClientManager::getInstance()->findMySID(aConn->getHintedUser());

	aConn->send(d->getCommand(aConn->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET), mySID));
}

void DownloadManager::on(AdcCommand::SND, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(aSource->getState() != UserConnection::STATE_SND) {
		dcdebug("DM::onFileLength Bad state, ignoring\n");
		return;
	}

	if(!aSource->getDownload()) {
		aSource->disconnect(true);
		return;
	}

	const string& type = cmd.getParam(0);
	int64_t start = Util::toInt64(cmd.getParam(2));
	int64_t bytes = Util::toInt64(cmd.getParam(3));

	if(cmd.hasFlag("TL", 4))	 
		aSource->getDownload()->setFlag(Download::FLAG_TTHLIST);
	
	if(type != Transfer::names[aSource->getDownload()->getType()]) {
		// Uhh??? We didn't ask for this...
		aSource->disconnect();
		return;
	}

	startData(aSource, start, bytes, cmd.hasFlag("ZL", 4));
}

void DownloadManager::startData(UserConnection* aSource, int64_t start, int64_t bytes, bool z) {
	Download* d = aSource->getDownload();
	dcassert(d != NULL);

	dcdebug("Preparing " I64_FMT ":" I64_FMT ", " I64_FMT ":" I64_FMT"\n", d->getStartPos(), start, d->getSize(), bytes);
	if(d->getSize() == -1) {
		if(bytes >= 0) {
			d->setSize(bytes);
		} else {
			failDownload(aSource, STRING(INVALID_SIZE));
			return;
		}
	} else if(d->getSize() != bytes || d->getStartPos() != start) {
		// This is not what we requested...
		failDownload(aSource, STRING(INVALID_SIZE));
		return;
	}
	
	try {
		Lock l (cs);
		d->open(bytes, z);
	} catch(const FileException& e) {
		failDownload(aSource, STRING(COULD_NOT_OPEN_TARGET_FILE) + " " + e.getError());
		return;
	} catch(const Exception& e) {
		failDownload(aSource, e.getError());
		return;
	}

	d->setStart(GET_TICK());
	d->tick();
	if (!aSource->isSet(UserConnection::FLAG_RUNNING) && aSource->isSet(UserConnection::FLAG_MCN1)) {
		ConnectionManager::getInstance()->addRunningMCN(aSource);
		aSource->setFlag(UserConnection::FLAG_RUNNING);
	}
	aSource->setState(UserConnection::STATE_RUNNING);

	fire(DownloadManagerListener::Starting(), d);
	if (d->getBundle()) {
		startBundle(aSource, d->getBundle());
	} else if (!aSource->getLastBundle().empty()) {
		removeRunningUser(aSource, true);
	}

	if(d->getPos() == d->getSize()) {
		try {
			// Already finished? A zero-byte file list could cause this...
			endData(aSource);
		} catch(const Exception& e) {
			failDownload(aSource, e.getError());
		}
	} else {
		aSource->setDataMode();
	}
}

void DownloadManager::on(UserConnectionListener::Data, UserConnection* aSource, const uint8_t* aData, size_t aLen) noexcept {
	Download* d = aSource->getDownload();
	dcassert(d != NULL);

	try {
		d->addPos(d->getOutput()->write(aData, aLen), aLen);
		d->tick();

		if(d->getOutput()->eof()) {
			endData(aSource);
			aSource->setLineMode(0);
		}
	} catch(const Exception& e) {
		//d->resetPos(); // is there a better way than resetting the position?
		failDownload(aSource, e.getError());
	}
}

/** Download finished! */
void DownloadManager::endData(UserConnection* aSource) {
	dcassert(aSource->getState() == UserConnection::STATE_RUNNING);
	Download* d = aSource->getDownload();
	dcassert(d != NULL);

	if(d->getType() == Transfer::TYPE_TREE) {
		d->getOutput()->flush();

		int64_t bl = 1024;
		while(bl * (int64_t)d->getTigerTree().getLeaves().size() < d->getTigerTree().getFileSize())
			bl *= 2;
		d->getTigerTree().setBlockSize(bl);
		d->getTigerTree().calcRoot();

		if(!(d->getTTH() == d->getTigerTree().getRoot())) {
			// This tree is for a different file, remove from queue...
			removeDownload(d);
			fire(DownloadManagerListener::Failed(), d, STRING(INVALID_TREE));

			QueueManager::getInstance()->removeSource(d->getPath(), aSource->getUser(), QueueItem::Source::FLAG_BAD_TREE, false);
			QueueManager::getInstance()->putDownload(d, false);

			checkDownloads(aSource);
			return;
		}
		d->setTreeValid(true);
	} else {
		// First, finish writing the file (flushing the buffers and closing the file...)
		try {
			d->getOutput()->flush();
		} catch(const Exception& e) {
			d->resetPos();
			failDownload(aSource, e.getError());
			return;
		}

		aSource->setSpeed(static_cast<int64_t>(d->getAverageSpeed()));
		aSource->updateChunkSize(d->getTigerTree().getBlockSize(), d->getSize(), GET_TICK() - d->getStart());
		
		dcdebug("Download finished: %s, size " I64_FMT ", downloaded " I64_FMT "\n", d->getPath().c_str(), d->getSize(), d->getPos());
	}

	removeDownload(d);

	fire(DownloadManagerListener::Complete(), d, d->getType() == Transfer::TYPE_TREE);
	QueueManager::getInstance()->putDownload(d, true, false);	
	checkDownloads(aSource);
}

int64_t DownloadManager::getRunningAverage() {
	Lock l(cs);
	int64_t avg = 0;
	for(DownloadList::const_iterator i = downloads.begin(); i != downloads.end(); ++i) {
		Download* d = *i;
		avg += static_cast<int64_t>(d->getAverageSpeed());
	}
	return avg;
}

void DownloadManager::on(UserConnectionListener::MaxedOut, UserConnection* aSource, string param) noexcept {
	noSlots(aSource, param);
}
void DownloadManager::noSlots(UserConnection* aSource, string param) {
	if(aSource->getState() != UserConnection::STATE_SND) {
		dcdebug("DM::noSlots Bad state, disconnecting\n");
		aSource->disconnect();
		return;
	}

	string extra = param.empty() ? Util::emptyString : " - " + STRING(QUEUED) + " " + param;
	failDownload(aSource, STRING(NO_SLOTS_AVAILABLE) + extra);
}

void DownloadManager::onFailed(UserConnection* aSource, const string& aError) {
	{
		Lock l(cs);
 		idlers.erase(remove(idlers.begin(), idlers.end(), aSource), idlers.end());
	}
	failDownload(aSource, aError);
}

void DownloadManager::failDownload(UserConnection* aSource, const string& reason) {
	Download* d = aSource->getDownload();
	if(d) {
		removeDownload(d);
		fire(DownloadManagerListener::Failed(), d, reason);
		QueueManager::getInstance()->putDownload(d, false);
	}

	removeRunningUser(aSource);
	removeConnection(aSource);
}

void DownloadManager::removeConnection(UserConnectionPtr aConn) {
	dcassert(aConn->getDownload() == NULL);
	aConn->removeListener(this);
	aConn->disconnect();
}

void DownloadManager::removeDownload(Download* d) {
	if(d->getOutput()) {
		if(d->getActual() > 0) {
			try {
				d->getOutput()->flush();
			} catch(const Exception&) {
			}
		}
	}

	{
		Lock l(cs);

		BundlePtr bundle = d->getBundle();
		if (bundle) {
			bundle->removeDownload(d);
		}

		dcassert(find(downloads.begin(), downloads.end(), d) != downloads.end());
		downloads.erase(remove(downloads.begin(), downloads.end(), d), downloads.end());
	}
}

void DownloadManager::setTarget(const string& oldTarget, const string& newTarget) {
	Lock l (cs);
	for(auto i = downloads.begin(); i != downloads.end(); ++i) {
		Download* d = *i;
		if (d->getPath() == oldTarget) {
			d->setPath(newTarget);
			dcassert(d->getBundle());
			//update the target in transferview
			fire(DownloadManagerListener::TargetChanged(), d->getPath(), d->getToken(), d->getBundle()->getToken());
		}
	}
}

void DownloadManager::changeBundle(BundlePtr sourceBundle, BundlePtr targetBundle, const string& path) {
	UserConnectionList ucl;
	{
		Lock l (cs);
		for(auto i = sourceBundle->getDownloads().begin(); i != sourceBundle->getDownloads().end();) {
			Download* d = *i;
			if (d->getPath() == path) {
				targetBundle->addDownload(d);
				d->setBundle(targetBundle);
				//update the bundle in transferview
				fire(DownloadManagerListener::TargetChanged(), d->getPath(), d->getToken(), d->getBundle()->getToken());
				ucl.push_back(&d->getUserConnection());
				sourceBundle->removeDownload(d);
			} else {
				i++;
			}
		}
	}

	for(auto i = ucl.begin(); i != ucl.end(); ++i) {
		startBundle(*i, targetBundle);
	}
}

BundlePtr DownloadManager::findRunningBundle(const string& bundleToken) {
	auto s = runningBundles.find(bundleToken);
	if (s != runningBundles.end()) {
		return s->second;
	}
	return NULL;
}

void DownloadManager::removeRunningUser(UserConnection* aSource, bool sendRemove /*false*/) {
	if (aSource->getLastBundle().empty()) {
		return;
	}

	{
		Lock l (cs);
		BundlePtr bundle = findRunningBundle(aSource->getLastBundle());
		if (bundle && bundle->removeRunningUser(aSource, sendRemove)) {
			//no running users for this bundle
			runningBundles.erase(bundle->getToken());
			fire(DownloadManagerListener::BundleWaiting(), bundle);
		}
	}

	aSource->setLastBundle(Util::emptyString);
}

void DownloadManager::disconnectBundle(BundlePtr aBundle, const UserPtr& aUser) {
	//UserConnectionList u;
	{
		Lock l(cs);
		for(auto i = aBundle->getDownloads().begin(); i != aBundle->getDownloads().end(); ++i) {
			Download* d = *i;
			if (aUser && d->getUser() != aUser) {
				continue;
			}
			d->getUserConnection().disconnect(true);
		}
	}
	/*for (auto i = u.begin(); i != u.end(); ++i) {
		(*i)->disconnect(true);
	} */
}

void DownloadManager::abortDownload(const string& aTarget, const UserPtr& aUser) {
	Lock l(cs);
	
	for(DownloadList::const_iterator i = downloads.begin(); i != downloads.end(); ++i) {
		Download* d = *i;
		if(d->getPath() == aTarget) {
			if (aUser) {
				if (d->getUser() != aUser) {
					continue;
				}
			}
			dcdebug("Trying to close connection for download 0x%X\n", d);
			d->getUserConnection().disconnect(true);
		}
	}
}

void DownloadManager::on(UserConnectionListener::FileNotAvailable, UserConnection* aSource) noexcept {
	if(!aSource->getDownload()) {
		aSource->disconnect(true);
		return;
	}
	fileNotAvailable(aSource);
}

/** @todo Handle errors better */
void DownloadManager::on(AdcCommand::STA, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(cmd.getParameters().size() < 2) {
		aSource->disconnect();
		return;
	}

	const string& err = cmd.getParameters()[0];
	if(err.length() != 3) {
		aSource->disconnect();
		return;
	}

	switch(Util::toInt(err.substr(0, 1))) {
	case AdcCommand::SEV_FATAL:
		aSource->disconnect();
		return;
	case AdcCommand::SEV_RECOVERABLE:
		switch(Util::toInt(err.substr(1))) {
		case AdcCommand::ERROR_FILE_NOT_AVAILABLE:
			fileNotAvailable(aSource);
			return;
		case AdcCommand::ERROR_SLOTS_FULL:
			string param;
			noSlots(aSource, cmd.getParam("QP", 0, param) ? param : Util::emptyString);
			return;
		}
	case AdcCommand::SEV_SUCCESS:
		// We don't know any messages that would give us these...
		dcdebug("Unknown success message %s %s", err.c_str(), cmd.getParam(1).c_str());
		return;
	}
	aSource->disconnect();
}

void DownloadManager::on(UserConnectionListener::Updated, UserConnection* aSource) noexcept {
	{
		Lock l(cs);
		UserConnectionList::iterator i = find(idlers.begin(), idlers.end(), aSource);
		if(i == idlers.end())
			return;
		idlers.erase(i);
	}
	
	checkDownloads(aSource);
}

void DownloadManager::fileNotAvailable(UserConnection* aSource) {
	if(aSource->getState() != UserConnection::STATE_SND) {
		dcdebug("DM::fileNotAvailable Invalid state, disconnecting");
		aSource->disconnect();
		return;
	}
	
	Download* d = aSource->getDownload();
	dcassert(d != NULL);
	dcdebug("File Not Available: %s\n", d->getPath().c_str());

	removeDownload(d);
	removeRunningUser(aSource);

	if (d->isSet(Download::FLAG_NFO)) {
		fire(DownloadManagerListener::Failed(), d, STRING(NO_PARTIAL_SUPPORT));
		dcdebug("Partial list & View NFO. File not available\n");
		QueueManager::getInstance()->putDownload(d, true); // true, false is not used in putDownload for partial
		removeConnection(aSource);
		return;
	} else if (d->getType() == Transfer::TYPE_PARTIAL_LIST && !aSource->isSet(UserConnection::FLAG_SMALL_SLOT)) {
		fire(DownloadManagerListener::Failed(), d, STRING(NO_PARTIAL_SUPPORT_RETRY));
	} else {
		fire(DownloadManagerListener::Failed(), d, STRING(FILE_NOT_AVAILABLE));
	}

	if (d->getType() == Transfer::TYPE_FULL_LIST) {
	
		QueueManager::getInstance()->putDownload(d, true);
		removeConnection(aSource);
		return;

	// Need to check if it's booth partial and view
	} else if ((d->getType() == Transfer::TYPE_PARTIAL_LIST) && (d->isSet(Download::FLAG_VIEW))) {
		dcdebug("Partial list & View. File not available\n");
		string dir = d->getPath().c_str();
		QueueManager::getInstance()->putDownload(d, true); // true, false is not used in putDownload for partial
		removeConnection(aSource);
		QueueManager::getInstance()->addList(aSource->getHintedUser(), QueueItem::FLAG_CLIENT_VIEW, dir);
		return;

// Todo if partial fail from search frame
// Something like this, need to think some more
/*	} else if ((d->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD)) && (d->getType() == Transfer::TYPE_PARTIAL_LIST)){
		dcdebug("Partial list & Directory Download. File not available\n");
		string file = d->getPath().c_str();
		QueueManager::getInstance()->putDownload(d, true); // true, false is not used in putDownload for partial
		removeConnection(aSource);
		QueueManager::getInstance()->addDirectory(file, HintedUser(aSource->getUser()), file);
		return;*/

	// Need to check if it's booth partial and match queue
	} else if ((d->getType() == Transfer::TYPE_PARTIAL_LIST) && (d->isSet(Download::FLAG_QUEUE))){
		dcdebug("Partial list & Match Queue. File not available\n");
		QueueManager::getInstance()->putDownload(d, true); // true, false is not used in putDownload for partial
		removeConnection(aSource);
		QueueManager::getInstance()->addList(aSource->getHintedUser(), QueueItem::FLAG_MATCH_QUEUE);
		return;
	}

	QueueManager::getInstance()->removeSource(d->getPath(), aSource->getUser(), (Flags::MaskType)(d->getType() == Transfer::TYPE_TREE ? QueueItem::Source::FLAG_NO_TREE : QueueItem::Source::FLAG_FILE_NOT_AVAILABLE), false);

	QueueManager::getInstance()->putDownload(d, false);
	checkDownloads(aSource);
}




} // namespace dcpp

/**
 * @file
 * $Id: DownloadManager.cpp 500 2010-06-25 22:08:18Z bigmuscle $
 */
