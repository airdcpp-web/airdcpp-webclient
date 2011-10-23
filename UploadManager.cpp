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
#include "UploadManager.h"

#include <cmath>

#include "ConnectionManager.h"
#include "LogManager.h"
#include "ShareManager.h"
#include "ClientManager.h"
#include "FilteredFile.h"
#include "ZUtils.h"
#include "ResourceManager.h"
#include "HashManager.h"
#include "AdcCommand.h"
#include "FavoriteManager.h"
#include "CryptoManager.h"
#include "Upload.h"
#include "UploadBundle.h"
#include "UserConnection.h"
#include "QueueManager.h"
#include "FinishedManager.h"
#include "SharedFileStream.h"

#include "Wildcards.h"

namespace dcpp {
	
static const string UPLOAD_AREA = "Uploads";

UploadManager::UploadManager() noexcept : running(0), extra(0), lastGrant(0), lastFreeSlots(-1), extraPartial(0), mcnSlots(0), smallSlots(0) {	
	ClientManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
}

UploadManager::~UploadManager() {
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);
	{
		Lock l(cs);
		for(auto ii = uploadQueue.cbegin(); ii != uploadQueue.cend(); ++ii) {
			for(auto i = ii->files.cbegin(); i != ii->files.cend(); ++i) {
				(*i)->dec();
			}
		}
		uploadQueue.clear();
	}

	while(true) {
		{
			Lock l(cs);
			if(uploads.empty())
				break;
		}
		Thread::sleep(100);
	}
}

