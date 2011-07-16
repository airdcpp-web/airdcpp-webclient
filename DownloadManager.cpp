/* 
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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
#include "DCPlusPlus.h"
#include "ConnectionManager.h"
#include "DownloadManager.h"

#include "ResourceManager.h"
#include "QueueManager.h"
#include "HashManager.h"
#include "Download.h"
#include "LogManager.h"
#include "User.h"
#include "File.h"
#include "FilteredFile.h"
#include "MerkleCheckOutputStream.h"
#include "UserConnection.h"
#include "ZUtils.h"


#include "UploadManager.h"
#include "FavoriteManager.h"

#include <limits>
#include <cmath>


// some strange mac definition
#ifdef ff
#undef ff
#endif

namespace dcpp {

static const string DOWNLOAD_AREA = "Downloads";

DownloadManager::DownloadManager() {
	TimerManager::getInstance()->addListener(this);
}

DownloadManager::~DownloadManager() throw() {
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

void DownloadManager::on(TimerManagerListener::Second, uint64_t aTick) throw() {
	typedef vector<pair<string, UserPtr> > TargetList;
	TargetList dropTargets;
	
	{
		Lock l(cs);

	DownloadList tickList;

	averageSpeedMap.clear();
	averagePosMap.clear();

		// Tick each ongoing download
		for(DownloadList::const_iterator i = downloads.begin(); i != downloads.end(); ++i) {
			Download* d = *i;

			if(d->getPos() > 0) {
				tickList.push_back(d);
				d->tick();
			}

			if (d->getType() == Transfer::TYPE_FILE && d->getStart() > 0)
			{
				if (d->getTigerTree().getFileSize() > (SETTING(DISCONNECT_FILESIZE) * 1048576))
				{
					if((d->getAverageSpeed() < SETTING(DISCONNECT_SPEED) * 1024))
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
	
			//averages for the download folders
		string tmp = d->getPath().substr(0, d->getPath().rfind("\\"));
		StringIntIter j = averageSpeedMap.find(tmp);
		if(j != averageSpeedMap.end())
			j->second += d->getAverageSpeed();
		else
			averageSpeedMap.insert(StringIntPair(tmp, d->getAverageSpeed()));
		
		
		j = averagePosMap.find(tmp);
		if(j != averagePosMap.end())
			j->second += d->getPos();
		else
			averagePosMap.insert(StringIntPair(tmp, d->getPos()));


		if(SETTING(FAV_DL_SPEED) > 0) {
			UserPtr fstusr = d->getUser();
			if(FavoriteManager::getInstance()->isFavoriteUser(fstusr) == false) {
				if(d->getAverageSpeed() > SETTING(FAV_DL_SPEED)*1024) {
					if((aTick - d->getStart()) > 7000) {
						FavoriteManager::getInstance()->addFavoriteUserB(fstusr);
						FavoriteManager::getInstance()->setUserDescription(fstusr, ("!fast user! (" + Util::toString(getRunningAverage()/1024) + "KB/s)"));
					}
				}
			}
		}
}
		if(tickList.size() > 0)
			fire(DownloadManagerListener::Tick(), tickList);
	}

	for(TargetList::iterator i = dropTargets.begin(); i != dropTargets.end(); ++i) {
		QueueManager::getInstance()->removeSource(i->first, i->second, QueueItem::Source::FLAG_SLOW_SOURCE);
	}
}

bool DownloadManager::checkIdle(const UserPtr& user, bool partialList) {
	Lock l(cs);	
	for(UserConnectionList::const_iterator i = idlers.begin(); i != idlers.end(); ++i) {	
		UserConnection* uc = *i;	
		if(uc->getUser() == user) {
			if ((!partialList && uc->isSet(UserConnection::FLAG_PARTIAL_LIST)) || (partialList && !uc->isSet(UserConnection::FLAG_PARTIAL_LIST)) && uc->isSet(UserConnection::FLAG_MCN1))
				continue;
			uc->updated();
			return true;
		}	
	}
	return false;
}

void DownloadManager::addConnection(UserConnectionPtr conn) {
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

bool DownloadManager::startDownload(QueueItem::Priority prio) {

	size_t downloadCount = getDownloadCount();


	bool full = (SETTING(DOWNLOAD_SLOTS) != 0) && (downloadCount >= (size_t)SETTING(DOWNLOAD_SLOTS));
	full = full || ((SETTING(MAX_DOWNLOAD_SPEED) != 0) && (getRunningAverage() >= (SETTING(MAX_DOWNLOAD_SPEED)*1024)));

	if(full) {
		bool extraFull = (SETTING(DOWNLOAD_SLOTS) != 0) && (getDownloadCount() >= (size_t)(SETTING(DOWNLOAD_SLOTS)+SETTING(EXTRA_DOWNLOAD_SLOTS)));
		if(extraFull) {
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

	bool partial=false;
	if (aConn->isSet(UserConnection::FLAG_PARTIAL_LIST) == 1)
		partial=true;

	QueueItem::Priority prio = QueueManager::getInstance()->hasDownload(aConn->getUser(), partial);
	if(!startDownload(prio)) {
		removeConnection(aConn);
		return;
	}

	string errorMessage = Util::emptyString;
	Download* d = QueueManager::getInstance()->getDownload(*aConn, errorMessage, partial);

	if(!d) {
		if(!errorMessage.empty()) {
			fire(DownloadManagerListener::Status(), aConn, errorMessage);
		}

		Lock l(cs);
		aConn->setState(UserConnection::STATE_IDLE);
 	    idlers.push_back(aConn);
		return;
	}

	aConn->setState(UserConnection::STATE_SND);
	
	if(aConn->isSet(UserConnection::FLAG_SUPPORTS_XML_BZLIST) && d->getType() == Transfer::TYPE_FULL_LIST) {
		d->setFlag(Download::FLAG_XML_BZ_LIST);
	}
	
	{
		Lock l(cs);
		downloads.push_back(d);
	}
	fire(DownloadManagerListener::Requesting(), d);

	dcdebug("Requesting " I64_FMT "/" I64_FMT "\n", d->getStartPos(), d->getSize());

	aConn->send(d->getCommand(aConn->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET)));
}

void DownloadManager::on(AdcCommand::SND, UserConnection* aSource, const AdcCommand& cmd) throw() {
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
		QueueManager::getInstance()->setFile(d);
	} catch(const FileException& e) {
		failDownload(aSource, STRING(COULD_NOT_OPEN_TARGET_FILE) + " " + e.getError());
		return;
	} catch(const Exception& e) {
		failDownload(aSource, e.getError());
		return;
	}

	try {
		if((d->getType() == Transfer::TYPE_FILE || d->getType() == Transfer::TYPE_FULL_LIST) && SETTING(BUFFER_SIZE) > 0 ) {
			d->setFile(new BufferedOutputStream<true>(d->getFile()));
		}
	} catch(const Exception& e) {
		failDownload(aSource, e.getError());
		return;
	} catch(...) {
		delete d->getFile();
		d->setFile(NULL);
		return;			
	}
			
	if(d->getType() == Transfer::TYPE_FILE) {
		typedef MerkleCheckOutputStream<TigerTree, true> MerkleStream;
		
		d->setFile(new MerkleStream(d->getTigerTree(), d->getFile(), d->getStartPos()));
		d->setFlag(Download::FLAG_TTH_CHECK);
	}
	
	// Check that we don't get too many bytes
	d->setFile(new LimitedOutputStream<true>(d->getFile(), bytes));

	if(z) {
		d->setFlag(Download::FLAG_ZDOWNLOAD);
		d->setFile(new FilteredOutputStream<UnZFilter, true>(d->getFile()));
	}

	d->setStart(GET_TICK());
	d->tick();
	aSource->setState(UserConnection::STATE_RUNNING);

	fire(DownloadManagerListener::Starting(), d);
	ConnectionManager::getInstance()->checkWaitingMCN();

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

void DownloadManager::on(UserConnectionListener::Data, UserConnection* aSource, const uint8_t* aData, size_t aLen) throw() {
	Download* d = aSource->getDownload();
	dcassert(d != NULL);

	try {
		d->addPos(d->getFile()->write(aData, aLen), aLen);
		d->tick();

		if(d->getFile()->eof()) {
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
		d->getFile()->flush();

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
			d->getFile()->flush();
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

	if(d->getType() != Transfer::TYPE_FILE)
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

void DownloadManager::on(UserConnectionListener::MaxedOut, UserConnection* aSource, string param) throw() {
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

	removeConnection(aSource);
}

void DownloadManager::removeConnection(UserConnectionPtr aConn) {
	dcassert(aConn->getDownload() == NULL);
	aConn->removeListener(this);
	aConn->disconnect();
}

void DownloadManager::removeDownload(Download* d) {
	if(d->getFile()) {
		if(d->getActual() > 0) {
			try {
				d->getFile()->flush();
			} catch(const Exception&) {
			}
		}
	}

	{
		Lock l(cs);
		dcassert(find(downloads.begin(), downloads.end(), d) != downloads.end());

		downloads.erase(remove(downloads.begin(), downloads.end(), d), downloads.end());
	}
}

void DownloadManager::abortDownload(const string& aTarget) {
	Lock l(cs);
	
	for(DownloadList::const_iterator i = downloads.begin(); i != downloads.end(); ++i) {
		Download* d = *i;
		if(d->getPath() == aTarget) {
			dcdebug("Trying to close connection for download 0x%X\n", d);
			d->getUserConnection().disconnect(true);
		}
	}
}

void DownloadManager::on(UserConnectionListener::FileNotAvailable, UserConnection* aSource) throw() {
	if(!aSource->getDownload()) {
		aSource->disconnect(true);
		return;
	}
	fileNotAvailable(aSource);
}

/** @todo Handle errors better */
void DownloadManager::on(AdcCommand::STA, UserConnection* aSource, const AdcCommand& cmd) throw() {
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

void DownloadManager::on(UserConnectionListener::Updated, UserConnection* aSource) throw() {
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
	if (d->isSet(Download::FLAG_NFO)) {
		fire(DownloadManagerListener::Failed(), d, STRING(NO_PARTIAL_SUPPORT));
		dcdebug("Partial list & View NFO. File not available\n");
		QueueManager::getInstance()->putDownload(d, true); // true, false is not used in putDownload for partial
		removeConnection(aSource);
		return;
	} else if (d->getType() == Transfer::TYPE_PARTIAL_LIST) {
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
