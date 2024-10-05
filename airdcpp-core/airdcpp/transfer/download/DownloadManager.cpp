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
#include <airdcpp/transfer/download/DownloadManager.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/transfer/download/Download.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/user/User.h>
#include <airdcpp/connection/UserConnection.h>

#include <limits>
#include <cmath>

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
	DropInfo(const string& aTarget, const BundlePtr& aBundle, const UserPtr& aUser) : bundle(aBundle), target(aTarget), user(aUser) { } 

	BundlePtr bundle;
	string target;
	UserPtr user;
};


bool DownloadManager::disconnectSlowSpeed(Download* d, uint64_t aTick) const noexcept {
	if (d->getBundle() && d->getBundle()->isSet(Bundle::FLAG_AUTODROP) && d->getStart() > 0 && d->getBundle()->countRunningUsers() >= SETTING(DISCONNECT_MIN_SOURCES))
	{
		if (d->getTigerTree().getFileSize() > (SETTING(DISCONNECT_FILESIZE) * 1048576))
		{
			if (d->getAverageSpeed() < Util::convertSize(SETTING(DISCONNECT_SPEED), Util::KB))
			{
				if (aTick - d->getLastTick() > static_cast<uint64_t>(SETTING(DISCONNECT_TIME) * 1000))
				{
					if (QueueManager::getInstance()->checkDropSlowSource(d))
					{
						return true;
					}
				}
			} else {
				d->setLastTick(aTick);
			}
		}
	}

	return false;
}

using UserSpeedMap = unordered_map<UserPtr, int64_t, User::Hash>;
void DownloadManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	vector<DropInfo> dropTargets;
	BundleList bundleTicks;
	UserSpeedMap userSpeedMap;

	{
		RLock l(cs);

		DownloadList tickList;
		// Tick each ongoing download
		for(auto d: downloads) {
			if (d->getPos() > 0) {
				userSpeedMap[d->getUser()] += d->getAverageSpeed();
				tickList.push_back(d);
				d->tick();

				if (d->getBundle() && d->getBundle()->onDownloadTick()) {
					bundleTicks.push_back(d->getBundle());
				}
			}

			if (disconnectSlowSpeed(d, aTick)) {
				dropTargets.emplace_back(d->getPath(), d->getBundle(), d->getUser());
			}
		}

		// Statistics
		int64_t totalDown = Socket::getTotalDown();
		int64_t totalUp = Socket::getTotalUp();

		auto diff = (int64_t)((lastUpdate == 0) ? aTick - 1000 : aTick - lastUpdate);
		int64_t updiff = totalUp - lastUpBytes;
		int64_t downdiff = totalDown - lastDownBytes;

		lastDownSpeed = diff > 0 ? (downdiff * 1000LL / diff) : 0;
		lastUpSpeed = diff > 0 ? (updiff * 1000LL / diff) : 0;

		SettingsManager::getInstance()->set(SettingsManager::TOTAL_UPLOAD, SETTING(TOTAL_UPLOAD) + updiff);
		SettingsManager::getInstance()->set(SettingsManager::TOTAL_DOWNLOAD, SETTING(TOTAL_DOWNLOAD) + downdiff);

		lastUpdate = aTick;
		lastUpBytes = totalUp;
		lastDownBytes = totalDown;

		if (!tickList.empty()) {
			fire(DownloadManagerListener::Tick(), tickList, aTick);
		}

		if (!bundleTicks.empty()) {
			fire(DownloadManagerListener::BundleTick(), bundleTicks, aTick);
		}
	}

	for (const auto& [user, speed] : userSpeedMap)
		user->setSpeed(speed);

	for (auto& dtp: dropTargets)
		QueueManager::getInstance()->handleSlowDisconnect(dtp.user, dtp.target, dtp.bundle);
}

bool DownloadManager::checkIdle(const string& aToken) {
	RLock l(cs);
	for (auto uc : idlers) {
		if (uc->getToken() == aToken) {
			uc->callAsync([this, uc] { revive(uc); });
			return true;
		}
	}
	return false;
}