bool UploadManager::prepareFile(UserConnection& aSource, const string& aType, const string& aFile, int64_t aStartPos, int64_t& aBytes, bool listRecursive, bool tthList) {
	dcdebug("Preparing %s %s " I64_FMT " " I64_FMT " %d\n", aType.c_str(), aFile.c_str(), aStartPos, aBytes, listRecursive);

	if(aFile.empty() || aStartPos < 0 || aBytes < -1 || aBytes == 0) {
		aSource.fileNotAvail("Invalid request");
		return false;
	}
	
	InputStream* is = 0;
	int64_t start = 0;
	int64_t size = 0;
	int64_t fileSize = 0;

	bool userlist = (aFile == Transfer::USER_LIST_NAME_BZ || aFile == Transfer::USER_LIST_NAME);
	bool free = userlist;
	bool partial = false;

	// Hide Share Mod
	bool isInSharingHub = true;

	if(aSource.getUser()) {
		isInSharingHub = ClientManager::getInstance()->isSharingHub(aSource.getHintedUser());
	}
	//Hide Share Mod

	string sourceFile;
	Transfer::Type type;

	try {
		if(aType == Transfer::names[Transfer::TYPE_FILE]) {
			sourceFile = ShareManager::getInstance()->toReal(aFile, isInSharingHub);

			if(aFile == Transfer::USER_LIST_NAME) {
				// Unpack before sending...
				string bz2 = File(sourceFile, File::READ, File::OPEN).read();
				string xml;
				CryptoManager::getInstance()->decodeBZ2(reinterpret_cast<const uint8_t*>(bz2.data()), bz2.size(), xml);
				// Clear to save some memory...
				string().swap(bz2);
				is = new MemoryInputStream(xml);
				start = 0;
				fileSize = size = xml.size();
			} else {
				File* f = new File(sourceFile, File::READ, File::OPEN);

				start = aStartPos;
				int64_t sz = f->getSize();
				size = (aBytes == -1) ? sz - start : aBytes;
				fileSize = sz;

				if((start + size) > sz) {
					aSource.fileNotAvail();
					delete f;
					return false;
				}

				free = free || (sz <= (int64_t)(SETTING(SET_MINISLOT_SIZE) * 1024) );


				if(!SETTING(FREE_SLOTS_EXTENSIONS).empty()){
				    int pos = sourceFile.rfind("\\")+1;
					if(Wildcard::patternMatch(Text::utf8ToAcp(sourceFile.substr(pos)), Text::utf8ToAcp(SETTING(FREE_SLOTS_EXTENSIONS)), '|')) {
						free = true;
					}
				}

				f->setPos(start);

				is = f;
				if((start + size) < sz) {
					is = new LimitedInputStream<true>(is, size);
				}
			}
			type = userlist ? Transfer::TYPE_FULL_LIST : Transfer::TYPE_FILE;			
		} else if(aType == Transfer::names[Transfer::TYPE_TREE]) {
			//sourceFile = ShareManager::getInstance()->toReal(aFile);
			sourceFile = aFile;
			MemoryInputStream* mis = ShareManager::getInstance()->getTree(aFile);
			if(!mis) {
				aSource.fileNotAvail();
				return false;
			}

			start = 0;
			fileSize = size = mis->getSize();
			is = mis;
			free = true;
			type = Transfer::TYPE_TREE;			
		} else if(aType == Transfer::names[Transfer::TYPE_PARTIAL_LIST]) {
			MemoryInputStream* mis;
			// Partial file list
			if (tthList) {
				mis = QueueManager::getInstance()->generateTTHList(aSource.getHintedUser(), aFile, isInSharingHub);
			} else {
				mis = ShareManager::getInstance()->generatePartialList(aFile, listRecursive, isInSharingHub);
			}

			if(mis == NULL) {
				aSource.fileNotAvail();
				return false;
			}
			
			start = 0;
			fileSize = size = mis->getSize();
			is = mis;
			free = true;
			type = Transfer::TYPE_PARTIAL_LIST;
		} else {
			aSource.fileNotAvail("Unknown file type");
			return false;
		}
	} catch(const ShareException& e) {
		// Partial file sharing upload
		if(aType == Transfer::names[Transfer::TYPE_FILE] && aFile.compare(0, 4, "TTH/") == 0) {

			TTHValue fileHash(aFile.substr(4));

            if (BOOLSETTING(USE_PARTIAL_SHARING) && QueueManager::getInstance()->isChunkDownloaded(fileHash, aStartPos, aBytes, sourceFile)) {				
				try {
					SharedFileStream* ss = new SharedFileStream(sourceFile, File::READ, File::OPEN | File::SHARED | File::NO_CACHE_HINT);
					
					start = aStartPos;
					fileSize = ss->getSize();
					size = (aBytes == -1) ? fileSize - start : aBytes;
					
					if((start + size) > fileSize) {
						aSource.fileNotAvail();
						delete ss;
						return false;
					}

					ss->setPos(start);
					is = ss;

					if((start + size) < fileSize) {
						is = new LimitedInputStream<true>(is, size);
					}

					partial = true;
					type = Transfer::TYPE_FILE;
					goto ok;
				} catch(const Exception&) {
					delete is;
				}
			}
		}
		aSource.fileNotAvail(e.getError());
		return false;
	} catch(const Exception& e) {
		LogManager::getInstance()->message(STRING(UNABLE_TO_SEND_FILE) + " " + sourceFile + ": " + e.getError());
		aSource.fileNotAvail();
		return false;
	}

ok:



	uint8_t slotType = aSource.getSlotType();
	
	bool noSlots = false;
	if (slotType != UserConnection::STDSLOT && slotType != UserConnection::MCNSLOT) {
		bool hasReserved = false;
		bool isFavorite = FavoriteManager::getInstance()->hasSlot(aSource.getUser());
		bool hasFreeSlot = false;
		{
		Lock l(cs);
		hasReserved = reservedSlots.find(aSource.getUser()) != reservedSlots.end();
		hasFreeSlot = (getFreeSlots() > 0) && ((uploadQueue.empty() && notifiedUsers.empty()) || isNotifiedUser(aSource.getUser()));
		
		if ((type==Transfer::TYPE_PARTIAL_LIST || fileSize <= 65792) && smallSlots <= 8) {
			slotType = UserConnection::SMALLSLOT;
		} else if (aSource.isSet(UserConnection::FLAG_MCN1)) {
			if (getMultiConn(aSource) || ((hasReserved || isFavorite|| getAutoSlot()) && !isUploading(aSource.getUser()->getCID()))) {
				slotType = UserConnection::MCNSLOT;
			} else {
				noSlots=true;
			}
		} else if (!(hasReserved || isFavorite || hasFreeSlot || getAutoSlot())) {
			noSlots=true;
		} else {
			slotType = UserConnection::STDSLOT;
		}
		}//lock free
		if (noSlots) {
			bool supportsFree = aSource.isSet(UserConnection::FLAG_SUPPORTS_MINISLOTS);
			bool allowedFree = (slotType == UserConnection::EXTRASLOT) || aSource.isSet(UserConnection::FLAG_OP) || getFreeExtraSlots() > 0;
			bool partialFree = partial && ((slotType == UserConnection::PARTIALSLOT) || (extraPartial < SETTING(EXTRA_PARTIAL_SLOTS)));

			if(free && supportsFree && allowedFree) {
				slotType = UserConnection::EXTRASLOT;
			} else if(partialFree) {
				slotType = UserConnection::PARTIALSLOT;
			} else {
				delete is;
				if (aSource.isSet(UserConnection::FLAG_MCN1) && isUploading(aSource.getUser()->getCID())) {
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

	// remove file from upload queue
	clearUserFiles(aSource.getUser());
	
	bool resumed = false;
	UploadBundlePtr bundle;

	// remove user from notified list
	{
		Lock l(cs);
	SlotIter cu = notifiedUsers.find(aSource.getUser());
	if(cu != notifiedUsers.end()) {
		notifiedUsers.erase(cu);
		}
	

	for(UploadList::iterator i = delayUploads.begin(); i != delayUploads.end(); ++i) {
		Upload* up = *i;
		if(&aSource == &up->getUserConnection()) {
			if (up->getBundle()) {
				bundle = up->getBundle();
			}
			if(sourceFile != up->getPath() && !bundle) {
				logUpload(up);
			} else {
				resumed = true;
			}
			removeDelayUpload(aSource.getToken(), false);
			break;
			}
		}
	} //lock free

	Upload* u = new Upload(aSource, sourceFile, TTHValue());
	//LogManager::getInstance()->message("Token2: " + aSource.getToken());
	u->setStream(is);
	u->setSegment(Segment(start, size));
	if (bundle)
		u->setBundle(bundle);
		
	if(u->getSize() != fileSize)
		u->setFlag(Upload::FLAG_CHUNKED);

	if(resumed)
		u->setFlag(Upload::FLAG_RESUMED);

	if(partial)
		u->setFlag(Upload::FLAG_PARTIAL);

	u->setFileSize(fileSize);
	u->setType(type);

	{
	Lock l(cs);
	uploads.push_back(u);
	}
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
				changeMultiConnSlot(aSource.getUser()->getCID(), true);
				break;
			case UserConnection::SMALLSLOT:
				smallSlots--;
				break;
		}
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
				changeMultiConnSlot(aSource.getUser()->getCID(), false);
				checkMultiConn();
				break;
			case UserConnection::SMALLSLOT:
				smallSlots++;
				break;
		}
		
		// user got a slot
		aSource.setSlotType(slotType);
	}

	return true;
}


void UploadManager::changeMultiConnSlot(const CID cid, bool remove) {
	Lock l(cs);
	if (!multiUploads.empty()) {
		MultiConnIter uis = multiUploads.find(cid);
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
			return;
		}
	}

	if (!remove) {
		//a new MCN upload
		multiUploads[cid] = 1;
		running++;
		mcnSlots++;
	}
}

