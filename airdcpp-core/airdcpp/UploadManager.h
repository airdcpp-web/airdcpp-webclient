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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_MANAGER_H
#define DCPLUSPLUS_DCPP_UPLOAD_MANAGER_H

#include "forward.h"

#include "ClientManagerListener.h"
#include "CriticalSection.h"
#include "FastAlloc.h"
#include "HintedUser.h"
#include "MerkleTree.h"
#include "Message.h"
#include "Singleton.h"
#include "Speaker.h"
#include "StringMatch.h"
#include "TimerManagerListener.h"
#include "UploadManagerListener.h"
#include "UserConnectionListener.h"
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
	const UserPtr& getUser() const noexcept { return user.user; }
	const HintedUser& getHintedUser() const noexcept { return user; }
	const string& getHubUrl() const noexcept { return user.hint; }

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

class UploadManager : private ClientManagerListener, private UserConnectionListener, public Speaker<UploadManagerListener>, private TimerManagerListener, public Singleton<UploadManager>
{
public:
	void setFreeSlotMatcher();

	/** @return Number of uploads. */ 
	size_t getUploadCount() const noexcept;

	size_t getRunningBundleCount() const noexcept;

	/**
	 * @remarks This is only used in the tray icons. Could be used in
	 * MainFrame too.
	 *
	 * @return Running average download speed in Bytes/s
	 */
	int64_t getRunningAverage(bool aLock = true) const noexcept;
	
	uint8_t getSlots() const noexcept;

	/** @return Number of free slots. */
	uint8_t getFreeSlots() const noexcept;
	
	/** @internal */
	int getFreeExtraSlots() const noexcept;
	
	/** @param aUser Reserve an upload slot for this user and connect. */
	void reserveSlot(const HintedUser& aUser, uint64_t aTime) noexcept;
	void unreserveSlot(const UserPtr& aUser) noexcept;
	void clearUserFiles(const UserPtr& aUser, bool lock) noexcept;
	bool hasReservedSlot(const UserPtr& aUser) const noexcept;
	bool isNotifiedUser(const UserPtr& aUser) const noexcept;
	typedef vector<WaitingUser> SlotQueue;
	SlotQueue getUploadQueue() const noexcept;

	void onUBD(const AdcCommand& cmd);
	void onUBN(const AdcCommand& cmd);
	UploadBundlePtr findBundle(const string& aBundleToken) const noexcept;

	/** @internal */
	void addConnection(UserConnectionPtr conn) noexcept;
	void abortUpload(const string& aFile, bool aWaitDisconnected = true) noexcept;
		
	IGETSET(uint8_t, extraPartial, ExtraPartial, 0);
	IGETSET(uint8_t, extra, Extra, 0);
	IGETSET(uint64_t, lastGrant, LastGrant, 0);

	SharedMutex& getCS() noexcept { return cs; }
	const UploadList& getUploads() const noexcept {
		return uploads;
	}
private:
	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;
	StringMatch freeSlotMatcher;

	uint8_t running = 0;
	uint8_t mcnSlots = 0;
	uint8_t smallSlots = 0;

	UploadList uploads;
	UploadList delayUploads;
	mutable SharedMutex cs;

	int lastFreeSlots = -1; /// amount of free slots at the previous minute
	
	typedef unordered_map<UserPtr, uint16_t, User::Hash> MultiConnMap;
	MultiConnMap multiUploads;

	typedef unordered_map<UserPtr, uint64_t, User::Hash> SlotMap;
	typedef SlotMap::iterator SlotIter;
	SlotMap reservedSlots;
	SlotMap notifiedUsers;
	SlotQueue uploadQueue;

	size_t addFailedUpload(const UserConnection& source, const string& file, int64_t pos, int64_t size) noexcept;
	void notifyQueuedUsers() noexcept;
	void connectUser(const HintedUser& aUser, const string& aToken) noexcept;

	bool isUploadingLocked(const UserPtr& aUser) const noexcept { return multiUploads.find(aUser) != multiUploads.end(); }
	bool getMultiConnLocked(const UserConnection& aSource) noexcept;
	void changeMultiConnSlot(const UserPtr& aUser, bool aRemove) noexcept;
	void checkMultiConn() noexcept;
	void updateSlotCounts(UserConnection& aSource, uint8_t aSlotType) noexcept;

	/* bundles */
	typedef unordered_map<string, UploadBundlePtr> RemoteBundleTokenMap;
	RemoteBundleTokenMap bundles;

	void createBundle(const AdcCommand& cmd);
	void changeBundle(const AdcCommand& cmd);
	void updateBundleInfo(const AdcCommand& cmd);
	void finishBundle(const AdcCommand& cmd);
	void removeBundleConnection(const AdcCommand& cmd);

	Upload* findUpload(const string& aToken) noexcept;

	friend class Singleton<UploadManager>;
	UploadManager() noexcept;
	~UploadManager();

	bool getAutoSlot() const noexcept;
	void removeConnection(UserConnection* aConn) noexcept;
	void removeUpload(Upload* aUpload, bool aDelay = false) noexcept;
	void logUpload(const Upload* u) noexcept;

	// ClientManagerListener
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool aWentOffline) noexcept override;
	
	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;

	// UserConnectionListener
	void on(BytesSent, UserConnection*, size_t, size_t) noexcept override;
	void on(Failed, UserConnection*, const string&) noexcept override;
	void on(Get, UserConnection*, const string&, int64_t) noexcept override;
	void on(Send, UserConnection*) noexcept override;
	void on(GetListLength, UserConnection* conn) noexcept override;
	void on(TransmitDone, UserConnection*) noexcept override;
	
	void on(AdcCommand::GET, UserConnection*, const AdcCommand&) noexcept override;
	void on(AdcCommand::GFI, UserConnection*, const AdcCommand&) noexcept override;

	bool prepareFile(UserConnection& aSource, const string& aType, const string& aFile, int64_t aResume, int64_t& aBytes, const string& aUserSID, bool aListRecursive = false, bool aIsTTHList = false);
};

} // namespace dcpp

#endif // !defined(UPLOAD_MANAGER_H)
