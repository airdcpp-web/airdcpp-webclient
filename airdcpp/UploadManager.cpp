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
#include "UploadManager.h"

#include <cmath>

#include "AdcCommand.h"
#include "AirUtil.h"
#include "BZUtils.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "LogManager.h"
#include "PathUtil.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "ShareManager.h"
#include "Upload.h"
#include "UploadQueueManager.h"
#include "UserConnection.h"


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
	return static_cast<uint8_t>(AirUtil::getSlots(false)); 
}

uint8_t UploadManager::getFreeSlots() const noexcept {
	return (uint8_t)max((getSlots() - runningUsers), 0);
}

int UploadManager::getFreeExtraSlots() const noexcept {
	return max(SETTING(EXTRA_SLOTS) - getExtra(), 0); 
}

bool UploadManager::prepareFile(UserConnection& aSource, const UploadRequest& aRequest) {
	dcdebug("Preparing %s %s " I64_FMT " " I64_FMT " %d" " " "%s %s\n", aRequest.type.c_str(), aRequest.file.c_str(), aRequest.segment.getStart(), aRequest.segment.getEnd(), aRequest.listRecursive,
		aSource.getHubUrl().c_str(), ClientManager::getInstance()->getFormatedNicks(aSource.getHintedUser()).c_str());

	if (!aRequest.validate()) {
		aSource.sendError("Invalid request");
		return false;
	}

	// Make sure that we have an user
	auto profile = ClientManager::getInstance()->findProfile(aSource, aRequest.userSID);
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
		Lock l(slotCS);

		SlotType slotType = UserConnection::NOSLOT;

		// Check slots
		try {
			slotType = parseSlotTypeHookedThrow(aSource, creator);
			if (slotType == UserConnection::NOSLOT) {
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
				log(STRING(UNABLE_TO_SEND_FILE) + " " + creator.sourceFile + ": " + e.getError() + " (" + (ClientManager::getInstance()->getFormatedNicks(aSource.getHintedUser()) + ")"), LogMessage::SEV_ERROR);
			}

			aSource.sendError();
			return false;
		}

		updateSlotCounts(aSource, slotType);
	}

	queue->removeQueue(aSource.getUser());

	{
		WLock l(cs);
		uploads.push_back(u);
	}

	fire(UploadManagerListener::Created(), u);
	return true;
}

