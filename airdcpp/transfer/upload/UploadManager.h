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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_MANAGER_H
#define DCPLUSPLUS_DCPP_UPLOAD_MANAGER_H

#include <airdcpp/forward.h>

#include <airdcpp/core/ActionHook.h>
#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/hash/value/MerkleTree.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/core/Speaker.h>
#include <airdcpp/util/text/StringMatch.h>
#include <airdcpp/core/timer/TimerManagerListener.h>
#include <airdcpp/transfer/upload/UploadManagerListener.h>
#include <airdcpp/transfer/upload/UploadSlot.h>
#include <airdcpp/connection/UserConnectionListener.h>

namespace dcpp {

class UploadQueueManager;
class UploadParser;
struct UploadRequest;
struct ParsedUpload;

class UploadManager : private UserConnectionListener, public Speaker<UploadManagerListener>, private TimerManagerListener, public Singleton<UploadManager>
{
public:
	ActionHook<OptionalUploadSlot, const UserConnection&, const ParsedUpload&> slotTypeHook;

	void setFreeSlotMatcher();

	/** @return Number of uploads. */ 
	size_t getUploadCount() const noexcept;

	/**
	 * @remarks This is only used in the tray icons. Could be used in
	 * MainFrame too.
	 *
	 * @return Running average download speed in Bytes/s
	 */
	int64_t getRunningAverageUnsafe() const noexcept;
	int64_t getRunningAverage() const noexcept {
		RLock l(cs);
		return getRunningAverageUnsafe();
	}
	
	uint8_t getSlots() const noexcept;

	/** @return Number of free slots. */
	uint8_t getFreeSlots() const noexcept;
	
	/** @internal */
	int getFreeExtraSlots() const noexcept;

	/** @internal */
	void addConnection(UserConnectionPtr conn) noexcept;
	void abortUpload(const string& aFile, bool aWaitDisconnected = true) noexcept;

	IGETSET(uint8_t, extra, Extra, 0);
	IGETSET(uint64_t, lastGrant, LastGrant, 0);

	SharedMutex& getCS() noexcept { return cs; }
	const UploadList& getUploads() const noexcept {
		return uploads;
	}

	using UploadCallback = std::function<void (Upload *)> &&;
	Callback getAsyncWrapper(TransferToken aToken, UploadCallback&& aCallback) const noexcept;

	Upload* findUploadUnsafe(TransferToken aToken) const noexcept;

	UploadQueueManager& getQueue() noexcept {
		return *queue.get();
	}
private:
	static Upload* findUpload(TransferToken aToken, const UploadList& aUploadList) noexcept;

	unique_ptr<UploadQueueManager> queue;

	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;
	StringMatch freeSlotMatcher;

	uint8_t runningUsers = 0;
	uint8_t mcnConnections = 0;
	uint8_t smallFileConnections = 0;

	int getFreeMultiConnUnsafe() const noexcept;

	UploadList uploads;
	UploadList delayUploads;
	mutable SharedMutex cs;
	mutable CriticalSection slotCS;
	
	using MultiConnMap = unordered_map<UserPtr, uint16_t, User::Hash>;
	MultiConnMap multiUploads;

	bool isUploadingMCN(const UserPtr& aUser) const noexcept;
	bool allowNewMultiConn(const UserConnection& aSource) const noexcept;
	void changeMultiConnSlot(const UserPtr& aUser, bool aRemove) noexcept;
	void disconnectExtraMultiConn() noexcept;

	void removeSlot(UserConnection& aSource) noexcept;
	void updateSlotCounts(UserConnection& aSource, const UploadSlot& aNewSlot) noexcept;

	friend class Singleton<UploadManager>;
	UploadManager() noexcept;
	~UploadManager() override;

	bool standardSlotsRemaining(const UserPtr& aUser) const noexcept;
	bool lowSpeedSlotsRemaining() const noexcept;

	void removeConnection(UserConnection* aConn) noexcept;
	void removeUpload(Upload* aUpload, bool aDelay = false) noexcept;
	void logUpload(const Upload* u) noexcept;
	
	void startTransfer(Upload* aUpload) noexcept;
	
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

	bool prepareFile(UserConnection& aSource, const UploadRequest& aRequest);

	unique_ptr<InputStream> resumeStream(const UserConnection& aSource, const UploadParser& aParser);

	// Parse slot type for the connection
	// Throws in case of hook errors
	OptionalUploadSlot parseSlotHookedThrow(const UserConnection& aSource, const UploadParser& aParser) const;

	OptionalUploadSlot parseAutoGrantHookedThrow(const UserConnection& aSource, const UploadParser& aParser) const;

	void deleteDelayUpload(Upload* aUpload, bool aResuming) noexcept;
	void disconnectOfflineUsers() noexcept;

	void checkExpiredDelayUploads();

	OptionalProfileToken findProfile(UserConnection& uc, const string& aUserSID) const noexcept;
};

} // namespace dcpp

#endif // !defined(UPLOAD_MANAGER_H)
