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

#include "forward.h"

#include "ActionHook.h"
#include "CriticalSection.h"
#include "MerkleTree.h"
#include "Message.h"
#include "Segment.h"
#include "Singleton.h"
#include "Speaker.h"
#include "StringMatch.h"
#include "TimerManagerListener.h"
#include "Transfer.h"
#include "UploadManagerListener.h"
#include "UserConnectionListener.h"
#include "UserInfoBase.h"

namespace dcpp {

class UploadQueueManager;

// typedef UserConnection::SlotTypes SlotType;
typedef uint8_t SlotType;

struct UploadRequest {
	UploadRequest(const string& aType, const string& aFile, const Segment& aSegment) : type(aType), file(aFile), segment(aSegment) {}
	UploadRequest(const string& aType, const string& aFile, const Segment& aSegment, const string& aUserSID, bool aListRecursive, bool aIsTTHList) : UploadRequest(aType, aFile, aSegment) {
		userSID = aUserSID;
		isTTHList = aIsTTHList;
		listRecursive = aListRecursive;
	}

	bool validate() const noexcept {
		auto failed = file.empty() || segment.getStart() < 0 || segment.getSize() < -1 || segment.getSize() == 0;
		return !failed;
	}

	bool isUserlist() const noexcept {
		return file == Transfer::USER_LIST_NAME_BZ || file == Transfer::USER_LIST_NAME_EXTRACTED;
	}

	const string& type;
	const string& file;
	Segment segment;
	string userSID;
	bool listRecursive = false;
	bool isTTHList = false;
};

struct ParsedUpload {
	string sourceFile;
	Transfer::Type type = Transfer::TYPE_LAST;
	int64_t fileSize = 0;

	bool partialFileSharing = false;
	bool miniSlot = false;
};


class UploadManager : private UserConnectionListener, public Speaker<UploadManagerListener>, private TimerManagerListener, public Singleton<UploadManager>
{
public:
	ActionHook<SlotType, const HintedUser&, const ParsedUpload&> slotTypeHook;

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
		
	IGETSET(uint8_t, extraPartial, ExtraPartial, 0);
	IGETSET(uint8_t, extra, Extra, 0);
	IGETSET(uint64_t, lastGrant, LastGrant, 0);

	SharedMutex& getCS() noexcept { return cs; }
	const UploadList& getUploads() const noexcept {
		return uploads;
	}

	bool callAsync(const string& aToken, std::function<void(const Upload*)>&& aHandler) const noexcept;

	Upload* findUploadUnsafe(const string& aToken) const noexcept;

	UploadQueueManager& getQueue() noexcept {
		return *queue.get();
	}
private:
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
	
	typedef unordered_map<UserPtr, uint16_t, User::Hash> MultiConnMap;
	MultiConnMap multiUploads;

	bool isUploadingMCN(const UserPtr& aUser) const noexcept;
	bool allowNewMultiConn(const UserConnection& aSource) const noexcept;
	void changeMultiConnSlot(const UserPtr& aUser, bool aRemove) noexcept;
	void disconnectExtraMultiConn() noexcept;

	void removeSlot(UserConnection& aSource) noexcept;
	void updateSlotCounts(UserConnection& aSource, SlotType aNewSlotType) noexcept;

	friend class Singleton<UploadManager>;
	UploadManager() noexcept;
	~UploadManager();

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

	class UploadParser : public ParsedUpload {
	public:
		class UploadParserException : public Exception {
		public:
			UploadParserException(const string& aError, bool aNoAccess) : Exception(aError), noAccess(aNoAccess) {

			}

			const bool noAccess;
		};

		UploadParser(const StringMatch& aFreeSlotMatcher) : freeSlotMatcher(aFreeSlotMatcher) {}

		void parseFileInfo(const UploadRequest& aRequest, ProfileToken aProfile, const HintedUser& aUser);
		Upload* toUpload(UserConnection& aSource, const UploadRequest& aRequest, unique_ptr<InputStream>& is, ProfileToken aProfile);
	private:

		void toRealWithSize(const UploadRequest& aRequest, ProfileToken aProfile, const HintedUser& aUser);
		const StringMatch& freeSlotMatcher;
	};

	bool prepareFile(UserConnection& aSource, const UploadRequest& aRequest);

	unique_ptr<InputStream> resumeStream(const UserConnection& aSource, const UploadParser& aParser);

	// Parse slot type for the connection
	// Throws in case of hook errors
	SlotType parseSlotTypeHookedThrow(const UserConnection& aSource, const UploadParser& aParser) const;

	SlotType parseAutoGrantHookedThrow(const UserConnection& aSource, const UploadParser& aParser) const;

	void deleteDelayUpload(Upload* aUpload, bool aResuming) noexcept;
};

} // namespace dcpp

#endif // !defined(UPLOAD_MANAGER_H)
