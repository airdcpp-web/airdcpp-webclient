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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_MANAGER_H
#define DCPLUSPLUS_DCPP_UPLOAD_MANAGER_H

#include "forward.h"
#include "UserConnectionListener.h"
#include "Singleton.h"
#include "UploadManagerListener.h"
#include "Client.h"
#include "ClientManager.h"
#include "ClientManagerListener.h"
#include "MerkleTree.h"
#include "FastAlloc.h"

namespace dcpp {

struct WaitingUser {
	HintedUser user;
	string token;
	
	WaitingUser(const HintedUser& _user, const std::string& _token) : user(_user), token(_token) { }
	
	operator const UserPtr&() const { return user.user; }
};

class UploadQueueItem :  public FastAlloc<UploadQueueItem>, public intrusive_ptr_base<UploadQueueItem>, public UserInfoBase {
public:
	UploadQueueItem(const HintedUser& u, const string& _file, int64_t p, int64_t sz, uint64_t itime) :
		user(u), file(_file), pos(p), size(sz), time(itime) { inc(); }
	
	typedef vector<UploadQueueItem*> List;
	typedef deque<pair<WaitingUser, UploadQueueItem::List>> SlotQueue;

	static int compareItems(const UploadQueueItem* a, const UploadQueueItem* b, uint8_t col) {
		switch(col) {
			case COLUMN_TRANSFERRED: return compare(a->pos, b->pos);
			case COLUMN_SIZE: return compare(a->size, b->size);
			case COLUMN_ADDED:
			case COLUMN_WAITING: return compare(a->time, b->time);
			default: return lstrcmpi(a->getText(col).c_str(), b->getText(col).c_str());
		}
		return 0;
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

	const string& getFile() const { return file; }
	const UserPtr& getUser() const { return user.user; }
	const HintedUser& getHintedUser() const { return user; }
	int64_t getSize() const { return size; }
	uint64_t getTime() const { return time; }

	GETSET(int64_t, pos, Pos);
	
private:
	string file;
	int64_t size;
	uint64_t time;
	
	HintedUser user;	
};

class UploadManager : private ClientManagerListener, private UserConnectionListener, public Speaker<UploadManagerListener>, private TimerManagerListener, public Singleton<UploadManager>
{
public:
	/** @return Number of uploads. */ 
	size_t getUploadCount() { Lock l(cs); return uploads.size(); }

	/**
	 * @remarks This is only used in the tray icons. Could be used in
	 * MainFrame too.
	 *
	 * @return Running average download speed in Bytes/s
	 */
	int64_t getRunningAverage();
	
	uint8_t getSlots() const { return (uint8_t)(max(SETTING(SLOTS), max(SETTING(HUB_SLOTS),0) * Client::getTotalCounts())); }

	/** @return Number of free slots. */
	uint8_t getFreeSlots() const { return (uint8_t)max((getSlots() - running), 0); }
	
	/** @internal */
	int getFreeExtraSlots() const { return max(SETTING(EXTRA_SLOTS) - getExtra(), 0); }
	
	/** @param aUser Reserve an upload slot for this user and connect. */
	void reserveSlot(const HintedUser& aUser, uint64_t aTime);
	void unreserveSlot(const UserPtr& aUser);
	void clearUserFiles(const UserPtr&);
	const UploadQueueItem::SlotQueue getUploadQueue();
	bool hasReservedSlot(const UserPtr& aUser) { Lock l(cs); return reservedSlots.find(aUser) != reservedSlots.end(); }
	bool isConnecting(const UserPtr& aUser) const { return connectingUsers.find(aUser) != connectingUsers.end(); }
	bool isUploading(const CID cid) const { return multiUploads.find(cid) != multiUploads.end(); }
	//bool isUploading(const UserPtr& aUser);
	void unreserveSlot(const UserPtr& aUser, bool add);
	bool getMultiConn(const UserConnection& aSource);
	void changeMultiConnSlot(const CID cid, bool remove);
	void checkMultiConn();

	/** @internal */
	void addConnection(UserConnectionPtr conn);
	void removeDelayUpload(const UserPtr& aUser);
	void abortUpload(const string& aFile, bool waiting = true);
		
	GETSET(uint8_t, extraPartial, ExtraPartial);
	GETSET(uint8_t, extra, Extra);
	GETSET(uint64_t, lastGrant, LastGrant);
	GETSET(uint8_t, extraAir, ExtraAir);


private:
	uint8_t running;
	uint8_t mcnSlots;
	uint8_t smallSlots;

	UploadList uploads;
	UploadList delayUploads;
	CriticalSection cs;

	int lastFreeSlots; /// amount of free slots at the previous minute
	
	typedef unordered_map<CID, uint8_t> MultiConnMap;
	typedef MultiConnMap::iterator MultiConnIter;
	typedef unordered_map<UserPtr, uint64_t, User::Hash> SlotMap;
	typedef SlotMap::iterator SlotIter;
	SlotMap reservedSlots;
	SlotMap connectingUsers;
	MultiConnMap multiUploads;
	UploadQueueItem::SlotQueue uploadQueue;

	size_t addFailedUpload(const UserConnection& source, const string& file, int64_t pos, int64_t size);
	void notifyQueuedUsers();


	friend class Singleton<UploadManager>;
	UploadManager() noexcept;
	~UploadManager();

	bool getAutoSlot();
	void removeConnection(UserConnection* aConn);
	void removeUpload(Upload* aUpload, bool delay = false);
	void logUpload(const Upload* u);

	// ClientManagerListener
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser) noexcept;
	
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

	bool prepareFile(UserConnection& aSource, const string& aType, const string& aFile, int64_t aResume, int64_t& aBytes, bool listRecursive = false, bool tthList = false);
};

} // namespace dcpp

#endif // !defined(UPLOAD_MANAGER_H)

/**
 * @file
 * $Id: UploadManager.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
