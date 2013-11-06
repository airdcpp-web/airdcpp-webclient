/* 
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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
#include "UploadManager.h"

#include <cmath>

#include "AdcCommand.h"
#include "BZUtils.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "CryptoManager.h"
#include "FavoriteManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "ShareManager.h"
#include "Upload.h"
#include "UploadBundle.h"
#include "UserConnection.h"

namespace dcpp {

using boost::range::find_if;

UploadManager::UploadManager() noexcept : running(0), extra(0), lastGrant(0), lastFreeSlots(-1), extraPartial(0), mcnSlots(0), smallSlots(0) {	
	ClientManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
}

UploadManager::~UploadManager() {
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);
	{
		WLock l(cs);
		for(const auto ii: uploadQueue) {
			for(const auto& f: ii.files) {
				f->dec();
			}
		}
		uploadQueue.clear();
	}

	while(true) {
		{
			RLock l(cs);
			if(uploads.empty())
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

uint8_t UploadManager::getSlots() const { 
	return (uint8_t)(max(AirUtil::getSlots(false), max(SETTING(HUB_SLOTS),0) * Client::getTotalCounts())); 
}

uint8_t UploadManager::getFreeSlots() const { 
	return (uint8_t)max((getSlots() - running), 0); 
}

int UploadManager::getFreeExtraSlots() const { 
	return max(SETTING(EXTRA_SLOTS) - getExtra(), 0); 
}

bool UploadManager::prepareFile(UserConnection& aSource, const string& aType, const string& aFile, int64_t aStartPos, int64_t& aBytes, const string& userSID, bool listRecursive, bool tthList) {
	dcdebug("Preparing %s %s " I64_FMT " " I64_FMT " %d" " " "%s %s\n", aType.c_str(), aFile.c_str(), aStartPos, aBytes, listRecursive, 
		aSource.getHubUrl().c_str(), ClientManager::getInstance()->getFormatedNicks(aSource.getHintedUser()).c_str());

	if(aFile.empty() || aStartPos < 0 || aBytes < -1 || aBytes == 0) {
		aSource.sendError("Invalid request");
		return false;
	}

	/*check if the user deserves a slot before any filesystem access*/

	bool userlist = (aFile == Transfer::USER_LIST_NAME_BZ || aFile == Transfer::USER_LIST_NAME);
	bool miniSlot = userlist;
	bool partialFileSharing = false;
	
	string sourceFile;
	Transfer::Type type;
	int64_t fileSize = 0;
	bool noAccess = false;

	//make sure that we have an user
	auto profile = ClientManager::getInstance()->findProfile(aSource, userSID);
	if (!profile) {
		aSource.sendError("Unknown user", AdcCommand::ERROR_UNKNOWN_USER);
		return false;
	}

	try {
		if(aType == Transfer::names[Transfer::TYPE_FILE]) {
			type = userlist ? Transfer::TYPE_FULL_LIST : Transfer::TYPE_FILE;

			//check that we have a file
			if (userlist) {
				auto info = move(ShareManager::getInstance()->getFileListInfo(aFile, *profile));
				sourceFile = move(info.second);
				fileSize = info.first;
			} else {
				//get all hubs with file transfers
				ProfileTokenSet profiles;
				ClientManager::getInstance()->listProfiles(aSource.getHintedUser().user, profiles);
				if (profiles.empty()) {
					//the user managed to go offline already?
					profiles.insert(*profile);
				}

				ShareManager::getInstance()->toRealWithSize(aFile, profiles, aSource.getHintedUser(), sourceFile, fileSize, noAccess);

				miniSlot = freeSlotMatcher.match(Util::getFileName(sourceFile));
			}

			miniSlot = miniSlot || (fileSize <= Util::convertSize(SETTING(SET_MINISLOT_SIZE), Util::KB));
		} else if(aType == Transfer::names[Transfer::TYPE_TREE]) {
			ProfileTokenSet profiles;
			ClientManager::getInstance()->listProfiles(aSource.getHintedUser().user, profiles);
			if (profiles.empty()) {
				//the user managed to go offline already?
				profiles.insert(*profile);
			}

			ShareManager::getInstance()->toRealWithSize(aFile, profiles, aSource.getHintedUser(), sourceFile, fileSize, noAccess);
			type = Transfer::TYPE_TREE;
			miniSlot = true;

		} else if(aType == Transfer::names[Transfer::TYPE_PARTIAL_LIST]) {
			type = Transfer::TYPE_PARTIAL_LIST;
			miniSlot = true;
		} else {
			aSource.sendError("Unknown file type");
			return false;
		}
	} catch(const ShareException& e) {
		// Partial sharing
		if (aFile.compare(0, 4, "TTH/") == 0 && SETTING(USE_PARTIAL_SHARING)) {
			TTHValue fileHash(aFile.substr(4));
			if (aType == Transfer::names[Transfer::TYPE_TREE]) {
				// The size isn't relevant here
				auto targets = QueueManager::getInstance()->getTargets(fileHash);
				if (!targets.empty()) {
					sourceFile = targets.front();
					partialFileSharing = true;
					type = Transfer::TYPE_TREE;
					miniSlot = true;
					goto checkslots;
				}
			} else if (aType == Transfer::names[Transfer::TYPE_FILE]) {
				if (QueueManager::getInstance()->isChunkDownloaded(fileHash, aStartPos, aBytes, fileSize, sourceFile)) {
					partialFileSharing = true;
					type = Transfer::TYPE_FILE;
					goto checkslots;
				}
			} else {
				aSource.sendError("Invalid file type");
				return false;
			}
		}

		aSource.sendError(e.getError(), noAccess ? AdcCommand::ERROR_FILE_ACCESS_DENIED : AdcCommand::ERROR_FILE_NOT_AVAILABLE);
		return false;
	}

