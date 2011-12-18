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
	typedef vector<pair<string, UserPtr> > TargetList;
	TargetList dropTargets;
	StringList targets;
	BundleList bundleTicks;
	{
		Lock l(cs);
		boost::unordered_map<UserPtr, int64_t> userSpeedMap;
		for (auto i = runningBundles.begin(); i != runningBundles.end(); ++i) {
			BundlePtr bundle = i->second;
			int64_t bundleSpeed = 0, bundleRatio = 0, bundlePos = 0;
			int downloads = 0;
			for (auto s = bundle->getDownloads().begin(); s != bundle->getDownloads().end(); ++s) {
				Download* d = *s;
				if (d->getAverageSpeed() > 0 && d->getStart() > 0) {
					downloads++;
					//bundle->setDownloadedBytes(d->getPos());
					int64_t pos = d->getPos();
					bundleSpeed += d->getAverageSpeed();
					bundleRatio += pos > 0 ? (double)d->getActual() / (double)pos : 1.00;
					userSpeedMap[d->getUser()] += d->getAverageSpeed();
					bundlePos += pos;
				}
			}
			if (bundleSpeed > 0) {
				bundleRatio = bundleRatio / downloads;
				bundle->setActual((int64_t)((double)bundle->getDownloadedBytes() * (bundleRatio == 0 ? 1.00 : bundleRatio)));
				bundle->setSpeed(bundleSpeed);
				bundle->setRunning(downloads);
				bundle->setDownloadedBytes(bundlePos);
				bundleTicks.push_back(bundle);
			}
		}

		for (auto i = userSpeedMap.begin(); i != userSpeedMap.end(); ++i) {
			i->first->setSpeed(i->second);
		}

		DownloadList tickList;

		// Tick each ongoing download
		for(DownloadList::const_iterator i = downloads.begin(); i != downloads.end(); ++i) {
			Download* d = *i;
			double speed = d->getAverageSpeed();

			if(d->getPos() > 0) {
				tickList.push_back(d);
				d->tick();
			}

			if (d->getType() == Transfer::TYPE_FILE && d->getStart() > 0)
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
				if(FavoriteManager::getInstance()->isFavoriteUser(fstusr) == false) {
					if(speed > SETTING(FAV_DL_SPEED)*1024) {
						if((aTick - d->getStart()) > 7000) {
							FavoriteManager::getInstance()->addFavoriteUserB(fstusr);
							FavoriteManager::getInstance()->setUserDescription(fstusr, ("!fast user! (" + Util::toString(getRunningAverage()/1024) + "KB/s)"));
						}
					}
				}
			}
		}

		if(tickList.size() > 0) {
			fire(DownloadManagerListener::Tick(), tickList);
		}
	}

	if (!bundleTicks.empty()) {
		updateBundles(bundleTicks);
		fire(DownloadManagerListener::BundleTick(), bundleTicks);
	}

	for(TargetList::iterator i = dropTargets.begin(); i != dropTargets.end(); ++i) {
		QueueManager::getInstance()->removeSource(i->first, i->second, QueueItem::Source::FLAG_SLOW_SOURCE);
	}
}


bool DownloadManager::sendBundle(UserConnection* aSource, BundlePtr aBundle, bool updateOnly) {
	/*if (updateOnly) {
		LogManager::getInstance()->message("DOWNLOADMANAGER SENDBUNDLE: " + aBundle->getName() + ", updateOnly");
	} else {
		LogManager::getInstance()->message("DOWNLOADMANAGER SENDBUNDLE: " + aBundle->getName());
	} */

	AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", aSource->getHintedUser().hint);
	cmd.addParam("TO", aSource->getToken());
	cmd.addParam("BU", aBundle->getToken());
	if (!updateOnly) {
		cmd.addParam("SI", Util::toString(aBundle->getSize()));
		cmd.addParam("NA", aBundle->getName());
		cmd.addParam("DL", Util::toString(aBundle->getDownloadedBytes()));
		if (aBundle->getSingleUser()) {
			cmd.addParam("SU1");
		} else {
			cmd.addParam("MU1");
		}
		cmd.addParam("AD1");
	} else {
		cmd.addParam("CH1");
	}
	return ClientManager::getInstance()->send(cmd, aSource->getUser()->getCID(), true, true);
}

