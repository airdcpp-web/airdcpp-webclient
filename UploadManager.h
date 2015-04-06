/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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
#include "Pointer.h"
#include "Singleton.h"
#include "StringMatch.h"
#include "TimerManager.h"
#include "UploadManagerListener.h"
#include "UserConnectionListener.h"
#include "UserInfoBase.h"

namespace dcpp {

class UploadQueueItem : public FastAlloc<UploadQueueItem>, public intrusive_ptr_base<UploadQueueItem>, public UserInfoBase {
public:
	UploadQueueItem(const HintedUser& _user, const string& _file, int64_t _pos, int64_t _size) :
		user(_user), file(_file), pos(_pos), size(_size), time(GET_TIME()) { inc(); }

	static int compareItems(const UploadQueueItem* a, const UploadQueueItem* b, uint8_t col) {
		switch(col) {
			case COLUMN_TRANSFERRED: return compare(a->pos, b->pos);
			case COLUMN_SIZE: return compare(a->size, b->size);
			case COLUMN_ADDED:
			case COLUMN_WAITING: return compare(a->time, b->time);
			default: return Util::stricmp(a->getText(col).c_str(), b->getText(col).c_str());
		}
	}

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
		
	const tstring getText(uint8_t col) const;
	int getImageIndex() const;

	int64_t getSize() const { return size; }
	uint64_t getTime() const { return time; }
	const string& getFile() const { return file; }
	const UserPtr& getUser() const { return user.user; }
	const HintedUser& getHintedUser() const { return user; }
	const string& getHubUrl() const { return user.hint; }

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
	size_t getUploadCount() const;

	/**
	 * @remarks This is only used in the tray icons. Could be used in
	 * MainFrame too.
	 *
	 * @return Running average download speed in Bytes/s
	 */
	int64_t getRunningAverage(bool lock=true);
	
	uint8_t getSlots() const;

	/** @return Number of free slots. */
	uint8_t getFreeSlots() const;
	
	/** @internal */
	int getFreeExtraSlots() const;
	
	/** @param aUser Reserve an upload slot for this user and connect. */
	void reserveSlot(const HintedUser& aUser, uint64_t aTime);
	void unreserveSlot(const UserPtr& aUser);
	void clearUserFiles(const UserPtr& aUser, bool lock);
	bool hasReservedSlot(const UserPtr& aUser) const;
	bool isNotifiedUser(const UserPtr& aUser) const;
	typedef vector<WaitingUser> SlotQueue;
	SlotQueue getUploadQueue() const;

	void unreserveSlot(const UserPtr& aUser, bool add);
	void onUBD(const AdcCommand& cmd);
	void onUBN(const AdcCommand& cmd);
	UploadBundlePtr findBundle(const string& aBundleToken);

	/** @internal */
	void addConnection(UserConnectionPtr conn);
	void removeDelayUpload(const UserConnection& source);
	void abortUpload(const string& aFile, bool waiting = true);
		
	GETSET(uint8_t, extraPartial, ExtraPartial);
	GETSET(uint8_t, extra, Extra);
	GETSET(uint64_t, lastGrant, LastGrant);

	SharedMutex& getCS() { return cs; }
	const UploadList& getUploads() const {
		return uploads;
	}
private:
	StringMatch freeSlotMatcher;

	uint8_t running;
	uint8_t mcnSlots;
	uint8_t smallSlots;

	UploadList uploads;
	UploadList delayUploads;
	mutable SharedMutex cs;

	int lastFreeSlots; /// amount of free slots at the previous minute
	
	typedef unordered_map<UserPtr, uint16_t, User::Hash> MultiConnMap;
	MultiConnMap multiUploads;

	typedef unordered_map<UserPtr, uint64_t, User::Hash> SlotMap;
	typedef SlotMap::iterator SlotIter;
	SlotMap reservedSlots;
	SlotMap notifiedUsers;
	SlotQueue uploadQueue;

	size_t addFailedUpload(const UserConnection& source, const string& file, int64_t pos, int64_t size);
	void notifyQueuedUsers();
	void connectUser(const HintedUser& aUser, const string& aToken);

	bool isUploading(const UserPtr& aUser) const { return multiUploads.find(aUser) != multiUploads.end(); }
	bool getMultiConn(const UserConnection& aSource);
	void changeMultiConnSlot(const UserPtr& aUser, bool remove);
	void checkMultiConn();
	void UpdateSlotCounts(UserConnection& aSource, uint8_t slotType);

	/* bundles */
	typedef unordered_map<string, UploadBundlePtr> RemoteBundleTokenMap;
	RemoteBundleTokenMap bundles;

	void createBundle(const AdcCommand& cmd);
	void changeBundle(const AdcCommand& cmd);
	void updateBundleInfo(const AdcCommand& cmd);
	void finishBundle(const AdcCommand& cmd);
	void removeBundleConnection(const AdcCommand& cmd);

	Upload* findUpload(const string& aToken);

	friend class Singleton<UploadManager>;
	UploadManager() noexcept;
	~UploadManager();

	bool getAutoSlot();
	void removeConnection(UserConnection* aConn);
	void removeUpload(Upload* aUpload, bool delay = false);
	void logUpload(const Upload* u);

	// ClientManagerListener
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept;
	
	// TimerManagerListener
	void on(Second, uint64_t aTick) noexcept;
	void on(Minute, uint64_t aTick) noexcept;

	// UserConnectionListener
	void on(BytesSent, UserConnection*, size_t, size_t) noexcept;
	void on(Failed, UserConnection*, const string&) noexcept;
	void on(Get, UserConnection*, const string&, int64_t) noexcept;
	void on(Send, UserConnection*) noexcept;
	void on(GetListLength, UserConnection* conn) noexcept;
	void on(TransmitDone, UserConnection*) noexcept;
	
	void on(AdcCommand::GET, UserConnection*, const AdcCommand&) noexcept;
	void on(AdcCommand::GFI, UserConnection*, const AdcCommand&) noexcept;

	bool prepareFile(UserConnection& aSource, const string& aType, const string& aFile, int64_t aResume, int64_t& aBytes, const string& userSID, bool listRecursive=false, bool tthList=false);
};

} // namespace dcpp

#endif // !defined(UPLOAD_MANAGER_H)
