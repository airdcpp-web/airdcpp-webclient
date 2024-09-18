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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_QUEUE_MANAGER_H
#define DCPLUSPLUS_DCPP_UPLOAD_QUEUE_MANAGER_H

#include "forward.h"

#include "ClientManagerListener.h"
#include "CriticalSection.h"
#include "FastAlloc.h"
#include "HintedUser.h"
#include "Message.h"
#include "Speaker.h"
#include "TimerManagerListener.h"
#include "UploadQueueManagerListener.h"
#include "UserInfoBase.h"

namespace dcpp {

class UploadQueueItem : public FastAlloc<UploadQueueItem>, public intrusive_ptr_base<UploadQueueItem>, public UserInfoBase {
public:
	UploadQueueItem(const HintedUser& _user, const string& _file, int64_t _pos, int64_t _size);

	static int compareItems(const UploadQueueItem* a, const UploadQueueItem* b, uint8_t col);

	enum {
		COLUMN_FIRST,
		COLUMN_FILE = COLUMN_FIRST,
		COLUMN_PATH,
		COLUMN_NICK,
		COLUMN_HUB,
		COLUMN_TRANSFERRED,
		COLUMN_SIZE,
		COLUMN_ADDED,
		COLUMN_WAITING,
		COLUMN_LAST
	};
		
	const tstring getText(uint8_t col) const noexcept;
	int getImageIndex() const noexcept;

	int64_t getSize() const noexcept { return size; }
	uint64_t getTime() const noexcept { return time; }
	const string& getFile() const noexcept { return file; }
	const UserPtr& getUser() const noexcept override { return user.user; }
	const HintedUser& getHintedUser() const noexcept { return user; }
	const string& getHubUrl() const noexcept override { return user.hint; }

	GETSET(int64_t, pos, Pos);
	
private:
	HintedUser	user;
	string		file;
	int64_t		size;
	uint64_t	time;
};

struct WaitingUser {

	WaitingUser(const HintedUser& _user, const std::string& _token) : user(_user), token(_token) { }
	operator const UserPtr&() const { return user.user; }

	set<UploadQueueItem*>	files;
	HintedUser				user;
	string					token;
};

class UploadQueueManager : private ClientManagerListener, public Speaker<UploadQueueManagerListener>, private TimerManagerListener
{
public:
	void clearUserFiles(const UserPtr& aUser) noexcept {
		WLock l(cs);
		clearUserFilesUnsafe(aUser);
	}
	void removeQueue(const UserPtr& aUser) noexcept;
	bool isNotifiedUserUnsafe(const UserPtr& aUser) const noexcept;
	using SlotQueue = vector<WaitingUser>;
	SlotQueue getUploadQueue() const noexcept;
		
	IGETSET(uint8_t, extraPartial, ExtraPartial, 0);
	IGETSET(uint8_t, extra, Extra, 0);
	IGETSET(uint64_t, lastGrant, LastGrant, 0);

	bool allowUser(const UserPtr& aUser) const noexcept;
	void connectUser(const HintedUser& aUser) noexcept;

	using FreeSlotF = std::function<uint8_t ()>;
	explicit UploadQueueManager(FreeSlotF&& aFreeSlotF) noexcept;
	~UploadQueueManager() final;
private:
	friend class UploadManager;

	void clearUserFilesUnsafe(const UserPtr& aUser) noexcept;

	mutable SharedMutex cs;

	using SlotMap = unordered_map<UserPtr, uint64_t, User::Hash>;
	SlotMap notifiedUsers;
	SlotQueue uploadQueue;

	size_t addFailedUpload(const UserConnection& source, const string& file, int64_t pos, int64_t size) noexcept;
	void notifyQueuedUsers(uint8_t aFreeSlots) noexcept;
	static void connectUser(const HintedUser& aUser, const string& aToken) noexcept;

	// ClientManagerListener
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool aWentOffline) noexcept override;
	
	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;

	FreeSlotF freeSlotF;
};

} // namespace dcpp

#endif // !defined(UPLOAD_MANAGER_H)