bool DownloadManager::checkIdle(const UserPtr& aUser, bool aSmallSlot) {

	RLock l(cs);
	for (auto uc: idlers) {
		if (uc->getUser() == aUser) {
			if (aSmallSlot != uc->isSet(UserConnection::FLAG_SMALL_SLOT) && uc->isMCN())
				continue;

			uc->callAsync([this, uc] { revive(uc); });
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

void DownloadManager::addConnection(UserConnection* aSource) {
	if (!aSource->isSet(UserConnection::FLAG_SUPPORTS_TTHF) || !aSource->isSet(UserConnection::FLAG_SUPPORTS_ADCGET)) {
		// Can't download from these...
		aSource->getUser()->setFlag(User::OLD_CLIENT);
		QueueManager::getInstance()->removeSource(aSource->getUser(), QueueItem::Source::FLAG_NO_TTHF);
		dcdebug("DownloadManager::addConnection: outdated user (%s)\n", aSource->getToken().c_str());
		disconnect(aSource);
		return;
	}


	aSource->addListener(this);
	checkDownloads(aSource);
}

QueueTokenSet DownloadManager::getRunningBundles(bool aIgnoreHighestPrio) const noexcept {
	QueueTokenSet bundleTokens;

	RLock l(cs);
	for (const auto& d : downloads) {
		const auto& b = d->getBundle();
		if (!b) {
			continue;
		}
		
		// these won't be included in the running bundle limit
		if (aIgnoreHighestPrio) {
			if (b->getPriority() == Priority::HIGHEST)
				continue;
			if (d->isSet(Download::FLAG_HIGHEST_PRIO)) {
				continue;
			}
		}

		bundleTokens.insert(b->getToken());
	}

	return bundleTokens;
}

size_t DownloadManager::getRunningBundleCount() const noexcept {
	return getRunningBundles(false).size();
}

string DownloadManager::updateConnectionHubUrl(UserConnection* aSource, const string& aNewHubUrl) const noexcept {
	if (aSource->getUser()->isNMDC()) {
		return Util::emptyString;
	}

	auto hubChanged = compare(aNewHubUrl, aSource->getHubUrl()) != 0;
	if (!hubChanged) {
		return Util::emptyString;
	}

	auto c = ClientManager::getInstance()->findClient(aNewHubUrl);
	if (!c || !c->isConnected()) {
		// No fallback, keep the old hint even if the hub is offline
		return Util::emptyString;
	}

	aSource->setHubUrl(aNewHubUrl);
	return c->getMyIdentity().getSIDString();
}

void DownloadManager::checkDownloads(UserConnection* aConn) {
	//We may have download assigned for a connection if we are downloading in segments
	//dcassert(!aConn->getDownload() || aConn->getDownload()->isSet(Download::FLAG_CHUNKED));

	auto hubs = ClientManager::getInstance()->getHubSet(aConn->getUser()->getCID());

	// always make sure that the current hub is also compared even if it is offline
	hubs.insert(aConn->getHubUrl());

	auto runningBundles = getRunningBundles();

	auto result = QueueManager::getInstance()->getDownload(*aConn, runningBundles, hubs);

	// Nothing to download? Skip finished download connections as they should be added in idlers
	if (!result.hasDownload) {
		dcdebug("DownloadManager::checkDownloads: no downloads from user %s (small slot: %s)\n", ClientManager::getInstance()->getFormattedNicks(aConn->getHintedUser()).c_str(), aConn->isSet(UserConnection::FLAG_SMALL_SLOT) ? "true" : "false");
		if (aConn->getState() != UserConnection::STATE_RUNNING) {
			failDownload(aConn, Util::emptyString, false);
			return;
		}
	}

	auto d = result.download;
	if (!d) {
		if (result.hasDownload) {
			dcdebug("DownloadManager::checkDownloads: can't start download from user %s (%s)\n", ClientManager::getInstance()->getFormattedNicks(aConn->getHintedUser()).c_str(), result.lastError.c_str());
		}

		aConn->setState(UserConnection::STATE_IDLE);
		fire(DownloadManagerListener::Idle(), aConn, result.lastError);

		{
			WLock l(cs);
			idlers.push_back(aConn);
		}

		return;
	}

	aConn->setState(UserConnection::STATE_SND);
	
	if(aConn->isSet(UserConnection::FLAG_SUPPORTS_XML_BZLIST) && d->getType() == Transfer::TYPE_FULL_LIST) {
		d->setFlag(Download::FLAG_XML_BZ_LIST);
	}
	
	{
		WLock l(cs);
		downloads.push_back(d);
		auto b = d->getBundle();
		if (b) {
			b->addDownload(d);
		}
	}

	dcdebug("DownloadManager::checkDownloads: requesting " I64_FMT "/" I64_FMT " (connection %s)\n", d->getStartPos(), d->getSegmentSize(), d->getConnectionToken().c_str());

	// We need to let the other user know in case we want to download hub-specific
	// items (such as file lists) via a different hub. Use the SID as there is no global hub ID
	auto mySID = updateConnectionHubUrl(aConn, result.hubHint);

	dcassert(aConn->getDownload());
	fire(DownloadManagerListener::Requesting(), d, !mySID.empty());
	aConn->send(d->getCommand(aConn->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET), mySID));
}

void DownloadManager::on(AdcCommand::SND, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(aSource->getState() != UserConnection::STATE_SND) {
		dcdebug("DM::onFileLength Bad state, ignoring\n");
		return;
	}

	if(!aSource->getDownload()) {
		dcdebug("DownloadManager::AdcCommand::SND: no download (%s)\n", aSource->getToken().c_str());
		disconnect(aSource, true);
		return;
	}

	const string& type = cmd.getParam(0);
	int64_t start = Util::toInt64(cmd.getParam(2));
	int64_t bytes = Util::toInt64(cmd.getParam(3));

	if(cmd.hasFlag("TL", 4))	 
		aSource->getDownload()->setFlag(Download::FLAG_TTHLIST);
	
	if(type != Transfer::names[aSource->getDownload()->getType()]) {
		// Uhh??? We didn't ask for this...
		dcdebug("DownloadManager::AdcCommand::SND: transfer type mismatch (%s)\n", aSource->getToken().c_str());
		disconnect(aSource);
		return;
	}

	startData(aSource, start, bytes, cmd.hasFlag("ZL", 4));
}

void DownloadManager::startData(UserConnection* aSource, int64_t start, int64_t bytes, bool z) {
	Download* d = aSource->getDownload();
	dcassert(d);

	dcdebug("DownloadManager::startData: preparing " I64_FMT ":" I64_FMT ", " I64_FMT ":" I64_FMT"\n", d->getStartPos(), start, d->getSegmentSize(), bytes);
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
	aSource->setState(UserConnection::STATE_RUNNING);

	fire(DownloadManagerListener::Starting(), d);

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
	if (!d) {
		//No download but receiving data??
		dcassert(0);
		dcdebug("DownloadManager::UserConnectionListener::Data: no download (%s)\n", aSource->getToken().c_str());
		disconnect(aSource, true);
		return;
	}

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
			fire(DownloadManagerListener::Failed(), d, STRING(INVALID_TREE));

			QueueManager::getInstance()->removeFileSource(d->getPath(), aSource->getUser(), QueueItem::Source::FLAG_BAD_TREE, false);

			dcdebug("DownloadManager::endData: invalid tree received from user %s (received %s while %s was excpected)\n", ClientManager::getInstance()->getFormattedNicks(d->getHintedUser()).c_str(), d->getTTH().toBase32().c_str(), d->getTigerTree().getRoot().toBase32().c_str());

			removeDownload(d);
			QueueManager::getInstance()->putDownloadHooked(d, false);
			checkDownloads(aSource);
			return;
		}
		d->setTreeValid(true);
	} else {
		aSource->setSpeed(d->getAverageSpeed());
		aSource->updateChunkSize(d->getTigerTree().getBlockSize(), d->getSegmentSize(), GET_TICK() - d->getStart());
		
		dcdebug("DownloadManager::endData: %s (connection %s), size " I64_FMT ", downloaded " I64_FMT " in " U64_FMT " ms\n", d->getPath().c_str(), d->getConnectionToken().c_str(), d->getSegmentSize(), d->getPos(), GET_TICK() - d->getStart());
	}

	fire(DownloadManagerListener::Complete(), d, d->getType() == Transfer::TYPE_TREE);
	removeDownload(d);

	try {
		QueueManager::getInstance()->putDownloadHooked(d, true);
	} catch (const HashException& e) {
		dcdebug("DownloadManager::endData: could not save tree into hash database, removing connection (%s)\n", e.getError().c_str());
		removeConnection(aSource);
		return;
	}

	checkDownloads(aSource);
}

