/* 
 * Copyright (C) 2001-2022 Jacek Sieka, arnetheduck on gmail point com
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

#include "ClientManager.h"
#include "ConnectionManager.h"
#include "Download.h"
#include "DownloadManager.h"
#include "FavoriteManager.h"
#include "HashManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "User.h"
#include "UserConnection.h"

#include <limits>
#include <cmath>

#include <boost/range/numeric.hpp>


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
			WLock l(cs);
			if(downloads.empty())
				break;
		}
		Thread::sleep(100);
	}
}

struct DropInfo {
	DropInfo(const string& aTarget, const BundlePtr& aBundle, const UserPtr& aUser) : bundle(aBundle), user(aUser), target(aTarget) { } 

	BundlePtr bundle;
	string target;
	UserPtr user;
};

void DownloadManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	vector<DropInfo> dropTargets;
	vector<pair<CID, AdcCommand>> UBNList;
	BundleList bundleTicks;
	unordered_map<UserPtr, int64_t, User::Hash> userSpeedMap;

	{
		RLock l(cs);
		for (auto& b : bundles | map_values) {
			if (b->onDownloadTick(UBNList)) {
				bundleTicks.push_back(b);
			}
		}

		DownloadList tickList;
		// Tick each ongoing download
		for(auto d: downloads) {
			auto speed = d->getAverageSpeed();

			if(d->getPos() > 0) {
				userSpeedMap[d->getUser()] += speed;
				tickList.push_back(d);
				d->tick();
			}

			if (d->getBundle() && d->getBundle()->isSet(Bundle::FLAG_AUTODROP) && d->getStart() > 0 && (int)d->getBundle()->getRunningUsers().size() >= SETTING(DISCONNECT_MIN_SOURCES))
			{
				if (d->getTigerTree().getFileSize() > (SETTING(DISCONNECT_FILESIZE) * 1048576))
				{
					if(speed < Util::convertSize(SETTING(DISCONNECT_SPEED), Util::KB))
					{
						if(aTick - d->getLastTick() > (uint32_t)SETTING(DISCONNECT_TIME) * 1000)
						{
							if(QueueManager::getInstance()->checkDropSlowSource(d))
							{
								dropTargets.emplace_back(d->getPath(), d->getBundle(), d->getUser());
							}
						}
					} else {
						d->setLastTick(aTick);
					}
				}
			}

			if(SETTING(FAV_DL_SPEED) > 0) {
				HintedUser fstusr = d->getHintedUser();
				if(speed > Util::convertSize(SETTING(FAV_DL_SPEED), Util::KB) && (aTick - d->getStart()) > 7000 && !fstusr.user->isFavorite()) {
					FavoriteManager::getInstance()->addFavoriteUser(fstusr);
					FavoriteManager::getInstance()->setUserDescription(fstusr, ("!fast user! (" + Util::toString(getRunningAverage()/1000) + "KB/s)"));
				}
			}
		}

		// Statistics
		int64_t totalDown = Socket::getTotalDown();
		int64_t totalUp = Socket::getTotalUp();

		int64_t diff = (int64_t)((lastUpdate == 0) ? aTick - 1000 : aTick - lastUpdate);
		int64_t updiff = totalUp - lastUpBytes;
		int64_t downdiff = totalDown - lastDownBytes;

		lastDownSpeed = diff > 0 ? (downdiff * 1000LL / diff) : 0;
		lastUpSpeed = diff > 0 ? (updiff * 1000LL / diff) : 0;

		SettingsManager::getInstance()->set(SettingsManager::TOTAL_UPLOAD, SETTING(TOTAL_UPLOAD) + updiff);
		SettingsManager::getInstance()->set(SettingsManager::TOTAL_DOWNLOAD, SETTING(TOTAL_DOWNLOAD) + downdiff);

		lastUpdate = aTick;
		lastUpBytes = totalUp;
		lastDownBytes = totalDown;

		if(!tickList.empty()) {
			fire(DownloadManagerListener::Tick(), tickList);
		}

		if (!bundleTicks.empty()) {
			fire(DownloadManagerListener::BundleTick(), bundleTicks, aTick);
		}
	}

	for (auto& usp: userSpeedMap)
		usp.first->setSpeed(usp.second);

	for(auto& ubp: UBNList)
		ClientManager::getInstance()->sendUDP(ubp.second, ubp.first, true, true);

	for (auto& dtp: dropTargets)
		QueueManager::getInstance()->handleSlowDisconnect(dtp.user, dtp.target, dtp.bundle);
}

void DownloadManager::sendSizeUpdate(const BundlePtr& aBundle) const noexcept {
	RLock l (cs);
	aBundle->sendSizeUpdate();
}

void DownloadManager::startBundle(UserConnection* aSource, BundlePtr aBundle) {
	if (aSource->getLastBundle().empty() || aSource->getLastBundle() != Util::toString(aBundle->getToken())) {
		if (!aSource->getLastBundle().empty()) {
			removeRunningUser(aSource);
		} 

		{
			WLock l (cs);
			if (aBundle->addRunningUser(aSource)) {
				//this is the first running user for this bundle
				aBundle->setStart(GET_TICK());
				bundles[aBundle->getToken()] = aBundle;
			}
		}
		aSource->setLastBundle(aBundle->getStringToken());
	}
}

bool DownloadManager::checkIdle(const UserPtr& user, bool smallSlot, bool reportOnly) {

	RLock l(cs);
	for (auto uc: idlers) {
		if (uc->getUser() == user) {
			if (smallSlot != uc->isSet(UserConnection::FLAG_SMALL_SLOT) && uc->isSet(UserConnection::FLAG_MCN1))
				continue;
			if (!reportOnly)
				uc->callAsync([this, uc] { revive(uc); });
			//dcdebug("uc updated");
			return true;
		}	
	}
	return false;
}

void DownloadManager::revive(UserConnection* uc) {
	{
		WLock l(cs);
		auto i = find(idlers.begin(), idlers.end(), uc);
		if(i == idlers.end())
			return;
		idlers.erase(i);
	}

	checkDownloads(uc);
}

void DownloadManager::addConnection(UserConnection* conn) {
	if (!conn->isSet(UserConnection::FLAG_SUPPORTS_TTHF) || !conn->isSet(UserConnection::FLAG_SUPPORTS_ADCGET)) {
		// Can't download from these...
		conn->getUser()->setFlag(User::OLD_CLIENT);
		QueueManager::getInstance()->removeSource(conn->getUser(), QueueItem::Source::FLAG_NO_TTHF);
		conn->disconnect();
		return;
	}


	conn->addListener(this);
	checkDownloads(conn);
}

void DownloadManager::getRunningBundles(QueueTokenSet& bundles_) const noexcept {
	RLock l(cs);
	for (const auto& b : bundles | map_values) {
		// we need to check this to ignore previous bundles for running connections 
		// (non-running bundles are removed only when no next download was found)
		if (b->getDownloads().empty())
			continue;
		
		// these won't be included in the running bundle limit
		if (b->getPriority() == Priority::HIGHEST)
			continue;
		if (all_of(b->getDownloads().begin(), b->getDownloads().end(), Flags::IsSet(Download::FLAG_HIGHEST_PRIO)))
			continue;

		bundles_.insert(b->getToken());
	}
}

size_t DownloadManager::getRunningBundleCount() const noexcept {
	RLock l(cs);
	auto ret = accumulate(bundles | map_values, (size_t)0, [&](size_t old, const BundlePtr& b) {
		if (b->getSpeed() == 0) {
			return old;
		}

		return old + 1;
	});

	return ret;
}

void DownloadManager::checkDownloads(UserConnection* aConn) {
	//We may have download assigned for a connection if we are downloading in segments
	//dcassert(!aConn->getDownload() || aConn->getDownload()->isSet(Download::FLAG_CHUNKED));

	QueueItemBase::DownloadType dlType = QueueItemBase::TYPE_ANY;
	if (aConn->isSet(UserConnection::FLAG_SMALL_SLOT)) {
		dlType = QueueItemBase::TYPE_SMALL;
	} else if (aConn->isSet(UserConnection::FLAG_MCN1)) {
		dlType = QueueItemBase::TYPE_MCN_NORMAL;
	}

	auto hubs = ClientManager::getInstance()->getHubSet(aConn->getUser()->getCID());

	//always make sure that the current hub is also compared even if it is offline
	hubs.insert(aConn->getHubUrl());

	string errorMessage;
	QueueTokenSet runningBundles;
	getRunningBundles(runningBundles);

	bool start = QueueManager::getInstance()->startDownload(aConn->getHintedUser(), runningBundles, hubs, dlType, aConn->getSpeed(), errorMessage);

	// not a finished download?
	if(!start && aConn->getState() != UserConnection::STATE_RUNNING) {
		removeRunningUser(aConn);
		removeConnection(aConn);
		return;
	}

	string newUrl;
	Download* d = nullptr;

	if (start)
		d = QueueManager::getInstance()->getDownload(*aConn, runningBundles, hubs, errorMessage, newUrl, dlType);

	if(!d) {
		//aConn->setDownload(nullptr);
		aConn->unsetFlag(UserConnection::FLAG_RUNNING);
		if(!errorMessage.empty()) {
			fire(DownloadManagerListener::Status(), aConn, errorMessage);
		}

		if (!checkIdle(aConn->getUser(), aConn->isSet(UserConnection::FLAG_SMALL_SLOT), true)) {
			aConn->setState(UserConnection::STATE_IDLE);
			removeRunningUser(aConn);
			{
				WLock l(cs);
 				idlers.push_back(aConn);
			}
		} else {
			aConn->disconnect(true);
		}
		return;
	}

	/*
	Find mySID, better ways to get the correct one transferred here?
	the hinturl of the connection is updated to the hub where the connection request is coming from,
	so we should be able to find our own SID by finding the hub where the user is at (if we have a hint).
	*/

	string mySID;
	if(!aConn->getUser()->isNMDC()) {
		mySID = ClientManager::getInstance()->findMySID(aConn->getUser(), newUrl, false); //no fallback, keep the old hint even if the hub is offline
	}

	aConn->setState(UserConnection::STATE_SND);
	
	if(aConn->isSet(UserConnection::FLAG_SUPPORTS_XML_BZLIST) && d->getType() == Transfer::TYPE_FULL_LIST) {
		d->setFlag(Download::FLAG_XML_BZ_LIST);
	}
	
	{
		WLock l(cs);
		downloads.push_back(d);
		BundlePtr b = d->getBundle();
		if (b) {
			b->addDownload(d);
		}
	}

	dcdebug("Requesting " I64_FMT "/" I64_FMT "\n", d->getStartPos(), d->getSegmentSize());

	//only update the hub if it has been changed
	if (compare(newUrl, aConn->getHubUrl()) == 0) {
		mySID.clear();
	} else if (!newUrl.empty()) {
		aConn->setHubUrl(newUrl);
	}

	fire(DownloadManagerListener::Requesting(), d, !mySID.empty());
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
	dcassert(d);

	dcdebug("Preparing " I64_FMT ":" I64_FMT ", " I64_FMT ":" I64_FMT"\n", d->getStartPos(), start, d->getSegmentSize(), bytes);
	if (d->getSegmentSize() == -1) {
		if(bytes >= 0) {
			d->setSegmentSize(bytes);
			if ((d->getType() == Download::TYPE_PARTIAL_LIST) || (d->getType() == Download::TYPE_FULL_LIST))
				QueueManager::getInstance()->setFileListSize(d->getPath(), bytes);
		} else {
			failDownload(aSource, STRING(INVALID_SIZE), true);
			return;
		}
	} else if (d->getSegmentSize() != bytes || d->getStartPos() != start) {
		// This is not what we requested...
		failDownload(aSource, STRING(INVALID_SIZE), true);
		return;
	}
	
	try {
		auto hasDownloadedBytes = QueueManager::getInstance()->hasDownloadedBytes(d->getPath());

		{
			RLock l (cs);
			d->open(bytes, z, hasDownloadedBytes);
		}
	} catch(const FileException& e) {
		QueueManager::getInstance()->onDownloadError(d->getBundle(), e.getError());
	
		failDownload(aSource, STRING(COULD_NOT_OPEN_TARGET_FILE) + " " + e.getError(), true);
		return;
	} catch(const Exception& e) {
		failDownload(aSource, e.getError(), true);
		return;
	}

	d->setStart(GET_TICK());
	d->tick();
	if (!aSource->isSet(UserConnection::FLAG_RUNNING) && aSource->isSet(UserConnection::FLAG_MCN1) && (d->getType() == Download::TYPE_FILE || d->getType() == Download::TYPE_PARTIAL_LIST)) {
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

	if (d->getPos() == d->getSegmentSize()) {
		try {
			// Already finished? A zero-byte file list could cause this...
			endData(aSource);
		} catch(const Exception& e) {
			failDownload(aSource, e.getError(), true);
		}
	} else {
		aSource->setDataMode();
	}
}