bool UploadManager::getMultiConn(const UserConnection& aSource) {
	//inside a lock.
	CID cid = aSource.getUser()->getCID();

	bool hasFreeSlot=false;
	if ((int)(getSlots() - running - mcnSlots + multiUploads.size()) > 0) {
		if ((uploadQueue.empty() && notifiedUsers.empty()) || isNotifiedUser(aSource.getUser())) {
			hasFreeSlot=true;
		}
	}

	if (!multiUploads.empty()) {
		uint8_t highest=0;
		for(MultiConnIter i = multiUploads.begin(); i != multiUploads.end(); ++i) {
			if (i->first == cid) {
				continue;
			}
			if (i->second > highest) {
				highest = i->second;
			}
		}

		MultiConnIter uis = multiUploads.find(cid);
		if (uis != multiUploads.end()) {
			if (((highest > uis->second + 1) || hasFreeSlot) && (uis->second + 1 <= AirUtil::getSlotsPerUser(false) || AirUtil::getSlotsPerUser(false) == 0)) {
				return true;
			} else {
				return false;
			}
		}
	}

	//he's not uploading from us yet, check if we can allow new ones
	hasFreeSlot = (getFreeSlots() > 0) && ((uploadQueue.empty() && notifiedUsers.empty()) || isNotifiedUser(aSource.getUser()));
	if (hasFreeSlot) {
		return true;
	} else {
		return false;
	}
}