int64_t DownloadManager::getRunningAverage() const {
	RLock l(cs);
	int64_t avg = 0;
	for(auto d: downloads)
		avg += d->getAverageSpeed();

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
		disconnect(aSource);
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

void DownloadManager::failDownload(UserConnection* aSource, const string& aReason, bool aRotateQueue) {
	auto d = aSource->getDownload();
	if (d) {
		dcdebug("DownloadManager::failDownload: %s failed (%s)\n", aSource->getToken().c_str(), aReason.c_str());
		fire(DownloadManagerListener::Failed(), d, aReason);
		removeDownload(d);
		QueueManager::getInstance()->putDownloadHooked(d, false, false, aRotateQueue);
	} else {
		fire(DownloadManagerListener::Remove(), aSource);
	}

	removeConnection(aSource);
}

void DownloadManager::removeConnection(UserConnectionPtr aConn) {
	dcassert(!aConn->getDownload());
	aConn->removeListener(this);
	aConn->disconnect();
}

void DownloadManager::disconnect(UserConnectionPtr aConn, bool aGraceless) const noexcept {
	dcdebug("DownloadManager::disconnect: %s (graceless: %s)\n", aConn->getToken().c_str(), aGraceless ? "true" : "false");
	aConn->disconnect(aGraceless);
}

void DownloadManager::removeDownload(Download* d) {
	// Write the leftover bytes into file
	d->flush();

	{
		WLock l(cs);

		if (auto& bundle = d->getBundle(); bundle) {
			bundle->removeDownload(d);
		}

		dcassert(find(downloads.begin(), downloads.end(), d) != downloads.end());
		downloads.erase(remove(downloads.begin(), downloads.end(), d), downloads.end());
	}
}

void DownloadManager::disconnectBundle(const BundlePtr& aBundle, const UserPtr& aUser) {
	RLock l(cs);
	for(auto d: aBundle->getDownloads()) {
		if (aUser && d->getUser() != aUser) {
			continue;
		}
		d->getUserConnection().disconnect(true);
	}
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
			dcdebug("DownloadManager::abortDownload: trying to disconnect %s\n", d->getConnectionToken().c_str());
			d->getUserConnection().disconnect(true);
		}
	}
}

