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
#include "CryptoManager.h"
#include "FavoriteManager.h"
#include "LogManager.h"
#include "PathUtil.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "ShareManager.h"
#include "Upload.h"
#include "UserConnection.h"

#include <boost/range/numeric.hpp>
#include <boost/range/adaptor/map.hpp>


namespace dcpp {

using ranges::find_if;

UploadManager::UploadManager() noexcept {	
	ClientManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);


	SettingsManager::getInstance()->registerChangeHandler({
		SettingsManager::FREE_SLOTS_EXTENSIONS
	}, [this](auto ...) {
		setFreeSlotMatcher();
	});
}

UploadManager::~UploadManager() {
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);
	{
		WLock l(cs);
		for (const auto ii: uploadQueue) {
			for (const auto& f: ii.files) {
				f->dec();
			}
		}

		uploadQueue.clear();
	}

	while (true) {
		{
			RLock l(cs);
			if (uploads.empty())
				break;
		}
		Thread::sleep(100);
	}
}

UploadQueueItem::UploadQueueItem(const HintedUser& _user, const string& _file, int64_t _pos, int64_t _size) :
	user(_user), file(_file), pos(_pos), size(_size), time(GET_TIME()) {

	inc();
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
	return (uint8_t)max((getSlots() - running), 0); 
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

	// Check slots
	auto slotType = parseSlotType(aSource, creator);
	if (slotType == UserConnection::NOSLOT) {
		auto isUploadingF = [&] { RLock l(cs); return isUploadingLocked(aSource.getUser()); };
		if (aSource.isMCN() && isUploadingF()) {
			// Don't queue MCN requests for existing uploaders
			aSource.maxedOut();
		} else {
			aSource.maxedOut(addFailedUpload(aSource, creator.sourceFile, aRequest.segment.getStart(), creator.fileSize));
		}

		aSource.disconnect();
		return false;
	}

	// Open stream and create upload
	Upload* u = nullptr;
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

	{
		WLock l(cs);
		// remove file from upload queue
		clearUserFiles(aSource.getUser(), false);

		// remove user from notified list
		auto cu = notifiedUsers.find(aSource.getUser());
		if(cu != notifiedUsers.end()) {
			notifiedUsers.erase(cu);
		}
	}

	{
		WLock l(cs);
		uploads.push_back(u);
	}

	fire(UploadManagerListener::Created(), u);

	updateSlotCounts(aSource, slotType);
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
			CryptoManager::getInstance()->decodeBZ2(reinterpret_cast<const uint8_t*>(bz2.data()), bz2.size(), xml);
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

uint8_t UploadManager::parseSlotType(const UserConnection& aSource, const UploadParser& aParser) {
	auto slotType = aSource.getSlotType();

	if (slotType != UserConnection::STDSLOT && slotType != UserConnection::MCNSLOT) {
		auto isFavorite = FavoriteManager::getInstance()->hasSlot(aSource.getUser());

		{
			WLock l(cs);
			auto hasReserved = reservedSlots.find(aSource.getUser()) != reservedSlots.end();
			auto hasFreeSlot = (getFreeSlots() > 0) && ((uploadQueue.empty() && notifiedUsers.empty()) || isNotifiedUser(aSource.getUser()));
		
			if ((aParser.type == Transfer::TYPE_PARTIAL_LIST || (aParser.type != Transfer::TYPE_FULL_LIST && aParser.fileSize <= 65792)) && smallSlots <= 8) {
				slotType = UserConnection::SMALLSLOT;
			} else if (aSource.isMCN()) {
				if (getMultiConnLocked(aSource) || ((hasReserved || isFavorite|| getAutoSlot()) && !isUploadingLocked(aSource.getUser()))) {
					slotType = UserConnection::MCNSLOT;
				} else {
					slotType = UserConnection::NOSLOT;
				}
			} else if (!(hasReserved || isFavorite || hasFreeSlot || getAutoSlot())) {
				slotType = UserConnection::NOSLOT;
			} else {
				slotType = UserConnection::STDSLOT;
			}
		}

		if (slotType == UserConnection::NOSLOT) {
			auto supportsFree = aSource.isSet(UserConnection::FLAG_SUPPORTS_MINISLOTS);
			auto allowedFree = (slotType == UserConnection::EXTRASLOT) || aSource.isSet(UserConnection::FLAG_OP) || getFreeExtraSlots() > 0;
			auto partialFree = aParser.partialFileSharing && ((slotType == UserConnection::PARTIALSLOT) || (extraPartial < SETTING(EXTRA_PARTIAL_SLOTS)));

			if (aParser.miniSlot && supportsFree && allowedFree) {
				slotType = UserConnection::EXTRASLOT;
			} else if (partialFree) {
				slotType = UserConnection::PARTIALSLOT;
			}
		}

		setLastGrant(GET_TICK());
	}

	return slotType;
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

void UploadManager::updateSlotCounts(UserConnection& aSource, uint8_t aSlotType) noexcept {
	if(aSource.getSlotType() != aSlotType) {
		// remove old count
		switch(aSource.getSlotType()) {
			case UserConnection::STDSLOT:
				running--;
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
				smallSlots--;
				break;
		}
		
		// user got a slot
		aSource.setSlotType(aSlotType);

		// set new slot count
		switch(aSlotType) {
			case UserConnection::STDSLOT:
				//clearUserFiles(aSource.getUser());
				running++;
				checkMultiConn();
				break;
			case UserConnection::EXTRASLOT:
				extra++;
				break;
			case UserConnection::PARTIALSLOT:
				extraPartial++;
				break;
			case UserConnection::MCNSLOT:
				//clearUserFiles(aSource.getUser());
				changeMultiConnSlot(aSource.getUser(), false);
				checkMultiConn();
				break;
			case UserConnection::SMALLSLOT:
				smallSlots++;
				break;
		}
	}
}

void UploadManager::changeMultiConnSlot(const UserPtr& aUser, bool aRemove) noexcept {
	WLock l(cs);
	auto uis = multiUploads.find(aUser);
	if (uis != multiUploads.end()) {
		if (aRemove) {
			uis->second--;
			mcnSlots--;
			if (uis->second == 0) {
				multiUploads.erase(uis);
				//no uploads to this user, remove the reserved slot
				running--;
			}
		} else {
			uis->second++;
			mcnSlots++;
		}
	} else if (!aRemove) {
		//a new MCN upload
		multiUploads[aUser] = 1;
		running++;
		mcnSlots++;
	}
}

bool UploadManager::getMultiConnLocked(const UserConnection& aSource) noexcept {
	//inside a lock.
	auto u = aSource.getUser();

	bool hasFreeSlot = false;
	if ((int)(getSlots() - running - mcnSlots + multiUploads.size()) > 0) {
		if ((uploadQueue.empty() && notifiedUsers.empty()) || isNotifiedUser(aSource.getUser())) {
			hasFreeSlot = true;
		}
	}

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

		auto uis = multiUploads.find(u);
		if (uis != multiUploads.end()) {
			return ((highest > uis->second + 1) || hasFreeSlot) && (uis->second + 1 <= AirUtil::getSlotsPerUser(false) || AirUtil::getSlotsPerUser(false) == 0);
		}
	}

	//he's not uploading from us yet, check if we can allow new ones
	return (getFreeSlots() > 0) && ((uploadQueue.empty() && notifiedUsers.empty()) || isNotifiedUser(aSource.getUser()));
}

void UploadManager::checkMultiConn() noexcept {
	RLock l(cs);
	if ((int)(getSlots() - running - mcnSlots + multiUploads.size()) >= 0 || getAutoSlot() || multiUploads.empty()) {
		return; //no reason to remove anything
	}

	auto highest = max_element(multiUploads.begin(), multiUploads.end(), [&](const pair<UserPtr, uint16_t>& p1, const pair<UserPtr, uint16_t>& p2) { return p1.second < p2.second; });
	if (highest->second <= 1) {
		return; //can't disconnect the only upload
	}

	//find the correct upload to kill
	auto u = find_if(uploads.begin(), uploads.end(), [&](Upload* up) { return up->getUser() == highest->first && up->getUserConnection().getSlotType() == UserConnection::MCNSLOT; } );
	if (u != uploads.end()) {
		(*u)->getUserConnection().disconnect(true);
	}
}

Upload* UploadManager::findUploadUnsafe(const string& aToken) const noexcept {
	auto u = find_if(uploads.begin(), uploads.end(), [&](Upload* up) { return compare(up->getToken(), aToken) == 0; });
	if (u != uploads.end()) {
		return *u;
	}

	auto s = find_if(delayUploads.begin(), delayUploads.end(), [&](Upload* up) { return compare(up->getToken(), aToken) == 0; });
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

int64_t UploadManager::getRunningAverage(bool aLock) const noexcept {
	int64_t avg = 0;

	ConditionalRLock l(cs, aLock);
	for (auto u: uploads)
		avg += static_cast<int64_t>(u->getAverageSpeed()); 
	return avg;
}

bool UploadManager::getAutoSlot() const noexcept {
	/** A 0 in settings means disable */
	if (AirUtil::getSpeedLimit(false) == 0)
		return false;
	/** Max slots */
	if (getSlots() + AirUtil::getMaxAutoOpened() <= running)
		return false;		
	/** Only grant one slot per 30 sec */
	if (GET_TICK() < getLastGrant() + 30*1000)
		return false;
	/** Grant if upload speed is less than the threshold speed */
	return getRunningAverage(false) < Util::convertSize(AirUtil::getSpeedLimit(false), Util::KB);
}

void UploadManager::removeUpload(Upload* aUpload, bool aDelay) noexcept {
	auto deleteUpload = false;

	{
		WLock l(cs);
		auto i = find(delayUploads.begin(), delayUploads.end(), aUpload);
		if (i != delayUploads.end()) {
			delayUploads.erase(i);
			dcassert(!aDelay);
			dcassert(find(uploads.begin(), uploads.end(), aUpload) == uploads.end());
			deleteUpload = true;
		} else {
			dcassert(find(uploads.begin(), uploads.end(), aUpload) != uploads.end());
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

void UploadManager::reserveSlot(const HintedUser& aUser, uint64_t aTime) noexcept {
	bool connect = false;
	string token;
	{
		WLock l(cs);
		reservedSlots[aUser] = aTime > 0 ? (GET_TICK() + aTime*1000) : 0;
	
		if (aUser.user->isOnline()){
			// find user in uploadqueue to connect with correct token
			auto it = find_if(uploadQueue.cbegin(), uploadQueue.cend(), [&](const UserPtr& u) { return u == aUser.user; });
			if (it != uploadQueue.cend()) {
				token = it->token;
				connect = true;
			}
		}
	}

	if (connect) {
		connectUser(aUser, token);
	}

	fire(UploadManagerListener::SlotsUpdated(), aUser);
}

void UploadManager::connectUser(const HintedUser& aUser, const string& aToken) noexcept {
	string lastError;
	string hubUrl = aUser.hint;
	bool protocolError = false;
	ClientManager::getInstance()->connect(aUser.user, aToken, true, lastError, hubUrl, protocolError);

	//TODO: report errors?
}

void UploadManager::unreserveSlot(const UserPtr& aUser) noexcept {
	bool found = false;
	{
		WLock l(cs);
		auto uis = reservedSlots.find(aUser);
		if (uis != reservedSlots.end()){
			reservedSlots.erase(uis);
			found = true;
		}
	}

	if (found) {
		fire(UploadManagerListener::SlotsUpdated(), aUser);
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

size_t UploadManager::addFailedUpload(const UserConnection& aSource, const string& aFile, int64_t aPos, int64_t aSize) noexcept {
	size_t queue_position = 0;
	WLock l(cs);
	auto it = find_if(uploadQueue.begin(), uploadQueue.end(), [&](const UserPtr& u) -> bool { ++queue_position; return u == aSource.getUser(); });
	if (it != uploadQueue.end()) {
		it->token = aSource.getToken();
		for (const auto f: it->files) {
			if(f->getFile() == aFile) {
				f->setPos(aPos);
				return queue_position;
			}
		}
	}

	auto uqi = new UploadQueueItem(aSource.getHintedUser(), aFile, aPos, aSize);
	if (it == uploadQueue.end()) {
		++queue_position;

		WaitingUser wu(aSource.getHintedUser(), aSource.getToken());
		wu.files.insert(uqi);
		uploadQueue.push_back(wu);
	} else {
		it->files.insert(uqi);
	}

	fire(UploadManagerListener::QueueAdd(), uqi);
	return queue_position;
}

void UploadManager::clearUserFiles(const UserPtr& aUser, bool aLock) noexcept {
	
	ConditionalWLock l (cs, aLock);
	auto it = ranges::find_if(uploadQueue, [&](const UserPtr& u) { return u == aUser; });
	if (it != uploadQueue.end()) {
		for (const auto f: it->files) {
			fire(UploadManagerListener::QueueItemRemove(), f);
			f->dec();
		}
		uploadQueue.erase(it);
		fire(UploadManagerListener::QueueRemove(), aUser);
	}
}

void UploadManager::addConnection(UserConnectionPtr conn) noexcept {
	conn->addListener(this);
	conn->setState(UserConnection::STATE_GET);
}
	
void UploadManager::removeConnection(UserConnection* aSource) noexcept {
	dcassert(!aSource->getUpload());
	aSource->removeListener(this);

	// slot lost
	switch(aSource->getSlotType()) {
		case UserConnection::STDSLOT: running--; break;
		case UserConnection::EXTRASLOT: extra--; break;
		case UserConnection::PARTIALSLOT: extraPartial--; break;
		case UserConnection::SMALLSLOT: smallSlots--; break;
		case UserConnection::MCNSLOT: changeMultiConnSlot(aSource->getUser(), true); break;
	}

	aSource->setSlotType(UserConnection::NOSLOT);
}

void UploadManager::notifyQueuedUsers() noexcept {
	vector<WaitingUser> notifyList;
	{
		WLock l(cs);
		if (uploadQueue.empty()) return;		//no users to notify
		
		int freeslots = getFreeSlots();
		if(freeslots > 0)
		{
			freeslots -= notifiedUsers.size();
			while(!uploadQueue.empty() && freeslots > 0) {
				// let's keep him in the connectingList until he asks for a file
				auto wu = uploadQueue.front();
				clearUserFiles(wu.user, false);
				if(wu.user.user->isOnline()) {
					notifiedUsers[wu.user] = GET_TICK();
					notifyList.push_back(wu);
					freeslots--;
				}
			}
		}
	}

	for(auto it = notifyList.cbegin(); it != notifyList.cend(); ++it)
		connectUser(it->user, it->token);
}

void UploadManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	UserList disconnects, reservedRemoved;
	{
		WLock l(cs);
		for (auto j = reservedSlots.begin(); j != reservedSlots.end();) {
			if ((j->second > 0) && j->second < aTick) {
				reservedRemoved.push_back(j->first);
				reservedSlots.erase(j++);
			} else {
				++j;
			}
		}
	
		for(auto i = notifiedUsers.begin(); i != notifiedUsers.end();) {
			if ((i->second + (90 * 1000)) < aTick) {
				clearUserFiles(i->first, false);
				i = notifiedUsers.erase(i);
			} else
				++i;
		}

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

	auto freeSlots = getFreeSlots();
	if (freeSlots != lastFreeSlots) {
		lastFreeSlots = freeSlots;
	}

	for (auto& u: reservedRemoved)
		fire(UploadManagerListener::SlotsUpdated(), u);
}

void UploadManager::on(GetListLength, UserConnection* conn) noexcept { 
	conn->error("GetListLength not supported");
	conn->disconnect(false);
}

size_t UploadManager::getUploadCount() const noexcept { 
	RLock l(cs); 
	return uploads.size(); 
}

bool UploadManager::hasReservedSlot(const UserPtr& aUser) const noexcept {
	RLock l(cs); 
	return reservedSlots.find(aUser) != reservedSlots.end(); 
}

bool UploadManager::isNotifiedUser(const UserPtr& aUser) const noexcept {
	return notifiedUsers.find(aUser) != notifiedUsers.end(); 
}

UploadManager::SlotQueue UploadManager::getUploadQueue() const noexcept {
	RLock l(cs); 
	return uploadQueue; 
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

	notifyQueuedUsers();
	fire(UploadManagerListener::QueueUpdate());
}

void UploadManager::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool aWentOffline) noexcept {
	if (aWentOffline) {
		clearUserFiles(aUser, true);
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