void UploadManager::checkMultiConn() {

	int extras = getSlots() - running - mcnSlots + multiUploads.size();
	if ((int)extras > 0 || getAutoSlot()) {
		return; //no reason to remove anything
	}

	Lock l(cs);
	int uploadsStart=0, highest=0;
	MultiConnMap compare=multiUploads;
	while (extras < 0) {
		if (!compare.empty()) {
			highest=0;
			for(MultiConnIter i = compare.begin(); i != compare.end(); ++i) {
				if (i->second > highest) {
					highest = i->second;
				}
			}
			if (highest <= 1) {
				return;
			}

			for(MultiConnIter i = compare.begin(); i != compare.end(); ++i) {
				if (i->second == highest) {
					//find the correct upload to kill
					for(UploadList::const_iterator s = uploads.begin()+uploadsStart; s != uploads.end(); ++s) {
						uploadsStart++;
						Upload* u = *s;
						if (u == NULL) continue;
						if (u->getUser()->getCID() == i->first) {
							u->getUserConnection().disconnect(true);
							break;
						}
					}
					break;
				}
			}
			extras++;
		} else {
			break;
		}
	}
}

void UploadManager::onUBN(const AdcCommand& cmd) {
	string bundleToken;
	string hubIpPort;
	float percent = 0;
	string speedStr;

	for(StringIterC i = cmd.getParameters().begin(); i != cmd.getParameters().end(); ++i) {
		const string& str = *i;
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

	if ((percent <= 0 && speedStr.empty()) || bundleToken.empty()) {
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

		if (percent > 0 && percent < 100) {
			bundle->setUploaded(bundle->getSize()*(percent / 100.00000));
		}
	}
}

void UploadManager::createBundle(const AdcCommand& cmd) {

	string bundleToken;
	string hubIpPort;
	string token;
	string name;
	int64_t size=0;
	int64_t downloaded=0;
	bool singleUser = false, multiUser = false;

	for(StringIterC i = cmd.getParameters().begin(); i != cmd.getParameters().end(); ++i) {
		const string& str = *i;
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
		} else if (str.compare(0, 2, "MU") == 0) {
			multiUser = true;
		} else {
			//LogManager::getInstance()->message("ONUBD CREATE UNKNOWN PARAM");
		}
	}
	
	if (bundleToken.empty() || name.empty() || size <= 0 || token.empty()) {
		LogManager::getInstance()->message("INVALID UBD1");
		return;
	}

	//if (bundleTokens.find(token) != bundleTokens.end())
	//dcassert(bundleTokens.find(token) == bundleTokens.end());
	//dcassert(!findBundle(bundleToken));
	if (findBundle(bundleToken)) {
		//LogManager::getInstance()->message("ADDBUNDLE, BUNDLE FOUND!");
		changeBundle(cmd);
		return;
	}

	UploadBundlePtr bundle = UploadBundlePtr(new UploadBundle(getBundleTarget(token, name), bundleToken));
	bundle->setSize(size);
	bundle->setStart(GET_TICK());
	if (downloaded > 0) {
		bundle->setUploaded(downloaded);
	}

	if (multiUser) {
		bundle->setSingleUser(false);
	} else if (singleUser) {
		bundle->setSingleUser(true);
	}
	bundle->increaseRunning();

	{
		Lock l (cs);
		//LogManager::getInstance()->message("ADDBUNDLE, BUNDLE ADDED!");
		bundleTokens.insert(make_pair(token, bundle));
		bundles.push_back(bundle);
	}

	setBundle(token, bundle);

}

void UploadManager::updateBundleInfo(const AdcCommand& cmd) {


	string bundleToken;
	string name;
	int64_t size=0;
	bool singleUser = false, multiUser = false;

	for(StringIterC i = cmd.getParameters().begin(); i != cmd.getParameters().end(); ++i) {
		const string& str = *i;
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
			//LogManager::getInstance()->message("UpdateBundleInfo: SINGLEUSER");
			bundle->setSingleUser(true);
			bundle->setTotalSpeed(0);
		} else if (size > 0) {
			bundle->setSize(size);
			if (!name.empty()) {
				bundle->setTarget(getBundleTarget(bundleToken, name));
			}
		}
		return;
	}
}

void UploadManager::changeBundle(const AdcCommand& cmd) {
	string bundleToken;
	string token;

	for(StringIterC i = cmd.getParameters().begin(); i != cmd.getParameters().end(); ++i) {
		const string& str = *i;
		if(str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		} else if(str.compare(0, 2, "TO") == 0) {
			token = str.substr(2);
		} else {
			//LogManager::getInstance()->message("ONUBD CHANGE UNKNOWN PARAM");
		}
	}
	
	if (bundleToken.empty() || token.empty()) {
		LogManager::getInstance()->message("INVALID UBD1: CHANGE");
		return;
	}

	UploadBundlePtr bundle = findBundle(bundleToken);
	dcassert(bundle);

	if (bundle) {
		findRemovedToken(token);
		dcassert(bundleTokens.find(token) == bundleTokens.end());
		{
		Lock l (cs);
		bundleTokens.insert(make_pair(token, bundle));
		bundle->increaseRunning();
		}
		//LogManager::getInstance()->message("CHANGE UPLOAD BUNDLE, INCREASE CONNECTIONS: " + Util::toString(bundle->getRunning()));
		setBundle(token, bundle);
	}
}


