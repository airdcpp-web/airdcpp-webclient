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
#include <airdcpp/transfer/upload/UploadManager.h>

#include <cmath>

#include <airdcpp/protocol/AdcCommand.h>
#include <airdcpp/util/AutoLimitUtil.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/core/io/stream/StreamBase.h>
#include <airdcpp/transfer/upload/Upload.h>
#include <airdcpp/transfer/upload/UploadFileParser.h>
#include <airdcpp/transfer/upload/UploadQueueManager.h>
#include <airdcpp/connection/UserConnection.h>


namespace dcpp {

using ranges::find_if;

UploadManager::UploadManager() noexcept : queue(make_unique<UploadQueueManager>([this] { return getFreeSlots(); })) {
	TimerManager::getInstance()->addListener(this);


	SettingsManager::getInstance()->registerChangeHandler({
		SettingsManager::FREE_SLOTS_EXTENSIONS
	}, [this](auto ...) {
		setFreeSlotMatcher();
	});
}

UploadManager::~UploadManager() {
	TimerManager::getInstance()->removeListener(this);

	while (true) {
		{
			RLock l(cs);
			if (uploads.empty())
				break;
		}
		Thread::sleep(100);
	}
}

void UploadManager::setFreeSlotMatcher() {
	freeSlotMatcher.pattern = SETTING(FREE_SLOTS_EXTENSIONS);
	freeSlotMatcher.setMethod(StringMatch::WILDCARD);
	freeSlotMatcher.prepare();
}

uint8_t UploadManager::getSlots() const noexcept {
	return static_cast<uint8_t>(AutoLimitUtil::getSlots(false)); 
}

uint8_t UploadManager::getFreeSlots() const noexcept {
	return (uint8_t)max((getSlots() - runningUsers), 0);
}

int UploadManager::getFreeExtraSlots() const noexcept {
	return max(SETTING(EXTRA_SLOTS) - getExtra(), 0); 
}

OptionalProfileToken UploadManager::findProfile(UserConnection& uc, const string& aUserSID) const noexcept {
	if (aUserSID.empty()) {
		// no SID specified, find with hint
		auto c = ClientManager::getInstance()->findClient(uc.getHubUrl());
		if (c) {
			return c->get(HubSettings::ShareProfile);
		}
	} else {
		auto ouList = ClientManager::getInstance()->getOnlineUsers(uc.getUser());
		for (const auto& ou : ouList) {
			if (compare(ou->getIdentity().getSIDString(), aUserSID) == 0) {
				uc.setHubUrl(ou->getClient()->getHubUrl());
				return ou->getClient()->get(HubSettings::ShareProfile);
			}
		}
	}

	// Don't accept invalid SIDs/offline hubs
	return nullopt;
}

bool UploadManager::prepareFile(UserConnection& aSource, const UploadRequest& aRequest) {
	dcdebug("Preparing %s %s " I64_FMT " " I64_FMT " %d" " " "%s %s\n", aRequest.type.c_str(), aRequest.file.c_str(), aRequest.segment.getStart(), aRequest.segment.getEnd(), aRequest.listRecursive,
		aSource.getHubUrl().c_str(), ClientManager::getInstance()->getFormattedNicks(aSource.getHintedUser()).c_str());

	if (!aRequest.validate()) {
		aSource.sendError("Invalid request");
		return false;
	}

	// Make sure that we have an user
	auto profile = findProfile(aSource, aRequest.userSID);
	if (!profile) {
		aSource.sendError("Unknown user", AdcCommand::ERROR_UNKNOWN_USER);
		return false;
	}

	// Check that we have something to send (no disk access at this point)
	UploadParser creator(freeSlotMatcher);
	try {
		creator.parseFileInfo(aRequest, *profile, aSource.getHintedUser());
	} catch (const UploadParser::UploadParserException& e) {
		aSource.sendError(e.getError(), e.noAccess ? AdcCommand::ERROR_FILE_ACCESS_DENIED : AdcCommand::ERROR_FILE_NOT_AVAILABLE);
		return false;
	}

	Upload* u = nullptr;

	{
		// Don't allow multiple connections to be here simultaneously while the slot is being assigned
		Lock slotLock(slotCS);

		OptionalTransferSlot slot;

		// Check slots
		try {
			slot = parseSlotHookedThrow(aSource, creator);
			if (!slot) {
				if (isUploadingMCN(aSource.getUser())) {
					// Don't queue MCN requests for existing uploaders
					aSource.maxedOut();
				} else {
					aSource.maxedOut(queue->addFailedUpload(aSource, creator.sourceFile, aRequest.segment.getStart(), creator.fileSize));
				}

				aSource.disconnect();
				return false;
			}
		} catch (const HookRejectException& e) {
			// Rejected
			aSource.sendError(e.getRejection()->message);
			aSource.disconnect();
			return false;
		}

		// Open stream and create upload
		try {
			unique_ptr<InputStream> is = resumeStream(aSource, creator);
			u = creator.toUpload(aSource, aRequest, is, *profile);
			if (!u) {
				aSource.sendError();
				return false;
			}
		} catch (const ShareException& e) {
			aSource.sendError(e.getError());
			return false;
		} catch (const QueueException& e) {
			aSource.sendError(e.getError());
			return false;
		} catch (const Exception& e) {
			if (!e.getError().empty()) {
				log(STRING(UNABLE_TO_SEND_FILE) + " " + creator.sourceFile + ": " + e.getError() + " (" + (ClientManager::getInstance()->getFormattedNicks(aSource.getHintedUser()) + ")"), LogMessage::SEV_ERROR);
			}

			aSource.sendError();
			return false;
		}

		{
			WLock l(cs);
			dcassert(!findUploadUnsafe(u->getToken()));
			uploads.push_back(u);
		}

		dcassert(slot);
		fire(UploadManagerListener::Created(), u, *slot);
		updateSlotCounts(aSource, *slot);
	}

	queue->removeQueue(aSource.getUser());
	return true;
}

bool UploadManager::standardSlotsRemaining(const UserPtr& aUser) const noexcept {
	auto noQueue = queue->allowUser(aUser);
	auto hasFreeSlot = (getFreeSlots() > 0) && noQueue;
	if (hasFreeSlot) {
		return true;
	}

	auto grantExtra = lowSpeedSlotsRemaining();
	if (grantExtra) {
		return true;
	}

	return false;
}

OptionalTransferSlot UploadManager::parseAutoGrantHookedThrow(const UserConnection& aSource, const UploadParser& aParser) const {
	auto data = slotTypeHook.runHooksData(this, aSource, aParser);
	if (data.empty()) {
		return nullopt;
	}

	auto normalizedData = ActionHook<OptionalTransferSlot>::normalizeData(data);

	auto max = ranges::max_element(
		normalizedData,
		[](const auto& a, const auto& b) {
			auto typeA = TransferSlot::toType(a);
			auto typeB = TransferSlot::toType(b);
			return typeA < typeB;
		}
	);
	return *max;
}

bool UploadManager::isUploadingMCN(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	return multiUploads.contains(aUser); 
}

#define SLOT_SOURCE_STANDARD "standard"
#define SLOT_SOURCE_MCN "mcn_small"
#define SLOT_SOURCE_MINISLOT "minislot"

OptionalTransferSlot UploadManager::parseSlotHookedThrow(const UserConnection& aSource, const UploadParser& aParser) const {
	auto currentSlotType = aSource.getSlotType();

	// Existing permanent slot?
	auto hasPermanentSlot = currentSlotType == TransferSlot::USERSLOT;
	if (hasPermanentSlot) {
		return aSource.getSlot();
	}

	// Existing uploader and no new connections allowed?
	if (!aParser.usesSmallSlot() && isUploadingMCN(aSource.getUser()) && !allowNewMultiConn(aSource)) {
		dcdebug("UploadManager::parseSlotType: new MCN connections not allowed for %s\n", aSource.getConnectToken().c_str());
		return nullopt;
	}

	// Hooks
	auto newSlot = parseAutoGrantHookedThrow(aSource, aParser);

	// Small file slots? Don't let the hooks override this
	if (aSource.isMCN() && aParser.usesSmallSlot()) {
		// All small files will get this slot type regardless of the connection count
		// as the actual small file connection isn't known but it's not really causing problems
		// Could be solved with https://forum.dcbase.org/viewtopic.php?f=55&t=856 (or adding a type flag for all MCN connections)
		auto smallFree = aSource.hasSlot(TransferSlot::FILESLOT, SLOT_SOURCE_MCN) || smallFileConnections <= 8;
		if (smallFree) {
			dcdebug("UploadManager::parseSlotType: assign small slot for %s\n", aSource.getConnectToken().c_str());
			return TransferSlot(TransferSlot::FILESLOT, SLOT_SOURCE_MCN);
		}
	}

	// Permanent slot?
	if (TransferSlot::toType(newSlot) == TransferSlot::USERSLOT) {
		dcdebug("UploadManager::parseSlotType: assign permanent slot for %s (%s)\n", aSource.getConnectToken().c_str(), newSlot->source.c_str());
		return newSlot;
	} else if (standardSlotsRemaining(aSource.getUser())) {
		dcdebug("UploadManager::parseSlotType: assign permanent slot for %s (standard)\n", aSource.getConnectToken().c_str());
		return TransferSlot(TransferSlot::USERSLOT, SLOT_SOURCE_STANDARD);
	}

	// Per-file slots
	if (!newSlot) {
		// Mini slots?
		if (aParser.miniSlot) {
			auto isOP = [&aSource] {
				auto ou = ClientManager::getInstance()->findOnlineUser(aSource.getHintedUser(), false);
				if (ou && ou->getIdentity().isOp()) {
					return true;
				}

				return false;
			};

			auto supportsFree = aSource.isSet(UserConnection::FLAG_SUPPORTS_MINISLOTS);
			auto allowedFree = aSource.hasSlot(TransferSlot::FILESLOT, SLOT_SOURCE_MINISLOT) || isOP() || getFreeExtraSlots() > 0;
			if (supportsFree && allowedFree) {
				dcdebug("UploadManager::parseSlotType: assign minislot for %s\n", aSource.getConnectToken().c_str());
				return TransferSlot(TransferSlot::FILESLOT, SLOT_SOURCE_MINISLOT);
			}
		}
	}

	dcdebug("UploadManager::parseSlotType: assign slot type %d for %s\n", TransferSlot::toType(newSlot), aSource.getConnectToken().c_str());
	return newSlot;
}

unique_ptr<InputStream> UploadManager::resumeStream(const UserConnection& aSource, const UploadParser& aParser) {
	Upload* delayUploadToDelete = nullptr;
	unique_ptr<InputStream> stream;

	{
		// Are we resuming an existing upload?
		WLock l(cs);
		auto i = ranges::find_if(delayUploads, [&aSource](const Upload* up) { return &aSource == &up->getUserConnection(); });
		if (i != delayUploads.end()) {
			auto up = *i;
			delayUploads.erase(i);

			if (aParser.sourceFile == up->getPath() && up->getType() == Transfer::TYPE_FILE && aParser.type == Transfer::TYPE_FILE && up->getSegment().getEnd() != aParser.fileSize) {
				// We are resuming the same file, reuse the existing upload (and file handle) because of OS cached stream data
				dcassert(aSource.getUpload());
				stream.reset(up->getStream()->releaseRootStream());
			}

			delayUploadToDelete = up;
		}
	}

	if (delayUploadToDelete) {
		deleteDelayUpload(delayUploadToDelete, !!stream.get());
	} else {
		dcassert(!aSource.getUpload());
	}

	return std::move(stream);
}

void UploadManager::removeSlot(UserConnection& aSource) noexcept {
	switch (aSource.getSlotType()) {
		case TransferSlot::USERSLOT: {
			if (aSource.isMCN()) {
				changeMultiConnSlot(aSource.getUser(), true);
			} else {
				runningUsers--;
			}
			break;
		}
		case TransferSlot::FILESLOT: {
			if (aSource.hasSlotSource(SLOT_SOURCE_MINISLOT)) {
				extra--;
			} else if (aSource.hasSlotSource(SLOT_SOURCE_MCN)) {
				smallFileConnections--;
			}
			break;
		}
		case TransferSlot::NOSLOT:
			break;
	}
}

void UploadManager::updateSlotCounts(UserConnection& aSource, const TransferSlot& aNewSlot) noexcept {
	auto newSlotType = aNewSlot.type;
	if (aSource.getSlotType() == newSlotType) {
		return;
	}

	// remove old count
	removeSlot(aSource);
		
	// user got a slot
	aSource.setSlot(aNewSlot);

	// set new slot count
	switch (newSlotType) {
		case TransferSlot::USERSLOT: {
			if (aSource.isMCN()) {
				changeMultiConnSlot(aSource.getUser(), false);
			} else {
				runningUsers++;
			}
			disconnectExtraMultiConn();
			break;
		}
		case TransferSlot::FILESLOT: {
			if (aSource.hasSlotSource(SLOT_SOURCE_MINISLOT)) {
				extra++;
			} else if (aSource.hasSlotSource(SLOT_SOURCE_MCN)) {
				smallFileConnections++;
			}

			break;
		}
		case TransferSlot::NOSLOT:
			break;
	}

	setLastGrant(GET_TICK());
}

void UploadManager::changeMultiConnSlot(const UserPtr& aUser, bool aRemove) noexcept {
	WLock l(cs);
	auto uis = multiUploads.find(aUser);
	if (uis != multiUploads.end()) {
		if (aRemove) {
			uis->second--;
			mcnConnections--;
			if (uis->second == 0) {
				multiUploads.erase(uis);
				//no uploads to this user, remove the reserved slot
				runningUsers--;
			}
		} else {
			uis->second++;
			mcnConnections++;
		}
	} else if (!aRemove) {
		//a new MCN upload
		multiUploads[aUser] = 1;
		runningUsers++;
		mcnConnections++;
	}
}

int UploadManager::getFreeMultiConnUnsafe() const noexcept {
	return getSlots() - runningUsers - mcnConnections + static_cast<int>(multiUploads.size());
}

bool UploadManager::allowNewMultiConn(const UserConnection& aSource) const noexcept {
	auto u = aSource.getUser();

	// Slot reserved for someone else?
	bool noQueue = queue->allowUser(aSource.getUser());

	{
		RLock l(cs);
		if (!multiUploads.empty()) {
			uint16_t highest = 0;
			for (const auto& [mcnUser, connectionCount] : multiUploads) {
				if (mcnUser == u) {
					continue;
				}
				if (connectionCount > highest) {
					highest = connectionCount;
				}
			}

			auto currentUserInfo = multiUploads.find(u);
			if (currentUserInfo != multiUploads.end()) {
				auto currentUserConnCount = currentUserInfo->second;
				auto newUserConnCount = currentUserConnCount + 1;

				// Remaining connections?
				auto hasFreeMcnSlot = getFreeMultiConnUnsafe() > 0 && noQueue;

				// Can't have more than 2 connections higher than the next user if there are no free slots
				if (newUserConnCount > highest && !hasFreeMcnSlot) {
					return false;
				}

				// Check per user limits
				auto totalMcnSlots = AutoLimitUtil::getSlotsPerUser(false);
				if (totalMcnSlots > 0 && newUserConnCount > totalMcnSlots) {
					return false;
				}

				return true;
			}
		}
	}

	// He's not uploading from us yet, check if we can allow new ones
	return getFreeSlots() > 0 && noQueue;
}

void UploadManager::disconnectExtraMultiConn() noexcept {
	if (lowSpeedSlotsRemaining()) {
		return;
	}

	RLock l(cs);
	if (getFreeMultiConnUnsafe() >= 0 || multiUploads.empty()) {
		return; //no reason to remove anything
	}

	auto highestConnCount = ranges::max_element(multiUploads | views::values);
	if (*highestConnCount <= 1) {
		return; //can't disconnect the only upload
	}

	// Find the correct upload to kill
	auto toDisconnect = ranges::find_if(uploads, [&](const Upload* up) { 
		return up->getUser() == highestConnCount.base()->first && up->getUserConnection().getSlotType() == TransferSlot::USERSLOT;
	});

	if (toDisconnect != uploads.end()) {
		(*toDisconnect)->getUserConnection().disconnect(true);
	}
}

Upload* UploadManager::findUpload(TransferToken aToken, const UploadList& aUploadList) noexcept {
	if (auto u = ranges::find_if(aUploadList, [&](const Upload* up) { return compare(up->getToken(), aToken) == 0; }); u != aUploadList.end()) {
		return *u;
	}

	return nullptr;
}

Upload* UploadManager::findUploadUnsafe(TransferToken aToken) const noexcept {
	if (auto u = findUpload(aToken, uploads); u) {
		return u;
	}

	if (auto u = findUpload(aToken, delayUploads); u) {
		return u;
	}

	return nullptr;
}

Callback UploadManager::getAsyncWrapper(TransferToken aToken, UploadCallback&& aCallback) const noexcept {
	return [aToken, this, hander = std::move(aCallback)] {
		Upload* upload = nullptr;

		{
			// Make sure that the upload hasn't been deleted
			RLock l(cs);
			upload = findUploadUnsafe(aToken);
		}

		if (upload) {
			hander(upload);
		}
	};
}

int64_t UploadManager::getRunningAverageUnsafe() const noexcept {
	int64_t avg = 0;
	for (auto u: uploads) {
		avg += u->getAverageSpeed();
	}

	return avg;
}

bool UploadManager::lowSpeedSlotsRemaining() const noexcept {
	auto speedLimit = Util::convertSize(AutoLimitUtil::getSpeedLimitKbps(false), Util::KB);

	// A 0 in settings means disable
	if (speedLimit == 0)
		return false;

	// Max slots
	if (getSlots() + AutoLimitUtil::getMaxAutoOpened() <= runningUsers)
		return false;

	// Only grant one slot per 30 sec
	if (GET_TICK() < getLastGrant() + 30*1000)
		return false;

	// Grant if upload speed is less than the threshold speed
	return getRunningAverage() < speedLimit;
}

void UploadManager::removeUpload(Upload* aUpload, bool aDelay) noexcept {
	auto deleteUpload = false;

	{
		WLock l(cs);
		auto i = ranges::find(delayUploads, aUpload);
		if (i != delayUploads.end()) {
			delayUploads.erase(i);
			dcassert(!aDelay);
			dcassert(!findUpload(aUpload->getToken(), uploads));
			deleteUpload = true;
		} else {
			dcassert(findUpload(aUpload->getToken(), uploads));
			dcassert(!findUpload(aUpload->getToken(), delayUploads));
			uploads.erase(remove(uploads.begin(), uploads.end(), aUpload), uploads.end());
			dcassert(!findUpload(aUpload->getToken(), uploads));

			if (aDelay) {
				delayUploads.push_back(aUpload);
			} else {
				deleteUpload = true;
			}
		}
	}

	if (deleteUpload) {
		dcdebug("Deleting upload %s (no delay, conn %s, upload " U32_FMT ")\n", aUpload->getPath().c_str(), aUpload->getConnectionToken().c_str(), aUpload->getToken());
		fire(UploadManagerListener::Removed(), aUpload);
		{
			RLock l(cs);
			dcassert(!findUploadUnsafe(aUpload->getToken()));
		}
		delete aUpload;
	} else {
		dcdebug("Adding delay upload %s (conn %s, upload " U32_FMT ")\n", aUpload->getPath().c_str(), aUpload->getConnectionToken().c_str(), aUpload->getToken());
	}
}

void UploadManager::on(UserConnectionListener::Get, UserConnection* aSource, const string& aFile, int64_t aResume) noexcept {
	if (aSource->getState() != UserConnection::STATE_GET) {
		dcdebug("UM::onGet Bad state, ignoring\n");
		return;
	}
	
	int64_t bytes = -1;

	auto request = UploadRequest(Transfer::names[Transfer::TYPE_FILE], aFile, Segment(aResume, bytes));
	if (prepareFile(*aSource, request)) {
		aSource->setState(UserConnection::STATE_SEND);
		aSource->fileLength(Util::toString(aSource->getUpload()->getSegmentSize()));
	}
}

void UploadManager::on(UserConnectionListener::Send, UserConnection* aSource) noexcept {
	if (aSource->getState() != UserConnection::STATE_SEND) {
		dcdebug("UM::onSend Bad state, ignoring\n");
		return;
	}

	auto u = aSource->getUpload();
	dcassert(u);

	startTransfer(u);
}

void UploadManager::on(AdcCommand::GET, UserConnection* aSource, const AdcCommand& c) noexcept {
	if(aSource->getState() != UserConnection::STATE_GET) {
		dcdebug("UM::onGET Bad state, ignoring\n");
		return;
	}

	const string& type = c.getParam(0);
	const string& fname = c.getParam(1);
	int64_t aStartPos = Util::toInt64(c.getParam(2));
	int64_t aBytes = Util::toInt64(c.getParam(3));
	string userSID;
	c.getParam("ID", 0, userSID);

	// bundles

	auto recursive = c.hasFlag("RE", 4);
	auto tthList = c.hasFlag("TL", 4);
	auto request = UploadRequest(tthList ? Transfer::names[Transfer::TYPE_TTH_LIST] : type, fname, Segment(aStartPos, aBytes), userSID, recursive);
	if (prepareFile(*aSource, request)) {
		auto u = aSource->getUpload();
		dcassert(u);

		AdcCommand cmd(AdcCommand::CMD_SND);
		cmd.addParam(type).addParam(fname)
			.addParam(Util::toString(u->getStartPos()))
			.addParam(Util::toString(u->getSegmentSize()));

		if(c.hasFlag("ZL", 4)) {
			u->setFiltered();
			cmd.addParam("ZL1");
		}
		if(tthList && type == Transfer::names[Transfer::TYPE_PARTIAL_LIST]) {
			cmd.addParam("TL1");	 
		}

		aSource->sendHooked(cmd);
		
		startTransfer(u);
	}
}

void UploadManager::startTransfer(Upload* aUpload) noexcept {
	if (!aUpload->isSet(Upload::FLAG_RESUMED))
		aUpload->setStart(GET_TICK());

	aUpload->tick();

	auto& uc = aUpload->getUserConnection();
	uc.setState(UserConnection::STATE_RUNNING);
	uc.transmitFile(aUpload->getStream());
	fire(UploadManagerListener::Starting(), aUpload);
}

void UploadManager::on(UserConnectionListener::BytesSent, UserConnection* aSource, size_t aBytes, size_t aActual) noexcept {
	dcassert(aSource->getState() == UserConnection::STATE_RUNNING);
	auto u = aSource->getUpload();
	dcassert(u);
	u->addPos(aBytes, aActual);
	u->tick();
}

void UploadManager::on(UserConnectionListener::Failed, UserConnection* aSource, const string& aError) noexcept {
	auto u = aSource->getUpload();
	if (u) {
		fire(UploadManagerListener::Failed(), u, aError);

		dcdebug("UM::onFailed (%s): Removing upload\n", aError.c_str());
		removeUpload(u);
	}

	removeConnection(aSource);
}

void UploadManager::on(UserConnectionListener::TransmitDone, UserConnection* aSource) noexcept {
	dcassert(aSource->getState() == UserConnection::STATE_RUNNING);
	auto u = aSource->getUpload();
	dcassert(u);

	aSource->setState(UserConnection::STATE_GET);

	auto partialSegmentFinished = u->isSet(Upload::FLAG_CHUNKED) && u->getSegment().getEnd() != u->getFileSize();
	if (!partialSegmentFinished) {
		logUpload(u);
	}

	removeUpload(u, partialSegmentFinished);
}

void UploadManager::logUpload(const Upload* u) noexcept {
	if(SETTING(LOG_UPLOADS) && u->getType() != Transfer::TYPE_TREE && (SETTING(LOG_FILELIST_TRANSFERS) || !u->isFilelist())) {
		ParamMap params;
		u->getParams(u->getUserConnection(), params);
		LOG(LogManager::UPLOAD, params);
	}

	fire(UploadManagerListener::Complete(), u);
}

void UploadManager::addConnection(UserConnectionPtr conn) noexcept {
	conn->addListener(this);
	conn->setState(UserConnection::STATE_GET);
}
	
void UploadManager::removeConnection(UserConnection* aSource) noexcept {
	dcassert(!aSource->getUpload());
	aSource->removeListener(this);

	// slot lost
	removeSlot(*aSource);

	aSource->setSlot(nullopt);
}

void UploadManager::disconnectOfflineUsers() noexcept {
	if (!SETTING(AUTO_KICK)) {
		return;
	}

	set<UserPtr> disconnects;
	{
		RLock l(cs);
		for (auto upload : uploads) {
			auto user = upload->getUser();
			if (user->isOnline()) {
				upload->unsetFlag(Upload::FLAG_PENDING_KICK);
				continue;
			}

			if (upload->isSet(Upload::FLAG_PENDING_KICK)) {
				if (disconnects.emplace(user).second) {
					log(STRING(DISCONNECTED_USER) + " " + Util::listToString(ClientManager::getInstance()->getNicks(user->getCID())), LogMessage::SEV_INFO);
				}

				upload->getUserConnection().disconnect(true);
				continue;
			}

			if (SETTING(AUTO_KICK_NO_FAVS) && user->isFavorite()) {
				continue;
			}

			upload->setFlag(Upload::FLAG_PENDING_KICK);
		}
	}
}

void UploadManager::on(GetListLength, UserConnection* conn) noexcept { 
	conn->error("GetListLength not supported");
	conn->disconnect(false);
}

size_t UploadManager::getUploadCount() const noexcept { 
	RLock l(cs); 
	return uploads.size(); 
}

//todo check all users hubs when sending.
void UploadManager::on(AdcCommand::GFI, UserConnection* aSource, const AdcCommand& c) noexcept {
	if (aSource->getState() != UserConnection::STATE_GET) {
		dcdebug("UM::onSend Bad state, ignoring\n");
		return;
	}
	
	if (c.getParameters().size() < 2) {
		aSource->sendHooked(AdcCommand(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_PROTOCOL_GENERIC, "Missing parameters"));
		return;
	}

	if (auto shareProfile = findProfile(*aSource, Util::emptyString); shareProfile) {
		const string& type = c.getParam(0);
		const string& ident = c.getParam(1);

		if (type == Transfer::names[Transfer::TYPE_FILE]) {
			try {
				aSource->sendHooked(ShareManager::getInstance()->getFileInfo(ident, *shareProfile));
				return;
			} catch (const ShareException&) { }
		}
	}

	aSource->sendError();
}

void UploadManager::deleteDelayUpload(Upload* aUpload, bool aResuming) noexcept {
	if (!aResuming && aUpload->isSet(Upload::FLAG_CHUNKED) && aUpload->getSegment().getEnd() != aUpload->getFileSize()) {
		logUpload(aUpload);
	}

	dcdebug("Deleting upload %s (delayed, conn %s, upload " U32_FMT ", resuming: %s)\n", aUpload->getPath().c_str(), aUpload->getConnectionToken().c_str(), aUpload->getToken(), aResuming ? "true" : "false");
	fire(UploadManagerListener::Removed(), aUpload);

#ifdef _DEBUG
	{
		RLock l(cs);
		dcassert(!findUploadUnsafe(aUpload->getToken()));
	}
#endif

	delete aUpload;
}

void UploadManager::checkExpiredDelayUploads() {
	RLock l(cs);
	for (const auto& u : delayUploads) {
		if (u->checkDelaySecond()) {
			dcdebug("UploadManager::checkExpiredDelayUploads: adding delay upload %s for removal (conn %s, upload " U32_FMT ")\n", u->getPath().c_str(), u->getConnectionToken().c_str(), u->getToken());

			dcassert(!findUpload(u->getToken(), uploads));

			// Delete uploads in their own thread
			// Makes uploads safe to access in the connection thread
			u->getUserConnection().callAsync(getAsyncWrapper(u->getToken(), [this](auto aUpload) {

				{
					WLock l(cs);
					dcassert(findUpload(aUpload->getToken(), delayUploads));
					dcassert(!findUpload(aUpload->getToken(), uploads));

					delayUploads.erase(remove(delayUploads.begin(), delayUploads.end(), aUpload), delayUploads.end());
				}

				deleteDelayUpload(aUpload, false);
			}));

			u->disableDelayCheck();
		}
	}
}

// TimerManagerListener
void UploadManager::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	checkExpiredDelayUploads();