void DownloadManager::on(UserConnectionListener::Data, UserConnection* aSource, const uint8_t* aData, size_t aLen) noexcept {
	Download* d = aSource->getDownload();
	if (!d) //No download but receiving data??
		aSource->disconnect(true);

	try {
		d->addPos(d->getOutput()->write(aData, aLen), aLen);
		d->tick();

		if(d->getOutput()->eof()) {
			endData(aSource);
			aSource->setLineMode(0);
		}
	} catch(const Exception& e) {

		//TTH inconsistency, do we get other errors here?
		if (e.getErrorCode() == Exception::TTH_INCONSISTENCY) {
			QueueManager::getInstance()->removeFileSource(d->getPath(), aSource->getUser(), QueueItem::Source::FLAG_TTH_INCONSISTENCY, false);
			//Pause temporarily to give other bundles a chance to get downloaded, this one wont complete anyway...
			//Might be enough to just remove this source? 
			QueueManager::getInstance()->onDownloadError(d->getBundle(), e.getError()); 
			//d->resetPos(); // is there a better way than resetting the position?
		}

		failDownload(aSource, e.getError(), true);
	}
}

/** Download finished! */
void DownloadManager::endData(UserConnection* aSource) {
	dcassert(aSource->getState() == UserConnection::STATE_RUNNING);
	Download* d = aSource->getDownload();
	dcassert(d);

	if(d->getType() == Transfer::TYPE_TREE) {
		d->getOutput()->flushBuffers(false);

		int64_t bl = 1024;
		while(bl * (int64_t)d->getTigerTree().getLeaves().size() < d->getTigerTree().getFileSize())
			bl *= 2;
		d->getTigerTree().setBlockSize(bl);
		d->getTigerTree().calcRoot();

		if(!(d->getTTH() == d->getTigerTree().getRoot())) {
			// This tree is for a different file, remove from queue...
			removeDownload(d);
			fire(DownloadManagerListener::Failed(), d, STRING(INVALID_TREE));

			QueueManager::getInstance()->removeFileSource(d->getPath(), aSource->getUser(), QueueItem::Source::FLAG_BAD_TREE, false);
			QueueManager::getInstance()->putDownloadHooked(d, false);

			checkDownloads(aSource);
			return;
		}
		d->setTreeValid(true);
	} else {
		aSource->setSpeed(static_cast<int64_t>(d->getAverageSpeed()));
		aSource->updateChunkSize(d->getTigerTree().getBlockSize(), d->getSegmentSize(), GET_TICK() - d->getStart());
		
		dcdebug("Download finished: %s, size " I64_FMT ", downloaded " I64_FMT " in " U64_FMT " ms\n", d->getPath().c_str(), d->getSegmentSize(), d->getPos(), GET_TICK() - d->getStart());
	}

	removeDownload(d);

	fire(DownloadManagerListener::Complete(), d, d->getType() == Transfer::TYPE_TREE);
	try {
		QueueManager::getInstance()->putDownloadHooked(d, true);
	} catch (const HashException& e) {
		removeRunningUser(aSource);
		removeConnection(aSource);
		ConnectionManager::getInstance()->failDownload(aSource->getToken(), e.getError(), true);
		return;
	}

	checkDownloads(aSource);
}