Upload* UploadManager::UploadParser::toUpload(UserConnection& aSource, const UploadRequest& aRequest, unique_ptr<InputStream>& is, ProfileToken aProfile) {
	bool resumed = is.get();
	auto startPos = aRequest.segment.getStart();
	auto bytes = aRequest.segment.getSize();

	switch (type) {
	case Transfer::TYPE_FULL_LIST:
		// handle below...
	case Transfer::TYPE_FILE:
	{
		if (aRequest.file == Transfer::USER_LIST_NAME_EXTRACTED) {
			// Unpack before sending...
			string bz2 = File(sourceFile, File::READ, File::OPEN).read();
			string xml;
			BZUtil::decodeBZ2(reinterpret_cast<const uint8_t*>(bz2.data()), bz2.size(), xml);
			// Clear to save some memory...
			string().swap(bz2);
			is.reset(new MemoryInputStream(xml));
			startPos = 0;
			fileSize = bytes = is->getSize();
		} else {
			if (bytes == -1) {
				bytes = fileSize - startPos;
			}

			if ((startPos + bytes) > fileSize) {
				throw Exception("Bytes were requested beyond the end of the file");
			}

			if (!is) {
				auto f = make_unique<File>(sourceFile, File::READ, File::OPEN | File::SHARED_WRITE); // write for partial sharing
				is = std::move(f);
			}

			is->setPos(startPos);

			if ((startPos + bytes) < fileSize) {
				is.reset(new LimitedInputStream<true>(is.release(), bytes));
			}
		}
		break;
	}
	case Transfer::TYPE_TREE:
	{
		// sourceFile = aRequest.file;
		unique_ptr<MemoryInputStream> mis(ShareManager::getInstance()->getTree(sourceFile, aProfile));
		if (!mis.get()) {
			return nullptr;
		}

		startPos = 0;
		fileSize = bytes = mis->getSize();
		is = std::move(mis);
		break;
	}
	case Transfer::TYPE_PARTIAL_LIST: {
		unique_ptr<MemoryInputStream> mis = nullptr;
		// Partial file list
		if (aRequest.isTTHList) {
			if (!PathUtil::isAdcDirectoryPath(aRequest.file)) {
				BundlePtr bundle = nullptr;
				mis.reset(QueueManager::getInstance()->generateTTHList(Util::toUInt32(aRequest.file), aProfile != SP_HIDDEN, bundle));

				// We don't want to show the token in transfer view
				if (bundle) {
					sourceFile = bundle->getName();
				} else {
					dcassert(0);
				}
			} else {
				mis.reset(ShareManager::getInstance()->generateTTHList(aRequest.file, aRequest.listRecursive, aProfile));
			}
		} else {
			mis.reset(ShareManager::getInstance()->generatePartialList(aRequest.file, aRequest.listRecursive, aProfile));
		}

		if (!mis.get()) {
			return nullptr;
		}

		startPos = 0;
		fileSize = bytes = mis->getSize();
		is = std::move(mis);
		break;
	}
	default:
		dcassert(0);
	}

	// Upload
	// auto size = is->getSize();
	auto u = new Upload(aSource, sourceFile, TTHValue(), std::move(is));
	u->setSegment(Segment(startPos, bytes));
	if (u->getSegment().getEnd() != fileSize) {
		u->setFlag(Upload::FLAG_CHUNKED);
	}
	if (partialFileSharing) {
		u->setFlag(Upload::FLAG_PARTIAL);
	}
	if (resumed) {
		u->setFlag(Upload::FLAG_RESUMED);
	}

	u->setFileSize(fileSize);
	u->setType(type);
	dcdebug("Created upload for file %s (conn %s, resuming: %s)\n", u->getPath().c_str(), u->getToken().c_str(), resumed ? "true" : "false");
	return u;
}

void UploadManager::UploadParser::toRealWithSize(const UploadRequest& aRequest, ProfileToken aProfile, const HintedUser& aUser) {
	auto noAccess = false;
	try {
		// Get all hubs with file transfers
		ProfileTokenSet profiles;
		ClientManager::getInstance()->listProfiles(aUser, profiles);
		if (profiles.empty()) {
			//the user managed to go offline already?
			profiles.insert(aProfile);
		}

		ShareManager::getInstance()->toRealWithSize(aRequest.file, profiles, aUser, sourceFile, fileSize, noAccess);
	} catch (const ShareException&) {
		try {
			QueueManager::getInstance()->toRealWithSize(aRequest.file, sourceFile, fileSize, aRequest.segment);
		} catch (const QueueException&) {
			throw UploadParserException(UserConnection::FILE_NOT_AVAILABLE, noAccess);
		}
	}
}

void UploadManager::UploadParser::parseFileInfo(const UploadRequest& aRequest, ProfileToken aProfile, const HintedUser& aUser) {
	auto userlist = aRequest.isUserlist();

	if (aRequest.type == Transfer::names[Transfer::TYPE_FILE]) {
		type = userlist ? Transfer::TYPE_FULL_LIST : Transfer::TYPE_FILE;


		//check that we have a file
		if (userlist) {
			auto info = std::move(ShareManager::getInstance()->getFileListInfo(aRequest.file, aProfile));
			sourceFile = std::move(info.second);
			fileSize = info.first;
			miniSlot = true;
		} else {
			toRealWithSize(aRequest, aProfile, aUser);
			miniSlot = freeSlotMatcher.match(PathUtil::getFileName(sourceFile));
		}

		miniSlot = miniSlot || (fileSize <= Util::convertSize(SETTING(SET_MINISLOT_SIZE), Util::KB));
	} else if (aRequest.type == Transfer::names[Transfer::TYPE_TREE]) {
		toRealWithSize(aRequest, aProfile, aUser);
		type = Transfer::TYPE_TREE;
		miniSlot = true;

	} else if (aRequest.type == Transfer::names[Transfer::TYPE_PARTIAL_LIST]) {
		type = Transfer::TYPE_PARTIAL_LIST;
		miniSlot = true;
	} else {
		throw UploadParserException("Unknown file type", false);
	}
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

	return true;
}