checkslots:

	uint8_t slotType = aSource.getSlotType();
	
	bool noSlots = false;
	if (slotType != UserConnection::STDSLOT && slotType != UserConnection::MCNSLOT) {
		bool isFavorite = FavoriteManager::getInstance()->hasSlot(aSource.getUser());
		{
			WLock l(cs);
			bool hasReserved = reservedSlots.find(aSource.getUser()) != reservedSlots.end();
			bool hasFreeSlot = (getFreeSlots() > 0) && ((uploadQueue.empty() && notifiedUsers.empty()) || isNotifiedUser(aSource.getUser()));
		
			if ((type==Transfer::TYPE_PARTIAL_LIST || (type != Transfer::TYPE_FULL_LIST && fileSize <= 65792)) && smallSlots <= 8) {
				slotType = UserConnection::SMALLSLOT;
			} else if (aSource.isSet(UserConnection::FLAG_MCN1)) {
				if (getMultiConn(aSource) || ((hasReserved || isFavorite|| getAutoSlot()) && !isUploading(aSource.getUser()))) {
					slotType = UserConnection::MCNSLOT;
				} else {
					noSlots=true;
				}
			} else if (!(hasReserved || isFavorite || hasFreeSlot || getAutoSlot())) {
				noSlots=true;
			} else {
				slotType = UserConnection::STDSLOT;
			}
		}

		if (noSlots) {
			bool supportsFree = aSource.isSet(UserConnection::FLAG_SUPPORTS_MINISLOTS);
			bool allowedFree = (slotType == UserConnection::EXTRASLOT) || aSource.isSet(UserConnection::FLAG_OP) || getFreeExtraSlots() > 0;
			bool partialFree = partialFileSharing && ((slotType == UserConnection::PARTIALSLOT) || (extraPartial < SETTING(EXTRA_PARTIAL_SLOTS)));

			if(miniSlot && supportsFree && allowedFree) {
				slotType = UserConnection::EXTRASLOT;
			} else if(partialFree) {
				slotType = UserConnection::PARTIALSLOT;
			} else {
				if (aSource.isSet(UserConnection::FLAG_MCN1) && isUploading(aSource.getUser())) {
					//don't queue MCN requests for existing uploaders
					aSource.maxedOut();
				} else {
					aSource.maxedOut(addFailedUpload(aSource, sourceFile, aStartPos, fileSize));
				}
				aSource.disconnect();
				return false;
			}
		}

		setLastGrant(GET_TICK());
	}
	
	unique_ptr<InputStream> is = nullptr;
	int64_t start = 0;
	int64_t size = 0;
	Upload* u = nullptr;

	try {
		auto countFilePositions = [&] () -> void {
			start = aStartPos;
			size = (aBytes == -1) ? fileSize - start : aBytes;

			if((start + size) > fileSize) {
				throw Exception("Bytes were requested beyond the end of the file");
			}
		};


		{
			//are we resuming an existing upload?
			WLock l(cs);
			auto i = find_if(delayUploads, [&aSource](const Upload* up) { return &aSource == &up->getUserConnection(); });
			if (i != delayUploads.end()) {
				Upload* up = *i;
				delayUploads.erase(i);

				if(sourceFile != up->getPath()) {
					if (up->isSet(Upload::FLAG_CHUNKED) && up->getSegment().getEnd() != up->getSize())
						logUpload(up);
				} else if (up->getType() == Transfer::TYPE_FILE && type == Transfer::TYPE_FILE && up->getSegment().getEnd() != fileSize) {
					//we are resuming the same file, reuse the existing upload
					countFilePositions();
					up->resume(start, size);
					dcassert(aSource.getUpload());
					uploads.push_back(up);
					goto end;
				}

				delete up;
			}
		}

		// a new upload
		switch(type) {
		case Transfer::TYPE_FILE:
			// handle below...
		case Transfer::TYPE_FULL_LIST:
			{
				if(aFile == Transfer::USER_LIST_NAME) {
					// Unpack before sending...
					string bz2 = File(sourceFile, File::READ, File::OPEN).read();
					string xml;
					CryptoManager::getInstance()->decodeBZ2(reinterpret_cast<const uint8_t*>(bz2.data()), bz2.size(), xml);
					// Clear to save some memory...
					string().swap(bz2);
					is.reset(new MemoryInputStream(xml));
					start = 0;
					fileSize = size = xml.size();
				} else {
					countFilePositions();
					unique_ptr<File> f(new File(sourceFile, File::READ, File::OPEN | File::SHARED_WRITE)); // write for partial sharing
			
					f->setPos(start);
					is = move(f);
					if((start + size) < fileSize) {
						is.reset(new LimitedInputStream<true>(is.release(), size));
					}
				}
				break;
			}
		case Transfer::TYPE_TREE:
			{
				sourceFile = aFile;
				unique_ptr<MemoryInputStream> mis(ShareManager::getInstance()->getTree(aFile, *profile));
				if(!mis.get()) {
					aSource.sendError();
					return false;
				}

				start = 0;
				fileSize = size = mis->getSize();
				is = move(mis);
				break;
			}
		case Transfer::TYPE_PARTIAL_LIST:
			{
				unique_ptr<MemoryInputStream> mis = nullptr;
				// Partial file list
				if (tthList) {
					if (aFile[0] != '/') {
						mis.reset(QueueManager::getInstance()->generateTTHList(aFile, *profile != SP_HIDDEN));
					} else {
						mis.reset(ShareManager::getInstance()->generateTTHList(aFile, listRecursive, *profile));
					}
				} else {
					mis.reset(ShareManager::getInstance()->generatePartialList(aFile, listRecursive, *profile));
				}

				if(!mis.get()) {
					aSource.sendError();
					return false;
				}
				start = 0;
				fileSize = size = mis->getSize();
				is = move(mis);
				break;
			}
		default:
			dcassert(0);
		}
	} catch(const ShareException& e) {
		aSource.sendError(e.getError());
		return false;
	} catch(const QueueException& e) {
		aSource.sendError(e.getError());
		return false;
	} catch(const Exception& e) {
		if (!e.getError().empty())
			LogManager::getInstance()->message(STRING(UNABLE_TO_SEND_FILE) + " " + sourceFile + ": " + e.getError() + " (" + (ClientManager::getInstance()->getFormatedNicks(aSource.getHintedUser()) + ")"), LogManager::LOG_ERROR);
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

	u = new Upload(aSource, sourceFile, TTHValue(), move(is));
	u->setSegment(Segment(start, size));
	if(u->getSegment().getEnd() != fileSize)
		u->setFlag(Upload::FLAG_CHUNKED);
	if(partialFileSharing)
		u->setFlag(Upload::FLAG_PARTIAL);
	u->setFileSize(fileSize);
	u->setType(type);

	{
		auto bundle = !aSource.getLastBundle().empty() ? findBundle(aSource.getLastBundle()) : nullptr;
		{
			WLock l(cs);
			uploads.push_back(u);
			if (bundle) {
				bundle->addUpload(u);
			}
		}
	}
end:
	UpdateSlotCounts(aSource, slotType);
	return true;
}