int64_t DownloadManager::getRunningAverage() const {
	RLock l(cs);
	int64_t avg = 0;
	for(auto d: downloads)
		avg += static_cast<int64_t>(d->getAverageSpeed());

	return avg;
}

size_t DownloadManager::getTotalDownloadConnectionCount() const noexcept {
	RLock l(cs);
	return downloads.size();
}

size_t DownloadManager::getFileDownloadConnectionCount() const noexcept {
	RLock l(cs);
	return std::accumulate(downloads.begin(), downloads.end(), static_cast<size_t>(0), [](size_t aOld, const Download* aDownload) {
		return aDownload->getUserConnection().isSet(UserConnection::FLAG_SMALL_SLOT) ? aOld : aOld + 1;
	});
}

size_t DownloadManager::getBundleDownloadConnectionCount(const BundlePtr& aBundle) const noexcept {
	RLock l(cs);
	return aBundle->getDownloads().size();
}

void DownloadManager::on(UserConnectionListener::MaxedOut, UserConnection* aSource, const string& param) noexcept {
	noSlots(aSource, param);
}

void DownloadManager::noSlots(UserConnection* aSource, const string& param) {
	if(aSource->getState() != UserConnection::STATE_SND) {
		dcdebug("DM::noSlots Bad state, disconnecting\n");
		aSource->disconnect();
		return;
	}

	string extra = param.empty() ? Util::emptyString : " - " + STRING(QUEUED) + ": " + param;
	failDownload(aSource, STRING(NO_SLOTS_AVAILABLE) + extra, false);
}