SlotType UploadManager::parseAutoGrantHookedThrow(const UserConnection& aSource, const UploadParser& aParser) const {
	auto data = slotTypeHook.runHooksData(this, aSource.getHintedUser(), aParser);
	if (data.empty()) {
		return UserConnection::NOSLOT;
	}

	auto normalizedData = ActionHook<SlotType>::normalizeData(data);

	auto max = ranges::max_element(normalizedData);
	return static_cast<UserConnection::SlotTypes>(*max);
}

bool UploadManager::isUploadingMCN(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	return multiUploads.find(aUser) != multiUploads.end(); 
}

SlotType UploadManager::parseSlotTypeHookedThrow(const UserConnection& aSource, const UploadParser& aParser) const {
	auto currentSlotType = aSource.getSlotType();

	// Existing permanent slot?
	auto hasPermanentSlot = currentSlotType == UserConnection::STDSLOT || currentSlotType == UserConnection::MCNSLOT;
	if (hasPermanentSlot) {
		return currentSlotType;
	}

	// Existing uploader and no new connections allowed?
	if (isUploadingMCN(aSource.getUser()) && !allowNewMultiConn(aSource)) {
		dcdebug("UploadManager::parseSlotType: new MCN connections not allowed for %s\n", aSource.getToken().c_str());
		return UserConnection::NOSLOT;
	}

	// Hooks
	auto newSlotType = parseAutoGrantHookedThrow(aSource, aParser);

	if (aSource.isMCN()) {
		// Small file slots? Don't let the hooks override this
		auto isSmallFile = aParser.type == Transfer::TYPE_PARTIAL_LIST || (aParser.type != Transfer::TYPE_FULL_LIST && aParser.fileSize <= 65792);
		if (isSmallFile) {
			// All small files will get this slot type regardless of the connection count
			// as the actual small file connection isn't known but it's not really causing problems
			// Could be solved with https://forum.dcbase.org/viewtopic.php?f=55&t=856 (or adding a type flag for all MCN connections)
			auto smallFree = currentSlotType == UserConnection::SMALLSLOT || smallFileConnections <= 8;
			if (smallFree) {
				dcdebug("UploadManager::parseSlotType: assign small slot for %s\n", aSource.getToken().c_str());
				return UserConnection::SMALLSLOT;
			}
		}
	}

	// Permanent slot?
	if (newSlotType == UserConnection::STDSLOT || standardSlotsRemaining(aSource.getUser())) {
		dcdebug("UploadManager::parseSlotType: assign permanent slot for %s\n", aSource.getToken().c_str());
		return aSource.isMCN() ? UserConnection::MCNSLOT : UserConnection::STDSLOT;
	}

	// Per-file slots
	if (newSlotType == UserConnection::NOSLOT) {
		// Extra slots?
		if (aParser.miniSlot) {
			auto supportsFree = aSource.isSet(UserConnection::FLAG_SUPPORTS_MINISLOTS);
			auto allowedFree = currentSlotType == UserConnection::EXTRASLOT || aSource.isSet(UserConnection::FLAG_OP) || getFreeExtraSlots() > 0;
			if (supportsFree && allowedFree) {
				dcdebug("UploadManager::parseSlotType: assign minislot for %s\n", aSource.getToken().c_str());
				return UserConnection::EXTRASLOT;
			}
		}

		// Partial slots?
		if (aParser.partialFileSharing) {
			auto partialFree = currentSlotType == UserConnection::PARTIALSLOT || (extraPartial < SETTING(EXTRA_PARTIAL_SLOTS));
			if (partialFree) {
				dcdebug("UploadManager::parseSlotType: assign partial slot for %s\n", aSource.getToken().c_str());
				return UserConnection::PARTIALSLOT;
			}
		}
	}

	dcdebug("UploadManager::parseSlotType: assign slot type %d for %s\n", newSlotType, aSource.getToken().c_str());
	return newSlotType;
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
	}

	return std::move(stream);
}