	UploadList ticks;
	{
		RLock l(cs);
		for (auto u: uploads) {
			if (u->getPos() > 0) {
				ticks.push_back(u);
				u->tick();
			}
		}
		
		if (ticks.size() > 0) {
			fire(UploadManagerListener::Tick(), ticks);
		}
	}
}

void UploadManager::on(TimerManagerListener::Minute, uint64_t) noexcept {
	disconnectOfflineUsers();
}


void UploadManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(MENU_TRANSFERS));
}

/**
 * Abort upload of specific file
 */
void UploadManager::abortUpload(const string& aFile, bool aWaitDisconnected) noexcept {
	bool fileRunning = false;

	{
		RLock l(cs);

		//delayUploads also keep the file open...
		for (auto u: delayUploads) {
			if (u->getPath() == aFile) {
				u->getUserConnection().disconnect(true);
				fileRunning = true;
			}
		}

		for (auto u: uploads) {
			if (u->getPath() == aFile) {
				u->getUserConnection().disconnect(true);
				fileRunning = true;
			}
		}
	}
	
	if(!fileRunning) return;
	if(!aWaitDisconnected) return;
	
	for (int i = 0; i < 20 && fileRunning == true; i++){
		Thread::sleep(250);
		{
			RLock l(cs);
			fileRunning = false;
			for(auto u: delayUploads) {
				if(u->getPath() == aFile){
					dcdebug("delayUpload %s is not removed\n", aFile.c_str());
					fileRunning = true;
					break;
				}
			}

			if (fileRunning)
				continue;

			fileRunning = false;
			for(auto u: uploads) {
				if(u->getPath() == aFile){
					dcdebug("upload %s is not removed\n", aFile.c_str());
					fileRunning = true;
					break;
				}
			}
		}
	}
	
	if (fileRunning) {
		log("Aborting an upload " + aFile + " timed out", LogMessage::SEV_ERROR);
		//dcdebug("abort upload timeout %s\n", aFile.c_str());
	}

	//LogManager::getInstance()->message("Aborting an upload " + aFile + " timed out", LogMessage::SEV_ERROR);
}

} // namespace dcpp