void UploadManager::UpdateSlotCounts(UserConnection& aSource, uint8_t slotType){
	if(aSource.getSlotType() != slotType) {
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
		aSource.setSlotType(slotType);

		// set new slot count
		switch(slotType) {
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

void UploadManager::changeMultiConnSlot(const UserPtr& aUser, bool remove) {
	WLock l(cs);
	auto uis = multiUploads.find(aUser);
	if (uis != multiUploads.end()) {
		if (remove) {
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
	} else if (!remove) {
		//a new MCN upload
		multiUploads[aUser] = 1;
		running++;
		mcnSlots++;
	}
}

bool UploadManager::getMultiConn(const UserConnection& aSource) {
	//inside a lock.
	UserPtr u = aSource.getUser();

	bool hasFreeSlot=false;
	if ((int)(getSlots() - running - mcnSlots + multiUploads.size()) > 0) {
		if ((uploadQueue.empty() && notifiedUsers.empty()) || isNotifiedUser(aSource.getUser())) {
			hasFreeSlot=true;
		}
	}

	if (!multiUploads.empty()) {
		uint16_t highest=0;
		for(auto& i: multiUploads) {
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

void UploadManager::checkMultiConn() {
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

void UploadManager::onUBN(const AdcCommand& cmd) {
	string bundleToken;
	string hubIpPort;
	float percent = -1;
	string speedStr;

	for(const auto& str: cmd.getParameters()) {
		if(str.compare(0, 2, "HI") == 0) {
			hubIpPort = str.substr(2);
		} else if(str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		} else if(str.compare(0, 2, "DS") == 0) {
			speedStr = str.substr(2);
		} else if (str.compare(0, 2, "PE") == 0) {
			percent = Util::toFloat(str.substr(2));
		} else {
			//LogManager::getInstance()->message("ONUBN UNKNOWN PARAM: " + str);
		}
	}

	if ((percent < 0.00 && speedStr.empty()) || bundleToken.empty()) {
		return;
	}

	UploadBundlePtr bundle = findBundle(bundleToken);
	if (bundle) {
		if (bundle->getSingleUser()) {
			//LogManager::getInstance()->message("SINGLEUSER, RETURN");
			return;
		}

		if (!speedStr.empty() && speedStr.length() > 2) {
			size_t length = speedStr.length();
			double downloaded = Util::toDouble(speedStr.substr(0, length-1));
			if (downloaded > 0) {
				uint64_t speed = 0;
				if (speedStr[length-1] == 'k') {
					speed = downloaded*1024.0;
				} else if (speedStr[length-1] == 'm') {
					speed = downloaded*1048576.0;
				} else if (speedStr[length-1] == 'b') {
					speed = downloaded;
				}

				if (speed > 0) {
					bundle->setTotalSpeed(speed);
				}
			}
		}

		if (percent >= 0.00 && percent <= 100.00) {
			bundle->setUploadedSegments(bundle->getSize()*(percent / 100.00000));
		}
	}
}

void UploadManager::createBundle(const AdcCommand& cmd) {
	string bundleToken;
	string hubIpPort;
	string token;
	string name;
	int64_t size=0, downloaded=0;
	bool singleUser = false;

	for(const auto& str: cmd.getParameters()) {
		if(str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		} else if(str.compare(0, 2, "TO") == 0) {
			token = str.substr(2);
		} else if(str.compare(0, 2, "SI") == 0) {
			size = Util::toInt64(str.substr(2));
		} else if (str.compare(0, 2, "NA") == 0) {
			name = str.substr(2);
		} else if (str.compare(0, 2, "DL") == 0) {
			downloaded = Util::toInt64(str.substr(2));
		} else if (str.compare(0, 2, "SU") == 0) {
			singleUser = true;
		} else {
			//LogManager::getInstance()->message("ONUBD CREATE UNKNOWN PARAM");
		}
	}
	
	if (bundleToken.empty() || name.empty() || size <= 0 || token.empty()) {
		//LogManager::getInstance()->message("INVALID UBD1", LogManager::LOG_ERROR);
		return;
	} else if (!ConnectionManager::getInstance()->tokens.addToken(bundleToken)) {
		return;
	}

	//dcassert(!findBundle(bundleToken));
	if (findBundle(bundleToken)) {
		//LogManager::getInstance()->message("ADDBUNDLE, BUNDLE FOUND!");
		changeBundle(cmd);
		return;
	}

	UploadBundlePtr bundle = UploadBundlePtr(new UploadBundle(name, bundleToken, size, singleUser, downloaded));
	{
		WLock l (cs);
		Upload* u = findUpload(token);
		if (u) {
			bundle->addUpload(u);
			bundle->findBundlePath(name);
			bundles[bundle->getToken()] = bundle;
			u->getUserConnection().setLastBundle(bundleToken);
			return;
		}
		//LogManager::getInstance()->message("ADDBUNDLE, BUNDLE ADDED!");
	}

	//no upload
	if (ConnectionManager::getInstance()->setBundle(token, bundleToken)) {
		WLock l (cs);
		bundles[bundle->getToken()] = bundle;
	}
}

void UploadManager::updateBundleInfo(const AdcCommand& cmd) {
	string bundleToken;
	string name;
	int64_t size=0, downloaded=0;
	bool singleUser = false, multiUser = false;

	for(const auto& str: cmd.getParameters()) {
		if(str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		} else if(str.compare(0, 2, "SI") == 0) {
			size = Util::toInt64(str.substr(2));
		} else if (str.compare(0, 2, "NA") == 0) {
			name = str.substr(2);
		}  else if (str.compare(0, 2, "SU") == 0) {
			singleUser = true;
		} else if (str.compare(0, 2, "MU") == 0) {
			multiUser = true;
		} else if (str.compare(0, 2, "DL") == 0) {
			downloaded = Util::toInt64(str.substr(2));
		} else {
			//LogManager::getInstance()->message("ONUBD UPDATE UNKNOWN PARAM: " + str);
		}
	}

	if (bundleToken.empty()) {
		//LogManager::getInstance()->message("INVALID UBD1: UPDATE");
		return;
	}

	UploadBundlePtr bundle = findBundle(bundleToken);
	dcassert(bundle);
	if (bundle) {
		if (multiUser) {
			bundle->setSingleUser(false);
		} else if (singleUser) {
			bundle->setSingleUser(true, downloaded);
		} else {
			if (size > 0) {
				bundle->setSize(size);
			}
			if (!name.empty()) {
				bundle->findBundlePath(name);
			}
			fire(UploadManagerListener::BundleSizeName(), bundle->getToken(), bundle->getTarget(), bundle->getSize());
		}
	}
}

void UploadManager::changeBundle(const AdcCommand& cmd) {
	string bundleToken;
	string token;

	for(const auto& str: cmd.getParameters()) {
		if(str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		} else if(str.compare(0, 2, "TO") == 0) {
			token = str.substr(2);
		} else {
			//LogManager::getInstance()->message("ONUBD CHANGE UNKNOWN PARAM");
		}
	}
	
	if (bundleToken.empty() || token.empty()) {
		//LogManager::getInstance()->message("INVALID UBD1: CHANGE", LogManager::LOG_ERROR);
		return;
	}

	UploadBundlePtr b = findBundle(bundleToken);
	dcassert(b);

	if (b) {
		{
			WLock l (cs);
			Upload* u = findUpload(token);
			if (u) {
				b->addUpload(u);
				u->getUserConnection().setLastBundle(bundleToken);
				return;
			}
		}

		//no upload
		ConnectionManager::getInstance()->setBundle(token, bundleToken);
	}
}


void UploadManager::finishBundle(const AdcCommand& cmd) {
	string bundleToken;

	for(const auto& str: cmd.getParameters()) {
		if(str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
			break;
		}
	}
	
	if (bundleToken.empty()) {
		//LogManager::getInstance()->message("INVALID UBD1: FINISH", LogManager::LOG_ERROR);
		return;
	}

	UploadBundlePtr bundle = findBundle(bundleToken);
	//dcassert(!bundle);

	if (bundle) {
		{
			WLock l (cs);
			bundles.erase(bundle->getToken());
		}

		ConnectionManager::getInstance()->tokens.removeToken(bundle->getToken());
		fire(UploadManagerListener::BundleComplete(), bundle->getToken(), bundle->getName());
	}
}

void UploadManager::removeBundleConnection(const AdcCommand& cmd) {
	string token;

	for(const auto& str: cmd.getParameters()) {
		if(str.compare(0, 2, "TO") == 0) {
			token = str.substr(2);
		}
	}

	if (!token.empty()) {
		WLock l (cs);
		Upload* u = findUpload(token);
		if (u && u->getBundle()) {
			u->getBundle()->removeUpload(u);
			u->getUserConnection().setLastBundle(Util::emptyString);
		}
	}
}

void UploadManager::onUBD(const AdcCommand& cmd) {

	if (cmd.hasFlag("AD", 1)) {
		//LogManager::getInstance()->message("ADD UPLOAD BUNDLE");
		createBundle(cmd);
	} else if (cmd.hasFlag("CH", 1)) {
		//LogManager::getInstance()->message("CHANGE UPLOAD BUNDLE");
		changeBundle(cmd);
	} else if (cmd.hasFlag("UD", 1)) {
		//LogManager::getInstance()->message("UPDATE UPLOAD BUNDLE");
		updateBundleInfo(cmd);
	} else if (cmd.hasFlag("FI", 1)) {
		//LogManager::getInstance()->message("FINISH UPLOAD BUNDLE");
		finishBundle(cmd);
	} else if (cmd.hasFlag("RM", 1)) {
		//LogManager::getInstance()->message("REMOVE UPLOAD BUNDLE");
		removeBundleConnection(cmd);
	} else {
		//LogManager::getInstance()->message("NO FLAG");
	}
}

UploadBundlePtr UploadManager::findBundle(const string& aBundleToken) {
	RLock l(cs);
	auto s = bundles.find(aBundleToken);
	if (s != bundles.end()) {
		return s->second;
	}
	return nullptr;
}

Upload* UploadManager::findUpload(const string& aToken) {
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


int64_t UploadManager::getRunningAverage(bool lock) {
	int64_t avg = 0;

	ConditionalRLock l(cs, lock);
	for (auto u: uploads)
		avg += static_cast<int64_t>(u->getAverageSpeed()); 
	return avg;
}

bool UploadManager::getAutoSlot() {
	/** A 0 in settings means disable */
	if(AirUtil::getSpeedLimit(false) == 0)
		return false;
	/** Max slots */
	if(getSlots() + AirUtil::getMaxAutoOpened() < running)
		return false;		
	/** Only grant one slot per 30 sec */
	if(GET_TICK() < getLastGrant() + 30*1000)
		return false;
	/** Grant if upload speed is less than the threshold speed */
	return getRunningAverage(false) < Util::convertSize(AirUtil::getSpeedLimit(false), Util::KB);
}

void UploadManager::removeUpload(Upload* aUpload, bool delay) {
	WLock l(cs);
	dcassert(find(uploads.begin(), uploads.end(), aUpload) != uploads.end());
	uploads.erase(remove(uploads.begin(), uploads.end(), aUpload), uploads.end());

	if(delay) {
		delayUploads.push_back(aUpload);
	} else {
		delete aUpload;
	}
}

void UploadManager::reserveSlot(const HintedUser& aUser, uint64_t aTime) {
	bool connect = false;
	string token;
	{
		WLock l(cs);
		reservedSlots[aUser] = aTime > 0 ? (GET_TICK() + aTime*1000) : 0;
	
		if(aUser.user->isOnline()){
			// find user in uploadqueue to connect with correct token
			auto it = find_if(uploadQueue.cbegin(), uploadQueue.cend(), [&](const UserPtr& u) { return u == aUser.user; });
			if(it != uploadQueue.cend()) {
				token = it->token;
				connect = true;
			}
		}
	}

	if(connect) {
		connectUser(aUser, token);
	}

	fire(UploadManagerListener::SlotsUpdated(), aUser);
}

void UploadManager::connectUser(const HintedUser& aUser, const string& aToken) {
	string lastError;
	string hubUrl = aUser.hint;
	bool protocolError = false;
	ClientManager::getInstance()->connect(aUser.user, aToken, true, lastError, hubUrl, protocolError);

	//TODO: report errors?
}

void UploadManager::unreserveSlot(const UserPtr& aUser) {
	bool found = false;
	{
		WLock l(cs);
		auto uis = reservedSlots.find(aUser);
		if(uis != reservedSlots.end()){
			reservedSlots.erase(uis);
			found = true;
		}
	}
	if(found)
		fire(UploadManagerListener::SlotsUpdated(), aUser);
}

void UploadManager::on(UserConnectionListener::Get, UserConnection* aSource, const string& aFile, int64_t aResume) noexcept {
	if(aSource->getState() != UserConnection::STATE_GET) {
		dcdebug("UM::onGet Bad state, ignoring\n");
		return;
	}
	
	int64_t bytes = -1;
	if(prepareFile(*aSource, Transfer::names[Transfer::TYPE_FILE], Util::toAdcFile(aFile), aResume, bytes, Util::emptyString)) {
		aSource->setState(UserConnection::STATE_SEND);
		aSource->fileLength(Util::toString(aSource->getUpload()->getSize()));
	}
}

void UploadManager::on(UserConnectionListener::Send, UserConnection* aSource) noexcept {
	if(aSource->getState() != UserConnection::STATE_SEND) {
		dcdebug("UM::onSend Bad state, ignoring\n");
		return;
	}

	Upload* u = aSource->getUpload();
	dcassert(u);

	u->setStart(GET_TICK());
	u->tick();
	aSource->setState(UserConnection::STATE_RUNNING);
	aSource->transmitFile(u->getStream());
	fire(UploadManagerListener::Starting(), u);
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


	if(prepareFile(*aSource, type, fname, aStartPos, aBytes, userSID, c.hasFlag("RE", 4), c.hasFlag("TL", 4))) {
		Upload* u = aSource->getUpload();
		dcassert(u);

		AdcCommand cmd(AdcCommand::CMD_SND);
		cmd.addParam(type).addParam(fname)
			.addParam(Util::toString(u->getStartPos()))
			.addParam(Util::toString(u->getSize()));

		if(c.hasFlag("ZL", 4)) {
			u->setFiltered();
			cmd.addParam("ZL1");
		}
		if(c.hasFlag("TL", 4) && type == Transfer::names[Transfer::TYPE_PARTIAL_LIST]) {
			cmd.addParam("TL1");	 
		}

		aSource->send(cmd);
		
		if (!u->isSet(Upload::FLAG_RESUMED))
			u->setStart(GET_TICK());

		u->tick();
		aSource->setState(UserConnection::STATE_RUNNING);
		aSource->transmitFile(u->getStream());
		fire(UploadManagerListener::Starting(), u);
	}
}

void UploadManager::on(UserConnectionListener::BytesSent, UserConnection* aSource, size_t aBytes, size_t aActual) noexcept {
	dcassert(aSource->getState() == UserConnection::STATE_RUNNING);
	Upload* u = aSource->getUpload();
	dcassert(u);
	u->addPos(aBytes, aActual);
	u->tick();
}

void UploadManager::on(UserConnectionListener::Failed, UserConnection* aSource, const string& aError) noexcept {
	Upload* u = aSource->getUpload();

	if(u) {
		fire(UploadManagerListener::Failed(), u, aError);

		dcdebug("UM::onFailed (%s): Removing upload\n", aError.c_str());
		removeUpload(u);
	}

	removeConnection(aSource);
}

void UploadManager::on(UserConnectionListener::TransmitDone, UserConnection* aSource) noexcept {
	dcassert(aSource->getState() == UserConnection::STATE_RUNNING);
	Upload* u = aSource->getUpload();
	dcassert(u);

	aSource->setState(UserConnection::STATE_GET);

	bool partialSegmentFinished = u->isSet(Upload::FLAG_CHUNKED) && u->getSegment().getEnd() != u->getFileSize();
	if(!partialSegmentFinished) {
		logUpload(u);
	}

	removeUpload(u, partialSegmentFinished || u->getBundle());
}

void UploadManager::logUpload(const Upload* u) {
	if(SETTING(LOG_UPLOADS) && u->getType() != Transfer::TYPE_TREE && (SETTING(LOG_FILELIST_TRANSFERS) || u->getType() != Transfer::TYPE_FULL_LIST)) {
		ParamMap params;
		u->getParams(u->getUserConnection(), params);
		LOG(LogManager::UPLOAD, params);
	}

	fire(UploadManagerListener::Complete(), u);
}

size_t UploadManager::addFailedUpload(const UserConnection& source, const string& aFile, int64_t pos, int64_t size) {
	size_t queue_position = 0;
	WLock l(cs);
	auto it = find_if(uploadQueue.begin(), uploadQueue.end(), [&](const UserPtr& u) -> bool { ++queue_position; return u == source.getUser(); });
	if(it != uploadQueue.end()) {
		it->token = source.getToken();
		for(const auto f: it->files) {
			if(f->getFile() == aFile) {
				f->setPos(pos);
				return queue_position;
			}
		}
	}

	UploadQueueItem* uqi = new UploadQueueItem(source.getHintedUser(), aFile, pos, size);
	if(it == uploadQueue.end()) {
		++queue_position;

		WaitingUser wu(source.getHintedUser(), source.getToken());
		wu.files.insert(uqi);
		uploadQueue.push_back(wu);
	} else {
		it->files.insert(uqi);
	}

	fire(UploadManagerListener::QueueAdd(), uqi);
	return queue_position;
}

void UploadManager::clearUserFiles(const UserPtr& aUser, bool lock) {
	
	ConditionalWLock l (cs, lock);
	auto it = find_if(uploadQueue, [&](const UserPtr& u) { return u == aUser; });
	if(it != uploadQueue.end()) {
		for(const auto f: it->files) {
			fire(UploadManagerListener::QueueItemRemove(), f);
			f->dec();
		}
		uploadQueue.erase(it);
		fire(UploadManagerListener::QueueRemove(), aUser);
	}
}

void UploadManager::addConnection(UserConnectionPtr conn) {
	conn->addListener(this);
	conn->setState(UserConnection::STATE_GET);
}
	
void UploadManager::removeConnection(UserConnection* aSource) {
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

void UploadManager::notifyQueuedUsers() {
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
				WaitingUser wu = uploadQueue.front();
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
	UserList disconnects;
	vector<UserPtr> reservedRemoved;
	{
		WLock l(cs);
		for(auto j = reservedSlots.begin(); j != reservedSlots.end();) {
			if((j->second > 0) && j->second < aTick) {
				reservedRemoved.push_back(j->first);
				reservedSlots.erase(j++);
			} else {
				++j;
			}
		}
	
		for(auto i = notifiedUsers.begin(); i != notifiedUsers.end();) {
			if((i->second + (90 * 1000)) < aTick) {
				clearUserFiles(i->first, false);
				i = notifiedUsers.erase(i);
			} else
				++i;
		}

		if( SETTING(AUTO_KICK) ) {
			for(auto u: uploads) {
				if(u->getUser()->isOnline()) {
					u->unsetFlag(Upload::FLAG_PENDING_KICK);
					continue;
				}

				if(u->isSet(Upload::FLAG_PENDING_KICK)) {
					disconnects.push_back(u->getUser());
					continue;
				}

				if(SETTING(AUTO_KICK_NO_FAVS) && u->getUser()->isFavorite()) {
					continue;
				}

				u->setFlag(Upload::FLAG_PENDING_KICK);
			}
		}
	}
		
	for(auto& u: disconnects) {
		LogManager::getInstance()->message(STRING(DISCONNECTED_USER) + " " + Util::listToString(ClientManager::getInstance()->getNicks(u->getCID())), LogManager::LOG_INFO);
		ConnectionManager::getInstance()->disconnect(u, false);
	}

	int freeSlots = getFreeSlots();
	if(freeSlots != lastFreeSlots) {
		lastFreeSlots = freeSlots;
	}

	for(auto& u: reservedRemoved)
		fire(UploadManagerListener::SlotsUpdated(), u);
}

void UploadManager::on(GetListLength, UserConnection* conn) noexcept { 
	conn->error("GetListLength not supported");
	conn->disconnect(false);
}

size_t UploadManager::getUploadCount() const { 
	RLock l(cs); 
	return uploads.size(); 
}

bool UploadManager::hasReservedSlot(const UserPtr& aUser) const { 
	RLock l(cs); 
	return reservedSlots.find(aUser) != reservedSlots.end(); 
}

bool UploadManager::isNotifiedUser(const UserPtr& aUser) const { 
	return notifiedUsers.find(aUser) != notifiedUsers.end(); 
}

UploadManager::SlotQueue UploadManager::getUploadQueue() const { 
	RLock l(cs); 
	return uploadQueue; 
}

//todo check all users hubs when sending.
void UploadManager::on(AdcCommand::GFI, UserConnection* aSource, const AdcCommand& c) noexcept {
	if(aSource->getState() != UserConnection::STATE_GET) {
		dcdebug("UM::onSend Bad state, ignoring\n");
		return;
	}
	
	if(c.getParameters().size() < 2) {
		aSource->send(AdcCommand(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_PROTOCOL_GENERIC, "Missing parameters"));
		return;
	}

	auto shareProfile = ClientManager::getInstance()->findProfile(*aSource, Util::emptyString);
	if (shareProfile) {
		const string& type = c.getParam(0);
		const string& ident = c.getParam(1);

		if(type == Transfer::names[Transfer::TYPE_FILE]) {
			try {
				aSource->send(ShareManager::getInstance()->getFileInfo(ident, *shareProfile));
				return;
			} catch(const ShareException&) { }
		}
	}

	aSource->sendError();
}

// TimerManagerListener
void UploadManager::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	UploadList ticks;
	UploadBundleList tickBundles;
	{
		WLock l(cs);
		for(auto i = delayUploads.begin(); i != delayUploads.end();) {
			Upload* u = *i;
			if(++u->delayTime > 10) {
				if (u->isSet(Upload::FLAG_CHUNKED) && u->getSegment().getEnd() != u->getSize())
					logUpload(u);
				
				delete u;
				i = delayUploads.erase(i);
			} else {
				i++;
			}
		}

		for(auto i = bundles.begin(); i != bundles.end();) {
			UploadBundlePtr ub = i->second;
			if (ub->getUploads().empty() && ++ub->delayTime > 10) {
				ConnectionManager::getInstance()->tokens.removeToken((*i).second->getToken());
				i = bundles.erase(i);
			} else {
				if (ub->countSpeed() > 0)
					tickBundles.push_back(ub);
				i++;
			}
		}

		for(auto u: uploads) {
			if(u->getPos() > 0) {
				ticks.push_back(u);
				u->tick();
			}
		}
		
		if(ticks.size() > 0)
			fire(UploadManagerListener::Tick(), ticks);

		if (!tickBundles.empty())
			fire(UploadManagerListener::BundleTick(), tickBundles);


	}

	notifyQueuedUsers();
	fire(UploadManagerListener::QueueUpdate());
}

void UploadManager::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept {
	if(wentOffline) {
		clearUserFiles(aUser, true);
	}
}

void UploadManager::removeDelayUpload(const UserConnection& aSource) {
	WLock l(cs);
	auto i = find_if(delayUploads.begin(), delayUploads.end(), [&](Upload* up) { return &aSource == &up->getUserConnection(); });
	if (i != delayUploads.end()) {
		Upload* up = *i;
		delayUploads.erase(i);
		dcassert(find_if(uploads.begin(), uploads.end(), [&](Upload* up) { return &aSource == &up->getUserConnection(); }) == uploads.end());
		delete up;
		return;
	}
	//dcassert(find_if(uploads.begin(), uploads.end(), [&](Upload* up) { return &aSource == &up->getUserConnection(); }) != uploads.end());
}

/**
 * Abort upload of specific file
 */
void UploadManager::abortUpload(const string& aFile, bool waiting){
	bool fileRunning = false;

	{
		RLock l(cs);

		//delayUploads also keep the file open...
		for(auto u: delayUploads) {
			if(u->getPath() == aFile) {
				u->getUserConnection().disconnect(true);
				fileRunning = true;
			}
		}

		for(auto u: uploads) {
			if(u->getPath() == aFile) {
				u->getUserConnection().disconnect(true);
				fileRunning = true;
			}
		}
	}
	
	if(!fileRunning) return;
	if(!waiting) return;
	
	for(int i = 0; i < 20 && fileRunning == true; i++){
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
	
	if(fileRunning)
		LogManager::getInstance()->message("Aborting an upload " + aFile + " timed out", LogManager::LOG_ERROR);
		//dcdebug("abort upload timeout %s\n", aFile.c_str());

	//LogManager::getInstance()->message("Aborting an upload " + aFile + " timed out", LogManager::LOG_ERROR);
}

} // namespace dcpp