void DownloadManager::updateBundles(BundleList& bundles) {
	for(BundleList::iterator j = bundles.begin(); j != bundles.end(); ++j) {
		BundlePtr bundle = *j;
		if (bundle->getSingleUser() || bundle->getUploadReports().empty()) {
			continue;
		}
		string speed;
		string bundleToken = bundle->getToken();
		double percent = 0;
		float change = (((float)bundle->getSpeed() - (float)bundle->getLastSpeed()) / (((float)bundle->getSpeed() + (float)bundle->getLastSpeed()) / 2.00000));
		//LogManager::getInstance()->message("SPEEDCHANGE: " + Util::toString(change) + " old: " + Util::toString(bundle->getTotalSpeed()) + "current: " + Util::toString(bundle->getSpeed()));

		if (abs(change) > 0.05) {
			//LogManager::getInstance()->message("SEND SPEEDCHANGE: " + Util::toString(abs(change)) + " old: " + Util::toString(bundle->getTotalSpeed()) + "current: " + Util::toString(bundle->getSpeed()));
			speed = formatDownloaded(bundle->getSpeed());
			bundle->setLastSpeed(bundle->getSpeed());
		} else {
			//LogManager::getInstance()->message("DONT SEND");
		}
		//LogManager::getInstance()->message("SpeedCompare: " + Util::toString(bundle->getSpeed() - bundle->getTotalSpeed()) + " should be bigger than " + Util::toString(bundle->getSpeed()*0.05));

		if (floor(bundle->getLastPercent()) < floor(((float)bundle->getDownloadedBytes() / (float)bundle->getSize())* 10000)) {
			percent = (((float)bundle->getDownloadedBytes() / (float)bundle->getSize())*100);
			dcassert(percent <= 100.00);
			bundle->setLastPercent(percent*100.000);
		}
		//LogManager::getInstance()->message("PercentCompare: " + Util::toString(bundle->getLastPercent()) + " should be smaller than " + Util::toString(floor(((float)bundle->getDownloaded() / (float)bundle->getSize())* 10000)));

		//LogManager::getInstance()->message("Bundle notify info, percent: " + Util::toString(percent) + " speed: " + speed);
		if (!speed.empty() || percent > 0) {
			Lock l (cs);
			for(auto i = bundle->getUploadReports().begin(); i != bundle->getUploadReports().end(); ++i) {
				sendBundleUBN((*i), speed, percent, bundleToken);
			}
		}
	}
}

void DownloadManager::sendBundleUBN(HintedUser& user, const string& speed, const double percent, const string& bundleToken) {
	AdcCommand cmd(AdcCommand::CMD_UBN, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", user.hint);
	cmd.addParam("BU", bundleToken);
	if (!speed.empty())
		cmd.addParam("DS", speed);
	if (percent > 0)
		cmd.addParam("PE", Util::toString(percent));

	ClientManager::getInstance()->send(cmd, user.user->getCID(), true);
}

void DownloadManager::sendSizeNameUpdate(BundlePtr aBundle) {
	//LogManager::getInstance()->message("QueueManager::sendBundleUpdate");
	Lock l (cs);
	for(auto i = aBundle->getUploadReports().begin(); i != aBundle->getUploadReports().end(); ++i) {
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);
		cmd.addParam("HI", (*i).hint);
		cmd.addParam("BU", aBundle->getToken());

		if (aBundle->isSet(Bundle::FLAG_UPDATE_SIZE)) {
			aBundle->unsetFlag(Bundle::FLAG_UPDATE_SIZE);
			cmd.addParam("SI", Util::toString(aBundle->getSize()));
			//LogManager::getInstance()->message("UBD for bundle: " + aBundle->getName() + ", size: " + Util::toString(aBundle->getSize()));
		}

		if (aBundle->isSet(Bundle::FLAG_UPDATE_NAME)) {
			aBundle->unsetFlag(Bundle::FLAG_UPDATE_NAME);
			cmd.addParam("NA", aBundle->getName());
			//LogManager::getInstance()->message("UBD for bundle: " + aBundle->getName() + ", name: " + aBundle->getName());
		}

		cmd.addParam("UD1");

		ClientManager::getInstance()->send(cmd, (*i).user->getCID(), true);
	}
}

string DownloadManager::formatDownloaded(int64_t aBytes) {
	char buf[64];
	if(aBytes < 1024) {
		snprintf(buf, sizeof(buf), "%d%s", (int)(aBytes&0xffffffff), "b");
	} else if(aBytes < 1048576) {
		snprintf(buf, sizeof(buf), "%.02f%s", (double)aBytes/(1024.0), "k");
	} else {
		snprintf(buf, sizeof(buf), "%.02f%s", (double)aBytes/(1048576.0), "m");
	}
	return buf;
}