void UploadManager::finishBundle(const AdcCommand& cmd) {
	string bundleToken;

	for(StringIterC i = cmd.getParameters().begin(); i != cmd.getParameters().end(); ++i) {
		const string& str = *i;
		if(str.compare(0, 2, "BU") == 0) {
			bundleToken = str.substr(2);
		}
	}
	
	if (bundleToken.empty()) {
		LogManager::getInstance()->message("INVALID UBD1: FINISH");
		return;
	}

	UploadBundlePtr bundle = findBundle(bundleToken);
	//dcassert(!bundle);

	if (bundle) {
		//ignore bundle updates that may arrive after this
		bundle->setSingleUser(true);
		fire(UploadManagerListener::BundleComplete(), bundle->getToken(), bundle->getName());
	}
}

void UploadManager::removeBundleConnection(const AdcCommand& cmd) {
	string token;

	for(StringIterC i = cmd.getParameters().begin(); i != cmd.getParameters().end(); ++i) {
		const string& str = *i;
		if(str.compare(0, 2, "TO") == 0) {
			token = str.substr(2);
		}
	}

	if (!token.empty()) {
		findRemovedToken(token);
	}
}

void UploadManager::setBundle(const string aToken, UploadBundlePtr aBundle) {
	Lock l(cs);
	for(UploadList::const_iterator i = uploads.begin(); i != uploads.end(); ++i) {
		if ((*i)->getUserConnection().getToken() == aToken) {
			Upload* u = *i;
			u->setBundle(aBundle);
			return;
		}
	}
	//LogManager::getInstance()->message("UPLOAD BUNDLE, SETBUNDLE FAILED: " + aToken);
}