void UploadManager::removeSlot(UserConnection& aSource) noexcept {
	switch (aSource.getSlotType()) {
	case UserConnection::STDSLOT:
		runningUsers--;
		break;
	case UserConnection::EXTRASLOT:
		extra--;
		break;
	case UserConnection::PARTIALSLOT:
		extraPartial--;
		break;
	case UserConnection::MCNSLOT:
		changeMultiConnSlot(aSource.getUser(), true);
		break;
	case UserConnection::SMALLSLOT:
		smallFileConnections--;
		break;
	}
}

void UploadManager::updateSlotCounts(UserConnection& aSource, SlotType aNewSlotType) noexcept {
	if (aSource.getSlotType() == aNewSlotType) {
		return;
	}

	// remove old count
	removeSlot(aSource);
		
	// user got a slot
	aSource.setSlotType(aNewSlotType);

	// set new slot count
	switch(aNewSlotType) {
		case UserConnection::STDSLOT:
			runningUsers++;
			disconnectExtraMultiConn();
			break;
		case UserConnection::EXTRASLOT:
			extra++;
			break;
		case UserConnection::PARTIALSLOT:
			extraPartial++;
			break;
		case UserConnection::MCNSLOT:
			changeMultiConnSlot(aSource.getUser(), false);
			disconnectExtraMultiConn();
			break;
		case UserConnection::SMALLSLOT:
			smallFileConnections++;
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
	return static_cast<int>(getSlots() - runningUsers - mcnConnections) + static_cast<int>(multiUploads.size());
}

bool UploadManager::allowNewMultiConn(const UserConnection& aSource) const noexcept {
	auto u = aSource.getUser();

	// Slot reserved for someone else?
	bool noQueue = queue->allowUser(aSource.getUser());

	{
		RLock l(cs);
		if (!multiUploads.empty()) {
			uint16_t highest = 0;
			for (const auto& i: multiUploads) {
				if (i.first == u) {
					continue;
				}
				if (i.second > highest) {
					highest = i.second;
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
				auto totalMcnSlots = AirUtil::getSlotsPerUser(false);
				if (totalMcnSlots > 0 && newUserConnCount >= totalMcnSlots) {
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
	auto toDisconnect = ranges::find_if(uploads, [&](Upload* up) { 
		return up->getUser() == highestConnCount.base()->first && up->getUserConnection().getSlotType() == UserConnection::MCNSLOT;
	});

	if (toDisconnect != uploads.end()) {
		(*toDisconnect)->getUserConnection().disconnect(true);
	}
}

Upload* UploadManager::findUploadUnsafe(const string& aToken) const noexcept {
	auto u = ranges::find_if(uploads, [&](Upload* up) { return compare(up->getToken(), aToken) == 0; });
	if (u != uploads.end()) {
		return *u;
	}

	auto s = ranges::find_if(delayUploads, [&](Upload* up) { return compare(up->getToken(), aToken) == 0; });
	if (s != delayUploads.end()) {
		return *s;
	}

	return nullptr;
}


bool UploadManager::callAsync(const string& aToken, std::function<void(const Upload*)>&& aHandler) const noexcept {
	RLock l(cs);
	auto u = findUploadUnsafe(aToken);
	if (u) {
		u->getUserConnection().callAsync([aToken, this, hander = std::move(aHandler)] {
			Upload* upload = nullptr;

			{
				// Make sure that the upload hasn't been deleted
				RLock l(cs);
				upload = findUploadUnsafe(aToken);
			}

			if (upload) {
				hander(upload);
			}
		});

		return true;
	}

	return false;
}

int64_t UploadManager::getRunningAverageUnsafe() const noexcept {
	int64_t avg = 0;
	for (auto u: uploads) {
		avg += static_cast<int64_t>(u->getAverageSpeed());
	}

	return avg;
}

bool UploadManager::lowSpeedSlotsRemaining() const noexcept {
	auto speedLimit = Util::convertSize(AirUtil::getSpeedLimitKbps(false), Util::KB);

	// A 0 in settings means disable
	if (speedLimit == 0)
		return false;

	// Max slots
	if (getSlots() + AirUtil::getMaxAutoOpened() <= runningUsers)
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
			dcassert(ranges::find(uploads, aUpload) == uploads.end());
			deleteUpload = true;
		} else {
			dcassert(ranges::find(uploads, aUpload) != uploads.end());
			uploads.erase(remove(uploads.begin(), uploads.end(), aUpload), uploads.end());

			if (aDelay) {
				delayUploads.push_back(aUpload);
			} else {
				deleteUpload = true;
			}
		}
	}

	if (deleteUpload) {
		dcdebug("Deleting upload %s (no delay, conn %s)\n", aUpload->getPath().c_str(), aUpload->getToken().c_str());
		fire(UploadManagerListener::Removed(), aUpload);
		{
			RLock l(cs);
			dcassert(!findUploadUnsafe(aUpload->getToken()));
		}
		delete aUpload;
	} else {
		dcdebug("Adding delay upload %s (conn %s)\n", aUpload->getPath().c_str(), aUpload->getToken().c_str());
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
	auto request = UploadRequest(type, fname, Segment(aStartPos, aBytes), userSID, recursive, tthList);
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
		if(c.hasFlag("TL", 4) && type == Transfer::names[Transfer::TYPE_PARTIAL_LIST]) {
			cmd.addParam("TL1");	 
		}

		aSource->send(cmd);
		
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

	aSource->setSlotType(UserConnection::NOSLOT);
}

void UploadManager::on(TimerManagerListener::Minute, uint64_t) noexcept {
	UserList disconnects;
	{
		WLock l(cs);
		if (SETTING(AUTO_KICK)) {
			for (auto u: uploads) {
				if (u->getUser()->isOnline()) {
					u->unsetFlag(Upload::FLAG_PENDING_KICK);
					continue;
				}

				if (u->isSet(Upload::FLAG_PENDING_KICK)) {
					disconnects.push_back(u->getUser());
					continue;
				}

				if (SETTING(AUTO_KICK_NO_FAVS) && u->getUser()->isFavorite()) {
					continue;
				}

				u->setFlag(Upload::FLAG_PENDING_KICK);
			}
		}
	}
		
	for (auto& u: disconnects) {
		log(STRING(DISCONNECTED_USER) + " " + Util::listToString(ClientManager::getInstance()->getNicks(u->getCID())), LogMessage::SEV_INFO);
		ConnectionManager::getInstance()->disconnect(u, CONNECTION_TYPE_UPLOAD);
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
		aSource->send(AdcCommand(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_PROTOCOL_GENERIC, "Missing parameters"));
		return;
	}

	auto shareProfile = ClientManager::getInstance()->findProfile(*aSource, Util::emptyString);
	if (shareProfile) {
		const string& type = c.getParam(0);
		const string& ident = c.getParam(1);

		if (type == Transfer::names[Transfer::TYPE_FILE]) {
			try {
				aSource->send(ShareManager::getInstance()->getFileInfo(ident, *shareProfile));
				return;
			} catch(const ShareException&) { }
		}
	}

	aSource->sendError();
}

void UploadManager::deleteDelayUpload(Upload* aUpload, bool aResuming) noexcept {
	if (!aResuming && aUpload->isSet(Upload::FLAG_CHUNKED) && aUpload->getSegment().getEnd() != aUpload->getFileSize()) {
		logUpload(aUpload);
	}

	dcdebug("Deleting upload %s (delayed, conn %s, resuming: %s)\n", aUpload->getPath().c_str(), aUpload->getToken().c_str(), aResuming ? "true" : "false");
	fire(UploadManagerListener::Removed(), aUpload);
	{
		RLock l(cs);
		dcassert(!findUploadUnsafe(aUpload->getToken()));
	}

	delete aUpload;
}

// TimerManagerListener
void UploadManager::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	UploadList ticks;
	{
		WLock l(cs);
		for (auto i = delayUploads.begin(); i != delayUploads.end();) {
			auto u = *i;
			if (++u->delayTime > 10) {
				u->getUserConnection().callAsync([u, this] {
					deleteDelayUpload(u, false);
				});
				
				i = delayUploads.erase(i);
			} else {
				i++;
			}
		}

		for (auto u: uploads) {
			if (u->getPos() > 0) {
				ticks.push_back(u);
				u->tick();
			}
		}
		
		if (ticks.size() > 0)
			fire(UploadManagerListener::Tick(), ticks);
	}
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