void DownloadManager::onFailed(UserConnection* aSource, const string& aError) {
	{
		WLock l(cs);
 		idlers.erase(remove(idlers.begin(), idlers.end(), aSource), idlers.end());
	}
	failDownload(aSource, aError, false);
}

void DownloadManager::failDownload(UserConnection* aSource, const string& reason, bool rotateQueue) {
	Download* d = aSource->getDownload();
	if(d) {
		removeDownload(d);
		fire(DownloadManagerListener::Failed(), d, reason);
		QueueManager::getInstance()->putDownloadHooked(d, false, false, rotateQueue);
	}

	removeRunningUser(aSource);
	removeConnection(aSource);
}

void DownloadManager::removeConnection(UserConnectionPtr aConn) {
	dcassert(!aConn->getDownload());
	aConn->removeListener(this);
	aConn->disconnect();
}

void DownloadManager::removeDownload(Download* d) {
	// Write the leftover bytes into file
	if(d->getOutput()) {
		if(d->getActual() > 0) {
			try {
				d->getOutput()->flushBuffers(false);
			} catch(const Exception&) {
			}
		}
	}

	{
		WLock l(cs);

		BundlePtr bundle = d->getBundle();
		if (bundle) {
			bundle->removeDownload(d);
		}

		dcassert(find(downloads.begin(), downloads.end(), d) != downloads.end());
		downloads.erase(remove(downloads.begin(), downloads.end(), d), downloads.end());
	}
}

