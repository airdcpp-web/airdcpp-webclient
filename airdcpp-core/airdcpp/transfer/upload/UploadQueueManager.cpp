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

#include "UploadQueueManager.h"

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/connection/UserConnection.h>


namespace dcpp {

IncrementingIdCounter<UploadQueueItemToken> UploadQueueItem::idCounter;

UploadQueueItem::UploadQueueItem(const HintedUser& _user, const string& _file, int64_t _pos, int64_t _size) :
	pos(_pos), token(idCounter.next()), user(_user), file(_file), size(_size), time(GET_TIME()) {
}


using ranges::find_if;

UploadQueueManager::UploadQueueManager(FreeSlotF&& aFreeSlotF) noexcept : freeSlotF(std::move(aFreeSlotF)) {
	ClientManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
}

UploadQueueManager::~UploadQueueManager() {
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);
	{
		WLock l(cs);
		uploadQueue.clear();
	}
}

UserConnectResult UploadQueueManager::connectUser(const HintedUser& aUser, const string& aToken) noexcept {
	return ClientManager::getInstance()->connect(aUser, aToken, true);
}

optional<UserConnectResult> UploadQueueManager::connectUser(const HintedUser& aUser) noexcept {
	if (!aUser.user->isOnline()) {
		return nullopt;
	}

	bool connect = false;
	string token;

	optional<UserConnectResult> result;

	// find user in upload queue to connect with correct token
	{
		RLock l(cs);
		auto it = ranges::find_if(uploadQueue, [&](const UserPtr& u) { return u == aUser.user; });
		if (it != uploadQueue.cend()) {
			token = it->token;
			connect = true;
		}
	}

	if (connect) {
		result = connectUser(aUser, token);
	}

	return result;
}

size_t UploadQueueManager::addFailedUpload(const UserConnection& aSource, const string& aFile, int64_t aPos, int64_t aSize) noexcept {
	size_t queue_position = 0;
	WLock l(cs);
	auto it = ranges::find_if(uploadQueue, [&](const UserPtr& u) { ++queue_position; return u == aSource.getUser(); });
	if (it != uploadQueue.end()) {
		it->token = aSource.getConnectToken();
		for (const auto f: it->files) {
			if(f->getFile() == aFile) {
				f->setPos(aPos);
				return queue_position;
			}
		}
	}

	auto uqi = std::make_shared<UploadQueueItem>(aSource.getHintedUser(), aFile, aPos, aSize);
	if (it == uploadQueue.end()) {
		++queue_position;

		WaitingUser wu(aSource.getHintedUser(), aSource.getConnectToken());
		wu.files.insert(uqi);
		uploadQueue.push_back(wu);
	} else {
		it->files.insert(uqi);
	}

	fire(UploadQueueManagerListener::QueueAdd(), uqi);
	return queue_position;
}

void UploadQueueManager::clearUserFilesUnsafe(const UserPtr& aUser) noexcept {
	auto it = ranges::find_if(uploadQueue, [&](const UserPtr& u) { return u == aUser; });
	if (it != uploadQueue.end()) {
		for (const auto& f: it->files) {
			fire(UploadQueueManagerListener::QueueItemRemove(), f);
		}
		uploadQueue.erase(it);
		fire(UploadQueueManagerListener::QueueUserRemove(), aUser);
	}
}

void UploadQueueManager::removeQueue(const UserPtr& aUser) noexcept {
	WLock l(cs);
	// remove file from upload queue
	clearUserFilesUnsafe(aUser);

	// remove user from notified list
	auto cu = notifiedUsers.find(aUser);
	if (cu != notifiedUsers.end()) {
		notifiedUsers.erase(cu);
	}
}

bool UploadQueueManager::allowUser(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	return (uploadQueue.empty() && notifiedUsers.empty()) || isNotifiedUserUnsafe(aUser);
}

void UploadQueueManager::notifyQueuedUsers(uint8_t aFreeSlots) noexcept {
	auto freeslots = aFreeSlots;
	vector<WaitingUser> notifyList;

	{
		WLock l(cs);
		if (uploadQueue.empty()) return;		//no users to notify

		if (freeslots > 0) {
			freeslots -= notifiedUsers.size();
			while(!uploadQueue.empty() && freeslots > 0) {
				// let's keep him in the connectingList until he asks for a file
				auto wu = uploadQueue.front();
				clearUserFilesUnsafe(wu.user);
				if(wu.user.user->isOnline()) {
					notifiedUsers[wu.user] = GET_TICK();
					notifyList.push_back(wu);
					freeslots--;
				}
			}
		}
	}

	for(const auto& wu: notifyList)
		connectUser(wu.user, wu.token);
}

void UploadQueueManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	WLock l(cs);
	for (auto i = notifiedUsers.begin(); i != notifiedUsers.end();) {
		if ((i->second + (90 * 1000)) < aTick) {
			clearUserFilesUnsafe(i->first);
			i = notifiedUsers.erase(i);
		} else
			++i;
	}
}

bool UploadQueueManager::isNotifiedUserUnsafe(const UserPtr& aUser) const noexcept {
	return notifiedUsers.contains(aUser); 
}

UploadQueueManager::SlotQueue UploadQueueManager::getUploadQueue() const noexcept {
	RLock l(cs); 
	return uploadQueue; 
}

void UploadQueueManager::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	notifyQueuedUsers(freeSlotF());
	fire(UploadQueueManagerListener::QueueUpdate());
}

void UploadQueueManager::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool aWentOffline) noexcept {
	if (aWentOffline) {
		clearUserFiles(aUser);
	}
}

} // namespace dcpp