void DownloadManager::startBundle(UserConnection* aSource, BundlePtr aBundle) {
	if (aSource->getLastBundle().empty() || aSource->getLastBundle() != aBundle->getToken()) {
		bool updateOnly = false;
		UserPtr& u = aSource->getUser();
		if (!aSource->getLastBundle().empty()) {
			//LogManager::getInstance()->message("LASTBUNDLE NOT EMPTY, REMOVE");
			removeRunningUser(aSource);
		} 

		{

			if (aBundle->getRunningUsers().empty()) {
				fire(DownloadManagerListener::BundleUser(), aBundle->getToken(), aSource->getHintedUser());
				aBundle->setStart(GET_TICK());
			}

			bool sendMode=false;
			{
				Lock l (cs);
				auto y =  aBundle->getRunningUsers().find(u);
				if (y == aBundle->getRunningUsers().end()) {
					//LogManager::getInstance()->message("ADD DL BUNDLE, USER NOT FOUND, ADD NEW");
					if (aBundle->getSingleUser() && !aBundle->getRunningUsers().empty()) {
						//LogManager::getInstance()->message("SEND BUNDLE MODE");
						sendMode=true;
					} else if (aBundle->getRunningUsers().empty()) {
						runningBundles[aBundle->getToken()] = aBundle;
					}
					aBundle->getRunningUsers()[u] = 1;
				} else {
					updateOnly = true;
					y->second++;
					//LogManager::getInstance()->message("ADD DL BUNDLE, USER FOUND, INCREASE CONNECTIONS: " + Util::toString(y->second));
				}
			}

			if (sendMode) {
				sendBundleMode(aBundle, false);
			}
		}

		if (aSource->isSet(UserConnection::FLAG_UBN1)) {
			if (sendBundle(aSource, aBundle, updateOnly) && !updateOnly) {
				Lock l (cs);
				if (aBundle->getUploadReports().empty()) {
					aBundle->setLastSpeed(0);
					aBundle->setLastPercent(0);
				}
				aBundle->getUploadReports().push_back(aSource->getHintedUser());
				//LogManager::getInstance()->message("ADD UPLOAD REPORT: " + Util::toString(aBundle->getUploadReports().size()));
			}
		}

		aSource->setLastBundle(aBundle->getToken());
	} else  {

	}
}

void DownloadManager::sendBundleMode(BundlePtr aBundle, bool singleUser) {
	string bundleToken = aBundle->getToken();
	Lock l (cs);

	if (singleUser) {
		if (aBundle->getRunningUsers().size() != 1) {
			//LogManager::getInstance()->message("SET BUNDLE SINGLEUSER, FAAAAILED: " + Util::toString(aBundle->runningUsers.size()));
			return;
		}
		aBundle->setLastSpeed(0);
		aBundle->setLastPercent(0);
		aBundle->setSingleUser(true);
		//LogManager::getInstance()->message("SET BUNDLE SINGLEUSER, RUNNING: " + Util::toString(aBundle->runningUsers.size()));
	} else {
		aBundle->setSingleUser(false);
		//LogManager::getInstance()->message("SET BUNDLE MULTIUSER, RUNNING: " + aBundle->runningUsers.size());
	}

	for(auto i = aBundle->getUploadReports().begin(); i != aBundle->getUploadReports().end(); ++i) {
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

		cmd.addParam("HI", (*i).hint);
		cmd.addParam("BU", bundleToken);
		cmd.addParam("UD1");
		if (singleUser)
			cmd.addParam("SU1");
		else
			cmd.addParam("MU1");

		ClientManager::getInstance()->send(cmd, (*i).user->getCID(), true);
	}
}

void DownloadManager::sendBundleFinished(BundlePtr aBundle) {
	Lock l (cs);
	for(auto i = aBundle->getUploadReports().begin(); i != aBundle->getUploadReports().end(); ++i) {
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

		cmd.addParam("HI", (*i).hint);
		cmd.addParam("BU", aBundle->getToken());
		cmd.addParam("FI1");

		ClientManager::getInstance()->send(cmd, (*i).user->getCID(), true);
	}
}