string UploadManager::getBundleTarget(const string aToken, const string aName) {
	string path;
	{
		Lock l(cs);
	for(UploadList::const_iterator i = uploads.begin(); i != uploads.end(); ++i) {
		if ((*i)->getUserConnection().getToken() == aToken) {
			//LogManager::getInstance()->message("UPLOAD BUNDLE, NAME TOKEN UPLOAD");
			path = (*i)->getPath();
			break;
		}
		if (!(*i)->getBundle()) continue;
		if ((*i)->getBundle()->getToken() == aToken) {
			//LogManager::getInstance()->message("UPLOAD BUNDLE, NAME TOKEN BUNDLE");
			path = (*i)->getPath();
			break;
			}
		}
	} //lock free
	if (!path.empty()) {
		//LogManager::getInstance()->message("UPLOAD BUNDLE, ANAME: " + aName);
		size_t pos = path.find(aName);
		string target;
		if (pos != string::npos && path.length() > pos+aName.length()) {
			target = (path.substr(0, pos+aName.length()+1));
			//LogManager::getInstance()->message("UPLOAD BUNDLE, GETTARGET: " + target);
			return target;
		}
	} else {
		//LogManager::getInstance()->message("GETTARGET PATH EMPTY");
	}
	return aName;
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

UploadBundlePtr UploadManager::findBundle(const string bundleToken) {
	Lock l(cs);
	for (UploadBundleList::iterator i = bundles.begin(); i != bundles.end(); ++i) {
		if ((*i)->getToken() == bundleToken) {
			//LogManager::getInstance()->message("FOUND UPLOAD BUNDLE TARGET");
			return *i;
		}
	}
	return NULL;
}

bool UploadManager::findRemovedToken(const string aToken) {
	Lock l (cs);
	auto i = bundleTokens.find(aToken);
	if (i != bundleTokens.end()) {
		UploadBundlePtr b = i->second;
		if (b) {
			bundleTokens.erase(i);
			b->decreaseRunning();
			setBundle(aToken, NULL);
			if (b->getRunning() == 0) {
				//LogManager::getInstance()->message("ERASE UPLOAD BUNDLE");
				bundles.erase(std::remove(bundles.begin(), bundles.end(), b), bundles.end());
				//b->dec();
			} else {
				//LogManager::getInstance()->message("DON'T ERASE UPLOAD BUNDLE, RUNNING: " + Util::toString(b->getRunning()));
			}
		}
		return true;
	}
	return false;
}

int64_t UploadManager::getRunningAverage() {
	Lock l(cs);
	int64_t avg = 0;
	for(UploadList::const_iterator i = uploads.begin(); i != uploads.end(); ++i) {
		Upload* u = *i;
		avg += static_cast<int64_t>(u->getAverageSpeed());
	}
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
	return getRunningAverage() < (AirUtil::getSpeedLimit(false)*1024);
}

void UploadManager::removeUpload(Upload* aUpload, bool delay) {
	{
	Lock l(cs);
	dcassert(find(uploads.begin(), uploads.end(), aUpload) != uploads.end());
	uploads.erase(remove(uploads.begin(), uploads.end(), aUpload), uploads.end());
	}
	if(delay) {
		Lock l(cs);
		delayUploads.push_back(aUpload);
	} else {
		findRemovedToken(aUpload->getUserConnection().getToken());
		delete aUpload;
	}
}

void UploadManager::reserveSlot(const HintedUser& aUser, uint64_t aTime) {
	{
		Lock l(cs);
		reservedSlots[aUser] = GET_TICK() + aTime*1000;
	}
	if(aUser.user->isOnline())
	{
		// find user in uploadqueue to connect with correct token
		auto it = find_if(uploadQueue.cbegin(), uploadQueue.cend(), [&](const UserPtr& u) { return u == aUser.user; });
		if(it != uploadQueue.cend()) {
			ClientManager::getInstance()->connect(aUser, it->token);
		}
	}
}

void UploadManager::unreserveSlot(const UserPtr& aUser) {
	Lock l(cs);
	SlotIter uis = reservedSlots.find(aUser);
	if(uis != reservedSlots.end())
		reservedSlots.erase(uis);
}

void UploadManager::on(UserConnectionListener::Get, UserConnection* aSource, const string& aFile, int64_t aResume) noexcept {
	if(aSource->getState() != UserConnection::STATE_GET) {
		dcdebug("UM::onGet Bad state, ignoring\n");
		return;
	}
	
	int64_t bytes = -1;
	if(prepareFile(*aSource, Transfer::names[Transfer::TYPE_FILE], Util::toAdcFile(aFile), aResume, bytes)) {
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
	dcassert(u != NULL);

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
	//LogManager::getInstance()->message("Token1: " + aSource->getToken());
	if(prepareFile(*aSource, type, fname, aStartPos, aBytes, c.hasFlag("RE", 4), c.hasFlag("TL", 4))) {
		Upload* u = aSource->getUpload();
		dcassert(u != NULL);
		//dcassert(!u->getToken().empty());
		//LogManager::getInstance()->message(u->);

		AdcCommand cmd(AdcCommand::CMD_SND);
		cmd.addParam(type).addParam(fname)
			.addParam(Util::toString(u->getStartPos()))
			.addParam(Util::toString(u->getSize()));

		if(c.hasFlag("ZL", 4)) {
			u->setStream(new FilteredInputStream<ZFilter, true>(u->getStream()));
			u->setFlag(Upload::FLAG_ZUPLOAD);
			cmd.addParam("ZL1");
		}
		if(c.hasFlag("TL", 4) && type == Transfer::names[Transfer::TYPE_PARTIAL_LIST]) {
			cmd.addParam("TL1");	 
		}

		aSource->send(cmd);
		

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

	UploadBundlePtr bundle = u->getBundle();
	if (bundle) {
		if (bundle->getSingleUser()) {
			bundle->increaseUploaded(aBytes);
		}
	}
	dcassert(u != NULL);
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
	dcassert(u != NULL);

	aSource->setState(UserConnection::STATE_GET);

	if(!u->isSet(Upload::FLAG_CHUNKED) && !u->getBundle()) {
		logUpload(u);
		removeUpload(u);
	} else {
		if (u->getBundle()) {
			logUpload(u);
		}
		removeUpload(u, true);
	}
}

void UploadManager::logUpload(const Upload* u) {
	if(BOOLSETTING(LOG_UPLOADS) && u->getType() != Transfer::TYPE_TREE && (BOOLSETTING(LOG_FILELIST_TRANSFERS) || u->getType() != Transfer::TYPE_FULL_LIST)) {
		StringMap params;
		u->getParams(u->getUserConnection(), params);
		LOG(LogManager::UPLOAD, params);
	}

	fire(UploadManagerListener::Complete(), u);
}

size_t UploadManager::addFailedUpload(const UserConnection& source, const string& file, int64_t pos, int64_t size) {
	size_t queue_position = 0;

	auto it = find_if(uploadQueue.begin(), uploadQueue.end(), [&](const UserPtr& u) -> bool { ++queue_position; return u == source.getUser(); });
	if(it != uploadQueue.end()) {
		it->token = source.getToken();
		for(auto fileIter = it->files.cbegin(); fileIter != it->files.cend(); ++fileIter) {
			if((*fileIter)->getFile() == file) {
				(*fileIter)->setPos(pos);
				return queue_position;
			}
		}
	}

	UploadQueueItem* uqi = new UploadQueueItem(source.getHintedUser(), file, pos, size);
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

void UploadManager::clearUserFiles(const UserPtr& aUser) {
	
	Lock l (cs);
	auto it = find_if(uploadQueue.cbegin(), uploadQueue.cend(), [&](const UserPtr& u) { return u == aUser; });
	if(it != uploadQueue.cend()) {
		for(auto i = it->files.cbegin(); i != it->files.cend(); ++i) {
			fire(UploadManagerListener::QueueItemRemove(), (*i));
			(*i)->dec();
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
	dcassert(aSource->getUpload() == NULL);
	aSource->removeListener(this);

	// slot lost
	switch(aSource->getSlotType()) {
		case UserConnection::STDSLOT: running--; break;
		case UserConnection::EXTRASLOT: extra--; break;
		case UserConnection::PARTIALSLOT: extraPartial--; break;
		case UserConnection::SMALLSLOT: smallSlots--; break;
		case UserConnection::MCNSLOT:
			changeMultiConnSlot(aSource->getUser()->getCID(), true); 
			break;
	}
	aSource->setSlotType(UserConnection::NOSLOT);
}

void UploadManager::notifyQueuedUsers() {
	if (uploadQueue.empty()) return;		//no users to notify

	int freeslots = getFreeSlots();
	if(freeslots > 0)
	{
		freeslots -= notifiedUsers.size();
		while(!uploadQueue.empty() && freeslots > 0) {
			// let's keep him in the notifiedList until he asks for a file
			WaitingUser wu = uploadQueue.front();
			clearUserFiles(wu.user);
			
			notifiedUsers[wu.user] = GET_TICK();

			ClientManager::getInstance()->connect(wu.user, wu.token);

			freeslots--;
		}
	}
}

void UploadManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	UserList disconnects;
	{
		Lock l(cs);
		for(SlotIter j = reservedSlots.begin(); j != reservedSlots.end();) {
			if(j->second < aTick) {
				reservedSlots.erase(j++);
			} else {
				++j;
			}
		}
	
		for(SlotIter i = notifiedUsers.begin(); i != notifiedUsers.end();) {
			if((i->second + (90 * 1000)) < aTick) {
				clearUserFiles(i->first);
				notifiedUsers.erase(i++);
			} else
				++i;
		}

		if( BOOLSETTING(AUTO_KICK) ) {
			for(UploadList::const_iterator i = uploads.begin(); i != uploads.end(); ++i) {
				Upload* u = *i;
				if(u->getUser()->isOnline()) {
					u->unsetFlag(Upload::FLAG_PENDING_KICK);
					continue;
				}

				if(u->isSet(Upload::FLAG_PENDING_KICK)) {
					disconnects.push_back(u->getUser());
					continue;
				}

				if(BOOLSETTING(AUTO_KICK_NO_FAVS) && FavoriteManager::getInstance()->isFavoriteUser(u->getUser())) {
					continue;
				}

				u->setFlag(Upload::FLAG_PENDING_KICK);
			}
		}
	}
		
	for(UserList::const_iterator i = disconnects.begin(); i != disconnects.end(); ++i) {
		LogManager::getInstance()->message(STRING(DISCONNECTED_USER) + " " + Util::toString(ClientManager::getInstance()->getNicks((*i)->getCID(), Util::emptyString)));
		ConnectionManager::getInstance()->disconnect(*i, false);
	}

	int freeSlots = getFreeSlots();
	if(freeSlots != lastFreeSlots) {
		lastFreeSlots = freeSlots;
		/* I don't like spamming every minute so disabled it for now
			ClientManager::getInstance()->infoUpdated();
		*/
	}
}

void UploadManager::on(GetListLength, UserConnection* conn) noexcept { 
	conn->error("GetListLength not supported");
	conn->disconnect(false);
}

void UploadManager::on(AdcCommand::GFI, UserConnection* aSource, const AdcCommand& c) noexcept {
	if(aSource->getState() != UserConnection::STATE_GET) {
		dcdebug("UM::onSend Bad state, ignoring\n");
		return;
	}
	
	if(c.getParameters().size() < 2) {
		aSource->send(AdcCommand(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_PROTOCOL_GENERIC, "Missing parameters"));
		return;
	}

	const string& type = c.getParam(0);
	const string& ident = c.getParam(1);

	if(type == Transfer::names[Transfer::TYPE_FILE]) {
		try {
			aSource->send(ShareManager::getInstance()->getFileInfo(ident));
		} catch(const ShareException&) {
			aSource->fileNotAvail();
		}
	} else {
		aSource->fileNotAvail();
	}
}

// TimerManagerListener
void UploadManager::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	typedef vector<pair<UploadBundlePtr, double>> BundleSpeedMap;
	BundleSpeedMap bundleSpeeds;
	UploadList ticks;
	{
		Lock l(cs);
		for(UploadList::iterator i = delayUploads.begin(); i != delayUploads.end(); ++i) {
			Upload* u = *i;
			if(++u->delayTime > 10) {
				removeDelayUpload(u->getUserConnection().getToken(), true);
				break;
			}
		}

		for(UploadList::const_iterator i = uploads.begin(); i != uploads.end(); ++i) {
			if((*i)->getPos() > 0) {
				ticks.push_back(*i);
				(*i)->tick();
			}
		
			UploadBundlePtr bundle = (*i)->getBundle();
			if (bundle) {
				double speed = (*i)->getAverageSpeed();
				bool found=false;
				for (BundleSpeedMap::iterator j = bundleSpeeds.begin(); j != bundleSpeeds.end(); ++j) {
					if ((*j).first->getToken() == bundle->getToken()) {
						j->second += speed;
						bundle->setRunning(bundle->getRunning() + 1);
						found=true;
						break;
					}
				}
				if (!found) {
					bundle->setRunning(1);
					bundleSpeeds.push_back(make_pair(bundle, speed));
				}
			}
		}
	}
		for (BundleSpeedMap::iterator i = bundleSpeeds.begin(); i != bundleSpeeds.end(); ++i) {
			(*i).first->setSpeed((*i).second);
			//LogManager::getInstance()->message("Bundle status updated, speed: " + Util::formatBytes((*i).first->getSpeed()));
			//LogManager::getInstance()->message("Bundle status updated, downloaded: " + Util::formatBytes((*i).first->getDownloaded()));
		}

		if(ticks.size() > 0)
			fire(UploadManagerListener::Tick(), ticks, bundles);

		notifyQueuedUsers();
		fire(UploadManagerListener::QueueUpdate());
	
}

void UploadManager::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser) noexcept {
	if(!aUser->isOnline()) {
		clearUserFiles(aUser);
	}
}

void UploadManager::removeDelayUpload(const string& aToken, bool removeBundle) {
	if (removeBundle) {
		findRemovedToken(aToken);
	}
	Lock l(cs);
	for(UploadList::iterator i = delayUploads.begin(); i != delayUploads.end(); ++i) {
		Upload* up = *i;
		if(aToken == up->getUserConnection().getToken()) {
			delayUploads.erase(i);
			delete up;
			break;
		}
	}
}

/**
 * Abort upload of specific file
 */
void UploadManager::abortUpload(const string& aFile, bool waiting){
	bool nowait = true;

	{
		Lock l(cs);

		for(UploadList::const_iterator i = uploads.begin(); i != uploads.end(); i++){
			Upload* u = (*i);

			if(u->getPath() == aFile){
				u->getUserConnection().disconnect(true);
				nowait = false;
			}
		}
	}
	
	if(nowait) return;
	if(!waiting) return;
	
	for(int i = 0; i < 20 && nowait == false; i++){
		Thread::sleep(250);
		{
			Lock l(cs);

			nowait = true;
			for(UploadList::const_iterator i = uploads.begin(); i != uploads.end(); i++){
				Upload* u = (*i);

				if(u->getPath() == aFile){
					dcdebug("upload %s is not removed\n", aFile.c_str());
					nowait = false;
					break;
				}
			}
		}
	}
	
	if(!nowait)
		dcdebug("abort upload timeout %s\n", aFile.c_str());
}

} // namespace dcpp

/**
 * @file
 * $Id: UploadManager.cpp 568 2011-07-24 18:28:43Z bigmuscle $
 */