BundlePtr DownloadManager::findRunningBundle(QueueToken aBundleToken) const noexcept {
	auto s = bundles.find(aBundleToken);
	if (s != bundles.end()) {
		return s->second;
	}
	return nullptr;
}

void DownloadManager::removeRunningUser(UserConnection* aSource, bool sendRemove /*false*/) noexcept {
	if (aSource->getLastBundle().empty()) {
		return;
	}

	{
		WLock l (cs);
		BundlePtr bundle = findRunningBundle(Util::toUInt32(aSource->getLastBundle()));
		if (bundle && bundle->removeRunningUser(aSource, sendRemove)) {
			//no running users for this bundle
			bundles.erase(bundle->getToken());
			fire(DownloadManagerListener::BundleWaiting(), bundle);
		}
	}

	aSource->setLastBundle(Util::emptyString);
}

void DownloadManager::disconnectBundle(const BundlePtr& aBundle, const UserPtr& aUser) {
	//UserConnectionList u;
	{
		RLock l(cs);
		for(auto d: aBundle->getDownloads()) {
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
	RLock l(cs);
	
	for(auto d: downloads) {
		if(d->getPath() == aTarget) {
			if (aUser) {
				if (d->getUser() != aUser) {
					continue;
				}
			}
			dcdebug("Trying to close connection for download %p\n", d);
			d->getUserConnection().disconnect(true);
		}
	}
}

void DownloadManager::on(UserConnectionListener::FileNotAvailable, UserConnection* aSource) noexcept {
	if(!aSource->getDownload()) {
		aSource->disconnect(true);
		return;
	}
	fileNotAvailable(aSource, false);
}

/** @todo Handle errors better */
void DownloadManager::on(AdcCommand::STA, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(cmd.getParameters().size() < 2) {
		aSource->disconnect();
		return;
	}

	const string& errorCode = cmd.getParam(0);
	const string& errorMessage = cmd.getParam(1);
	if(errorCode.length() != 3) {
		aSource->disconnect();
		return;
	}

	switch(Util::toInt(errorCode.substr(0, 1))) {
		case AdcCommand::SEV_FATAL:
			aSource->disconnect();
			return;
		case AdcCommand::SEV_RECOVERABLE:
			switch(Util::toInt(errorCode.substr(1))) {
				case AdcCommand::ERROR_FILE_NOT_AVAILABLE:
					fileNotAvailable(aSource, false, errorMessage);
					return;
				case AdcCommand::ERROR_SLOTS_FULL:
					{
						string param;
						noSlots(aSource, cmd.getParam("QP", 0, param) ? param : Util::emptyString);
						return;
					}
				case AdcCommand::ERROR_FILE_ACCESS_DENIED:
					fileNotAvailable(aSource, true);
					return;
				case AdcCommand::ERROR_UNKNOWN_USER:
					failDownload(aSource, STRING(UNKNOWN_USER), !aSource->getDownload()->isFilelist());
					return;
			}
		case AdcCommand::SEV_SUCCESS:
			// We don't know any messages that would give us these...
			dcdebug("Unknown success message %s %s", errorCode.c_str(), errorMessage.c_str());
			return;
	}
	aSource->disconnect();
}

void DownloadManager::fileNotAvailable(UserConnection* aSource, bool aNoAccess, const string& aMessage) {
	if(aSource->getState() != UserConnection::STATE_SND) {
		dcdebug("DM::fileNotAvailable Invalid state, disconnecting");
		aSource->disconnect();
		return;
	}
	
	Download* d = aSource->getDownload();
	dcassert(d);
	dcdebug("File Not Available: %s\n", d->getPath().c_str());

	removeDownload(d);
	removeRunningUser(aSource);

	string error;
	if (aNoAccess) {
		error = STRING(NO_FILE_ACCESS);
	} else {
		error = d->getType() == Transfer::TYPE_TREE ? STRING(NO_FULL_TREE) : STRING(FILE_NOT_AVAILABLE);
		if (d->getType() == Transfer::TYPE_PARTIAL_LIST && aSource->isSet(UserConnection::FLAG_NMDC)) {
			error += " / " + STRING(NO_PARTIAL_SUPPORT);
		} else if (!aMessage.empty() && aMessage != UserConnection::FILE_NOT_AVAILABLE) {
			error += " (" + aMessage + ")";
		}
	}

	fire(DownloadManagerListener::Failed(), d, error);
	if (!aNoAccess) {
		QueueManager::getInstance()->removeFileSource(d->getPath(), aSource->getUser(), (Flags::MaskType)(d->getType() == Transfer::TYPE_TREE ? QueueItem::Source::FLAG_NO_TREE : QueueItem::Source::FLAG_FILE_NOT_AVAILABLE), false);
	}

	QueueManager::getInstance()->putDownloadHooked(d, false, aNoAccess);
	checkDownloads(aSource);
}

} // namespace dcpp