bool DownloadManager::checkIdle(const UserPtr& user, bool smallSlot, bool reportOnly) {

	Lock l(cs);
	for(UserConnectionList::const_iterator i = idlers.begin(); i != idlers.end(); ++i) {	
		UserConnection* uc = *i;
		if(uc->getUser() == user) {
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

bool DownloadManager::startDownload(QueueItem::Priority prio, const UserPtr& user, const string& aToken, bool mcn) {

	if (user) {
		//update the target in transferview for better grouping
		fire(DownloadManagerListener::Target(), user, aToken);
	}
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

	bool smallSlot=false;
	if (aConn->isSet(UserConnection::FLAG_SMALL_SLOT)) {
		smallSlot=true;
	}

	QueueItem::Priority prio = QueueManager::getInstance()->hasDownload(aConn->getUser(), smallSlot);
	bool start = startDownload(prio, aConn->getUser(), aConn->getToken());
	if(!start && !smallSlot) {
		removeConnection(aConn);
		return;
	}

	string errorMessage = Util::emptyString;
	Download* d = QueueManager::getInstance()->getDownload(*aConn, errorMessage, smallSlot);

	if(!d) {
		if(!errorMessage.empty()) {
			fire(DownloadManagerListener::Status(), aConn, errorMessage);
		}
		if (!checkIdle(aConn->getUser(), aConn->isSet(UserConnection::FLAG_SMALL_SLOT), true)) {
			aConn->setState(UserConnection::STATE_IDLE);
			removeRunningUser(aConn);
			{
				Lock l(cs);
 				idlers.push_back(aConn);
			}
			ConnectionManager::getInstance()->changeCQIState(aConn, true);
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

	aConn->send(d->getCommand(aConn->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET)));
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
	ConnectionManager::getInstance()->changeCQIState(aSource, false);
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
	//BundlePtr bundle = d->getBundle();
	dcassert(d != NULL);

	try {
		d->addPos(d->getFile()->write(aData, aLen), aLen);
		//LogManager::getInstance()->message("pos: " + Util::toString(pos) + "   aLen: " + Util::toString(aLen));
		//if (bundle)
		//	bundle->increaseDownloaded(aLen);
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
			//removeRunningUser(aSource, false);
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

	removeRunningUser(aSource);
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

		BundlePtr bundle = d->getBundle();
		if (bundle) {
			bundle->removeDownload(d->getUserConnection().getToken());
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
			fire(DownloadManagerListener::TargetChanged(), d->getPath(), d->getUserConnection().getToken(), d->getBundle()->getToken());
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
				fire(DownloadManagerListener::TargetChanged(), d->getPath(), d->getUserConnection().getToken(), d->getBundle()->getToken());
				ucl.push_back(&d->getUserConnection());
				sourceBundle->removeDownload(d->getUserConnection().getToken(), false);
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
		bool sendMode=false;
		BundlePtr bundle = findRunningBundle(aSource->getLastBundle());
		if (bundle) {
			Lock l (cs);
			auto y =  bundle->getRunningUsers().find(aSource->getUser());
			dcassert(y != bundle->getRunningUsers().end());
			if (y != bundle->getRunningUsers().end()) {
				y->second--;
				if (y->second == 0) {
					bundle->getRunningUsers().erase(y);
					for(auto i = bundle->getUploadReports().begin(); i != bundle->getUploadReports().end(); ++i) {
						if (i->user == aSource->getUser()) {
							bundle->getUploadReports().erase(i);
							//LogManager::getInstance()->message("ERASE UPLOAD REPORT: " + Util::toString(bundle->getUploadReports().size()));
							break;
						}
					}
					//LogManager::getInstance()->message("NO RUNNING, ERASE: uploadReports size " + Util::toString(bundle->getUploadReports().size()));
					if (bundle->getRunningUsers().size() == 1) {
						sendMode=true;
					} else if (bundle->getRunningUsers().empty()) {
						dcassert(runningBundles.find(bundle->getToken()) != runningBundles.end());
						runningBundles.erase(bundle->getToken());
						fire(DownloadManagerListener::BundleWaiting(), bundle);
					}
				} else {
					//LogManager::getInstance()->message("STILL RUNNING: " + Util::toString(y->second));
				}
			}

			if (sendRemove) {
				for(auto i = bundle->getUploadReports().begin(); i != bundle->getUploadReports().end(); ++i) {
					AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

					cmd.addParam("HI", (*i).hint);
					cmd.addParam("TO", bundle->getToken());
					cmd.addParam("RM1");

					ClientManager::getInstance()->send(cmd, (*i).user->getCID(), true);
				}
			}

			if (sendMode) {
				sendBundleMode(bundle, true);
			}
		} else {
			//..
		}
		aSource->setLastBundle(Util::emptyString);
	}
}

void DownloadManager::disconnectBundle(BundlePtr aBundle, const UserPtr& aUser) {
	//UserConnectionList u;
	{
		Lock l(cs);
		for(DownloadList::const_iterator i = downloads.begin(); i != downloads.end(); ++i) {
			Download* d = *i;
			if (aUser) {
				if (d->getUser() != aUser) {
					continue;
				}
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