void DownloadManager::on(UserConnectionListener::FileNotAvailable, UserConnection* aSource) noexcept {
	if(!aSource->getDownload()) {
		dcdebug("DM::FileNotAvailable: no download (%s)\n", aSource->getToken().c_str());
		disconnect(aSource, true);
		return;
	}
	fileNotAvailable(aSource, false);
}

/** @todo Handle errors better */
void DownloadManager::on(AdcCommand::STA, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(cmd.getParameters().size() < 2) {
		dcdebug("DM::AdcCommand::STA: not enough parameters (%s)\n", aSource->getToken().c_str());
		disconnect(aSource);
		return;
	}

	const string& errorCode = cmd.getParam(0);
	const string& errorMessage = cmd.getParam(1);
	if(errorCode.length() != 3) {
		dcdebug("DM::AdcCommand::STA: invalid error code (%s)\n", aSource->getToken().c_str());
		disconnect(aSource);
		return;
	}

	switch(Util::toInt(errorCode.substr(0, 1))) {
		case AdcCommand::SEV_FATAL:
			dcdebug("DM::AdcCommand::STA: fatal error (%s)\n", aSource->getToken().c_str());
			disconnect(aSource);
			return;
		[[fallthrough]];
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
				default: {
					//..
				}
			}
		case AdcCommand::SEV_SUCCESS:
			// We don't know any messages that would give us these...
			dcdebug("Unknown success message %s %s", errorCode.c_str(), errorMessage.c_str());
			return;
		// [[fallthrough]];
		// default:
			// ...
	}

	dcdebug("DM::AdcCommand::STA: disconnecting (%s)\n", aSource->getToken().c_str());
	disconnect(aSource);
}

void DownloadManager::fileNotAvailable(UserConnection* aSource, bool aNoAccess, const string& aMessage) {
	if(aSource->getState() != UserConnection::STATE_SND) {
		dcdebug("DM::fileNotAvailable Invalid state, disconnecting");
		disconnect(aSource);
		return;
	}
	
	Download* d = aSource->getDownload();
	dcassert(d);
	dcdebug("File Not Available: %s\n", d->getPath().c_str());

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

	removeDownload(d);
	QueueManager::getInstance()->putDownloadHooked(d, false, aNoAccess);
}

} // namespace dcpp
