/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#include <airdcpp/transfer/TransferInfoManager.h>

#include <airdcpp/transfer/download/Download.h>
#include <airdcpp/transfer/upload/Upload.h>

#include <airdcpp/transfer/download/DownloadManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/connection/ThrottleManager.h>
#include <airdcpp/transfer/upload/UploadManager.h>


namespace dcpp {
	TransferInfoManager::TransferInfoManager() {
		DownloadManager::getInstance()->addListener(this);
		UploadManager::getInstance()->addListener(this);
		ConnectionManager::getInstance()->addListener(this);
	}

	TransferInfoManager::~TransferInfoManager() {
		DownloadManager::getInstance()->removeListener(this);
		UploadManager::getInstance()->removeListener(this);
		ConnectionManager::getInstance()->removeListener(this);
	}

	TransferInfo::List TransferInfoManager::getTransfers() const noexcept {
		TransferInfo::List ret;
		{
			RLock l(cs);
			ranges::copy(transfers | views::values, back_inserter(ret));
		}

		return ret;
	}

	TransferInfoPtr TransferInfoManager::onTick(const Transfer* aTransfer, bool aIsDownload) noexcept {
		auto t = findTransfer(aTransfer->getConnectionToken());
		if (!t) {
			return nullptr;
		}

		t->setSpeed(aTransfer->getAverageSpeed());
		t->setBytesTransferred(aTransfer->getPos());
		t->setTimeLeft(aTransfer->getSecondsLeft());

		uint64_t timeSinceStarted = GET_TICK() - t->getStarted();
		if (timeSinceStarted < 1000) {
			t->setStatusString(aIsDownload ? STRING(DOWNLOAD_STARTING) : STRING(UPLOAD_STARTING));
		} else {
			t->setStatusString(STRING_F(RUNNING_PCT, t->getPercentage()));
		}

		onTransferUpdated(
			t,
			TransferInfo::UpdateFlags::STATUS | TransferInfo::UpdateFlags::BYTES_TRANSFERRED |
			TransferInfo::UpdateFlags::SPEED | TransferInfo::UpdateFlags::SECONDS_LEFT,
			true
		);

		return t;
	}

	void TransferInfoManager::on(UploadManagerListener::Tick, const UploadList& aUploads) noexcept {
		TransferInfo::List tickTransfers;
		for (const auto& ul : aUploads) {
			if (ul->getPos() == 0) continue;

			auto t = onTick(ul, false);
			if (t) {
				tickTransfers.push_back(t);
			}
		}

		if (!tickTransfers.empty()) {
			fire(TransferInfoManagerListener::Tick(), tickTransfers, TransferInfo::UpdateFlags::STATUS | TransferInfo::UpdateFlags::BYTES_TRANSFERRED |
				TransferInfo::UpdateFlags::SPEED | TransferInfo::UpdateFlags::SECONDS_LEFT);
		}
	}

	void TransferInfoManager::on(DownloadManagerListener::Tick, const DownloadList& aDownloads, uint64_t) noexcept {
		TransferInfo::List tickTransfers;
		for (const auto& dl : aDownloads) {
			auto t = onTick(dl, true);
			if (t) {
				tickTransfers.push_back(t);
			}
		}

		if (!tickTransfers.empty()) {
			fire(TransferInfoManagerListener::Tick(), tickTransfers, TransferInfo::UpdateFlags::STATUS | TransferInfo::UpdateFlags::BYTES_TRANSFERRED |
				TransferInfo::UpdateFlags::SPEED | TransferInfo::UpdateFlags::SECONDS_LEFT);
		}
	}

	TransferInfoPtr TransferInfoManager::addTransfer(const ConnectionQueueItem* aCqi, const string& aStatus) noexcept {
		auto t = std::make_shared<TransferInfo>(aCqi->getUser(), aCqi->getConnType() == ConnectionType::CONNECTION_TYPE_DOWNLOAD, aCqi->getToken());

		{
			WLock l(cs);
			transfers[aCqi->getToken()] = t;
		}

		t->setStatusString(aStatus);
		updateQueueInfo(t);
		return t;
	}

	void TransferInfoManager::on(ConnectionManagerListener::Added, const ConnectionQueueItem* aCqi) noexcept {
		if (aCqi->getConnType() == CONNECTION_TYPE_PM)
			return;

		auto t = addTransfer(aCqi, STRING(CONNECTING));

		fire(TransferInfoManagerListener::Added(), t);
	}

	void TransferInfoManager::on(ConnectionManagerListener::Removed, const ConnectionQueueItem* aCqi) noexcept {
		TransferInfoPtr t;

		{
			WLock l(cs);
			auto i = transfers.find(aCqi->getToken());
			if (i == transfers.end()) {
				return;
			}

			t = i->second;
			transfers.erase(i);
		}

		fire(TransferInfoManagerListener::Removed(), t);
	}

	void TransferInfoManager::onFailed(TransferInfoPtr& aInfo, const string& aReason) noexcept {
		if (aInfo->getState() == TransferInfo::STATE_FAILED) {
			// The connection is disconnected right after download fails, which causes double events
			// Don't override the previous message
			return;
		}

		aInfo->setStatusString(aReason);
		aInfo->setSpeed(-1);
		aInfo->setBytesTransferred(-1);
		aInfo->setTimeLeft(-1);
		aInfo->setState(TransferInfo::STATE_FAILED);

		onTransferUpdated(
			aInfo,
			TransferInfo::UpdateFlags::STATUS | TransferInfo::UpdateFlags::SPEED | TransferInfo::UpdateFlags::STATE |
			TransferInfo::UpdateFlags::BYTES_TRANSFERRED | TransferInfo::UpdateFlags::SECONDS_LEFT
		);

		fire(TransferInfoManagerListener::Failed(), aInfo);
	}

	void TransferInfoManager::on(ConnectionManagerListener::Failed, const ConnectionQueueItem* aCqi, const string& aReason) noexcept {
		auto t = findTransfer(aCqi->getToken());
		if (!t) {
			return;
		}

		t->setBundle(aCqi->getLastBundle());
		onFailed(t, aCqi->getUser().user->isSet(User::OLD_CLIENT) ? STRING(SOURCE_TOO_OLD) : aReason);
	}

	void TransferInfoManager::onTransferUpdated(const TransferInfoPtr& aTransfer, int aUpdatedProperties, bool aTick) noexcept {
		fire(TransferInfoManagerListener::Updated(), aTransfer, aUpdatedProperties, aTick);
	}

	void TransferInfoManager::updateQueueInfo(const TransferInfoPtr& aInfo) noexcept {
		if (!aInfo->isDownload()) {
			return;
		}

		auto result = QueueManager::getInstance()->startDownload(aInfo->getHintedUser(), QueueDownloadType::ANY);
		if (!result.qi) {
			return;
		}

		auto type = Transfer::TYPE_FILE;
		if (result.qi->getFlags() & QueueItem::FLAG_PARTIAL_LIST)
			type = Transfer::TYPE_PARTIAL_LIST;
		else if (result.qi->getFlags() & QueueItem::FLAG_USER_LIST)
			type = Transfer::TYPE_FULL_LIST;

		aInfo->setType(type);
		aInfo->setTarget(result.qi->getTarget());
		aInfo->setSize(result.qi->getSize());
		aInfo->setQueueToken(result.qi->getToken());
	}

	void TransferInfoManager::on(ConnectionManagerListener::Connecting, const ConnectionQueueItem* aCqi) noexcept {
		auto t = findTransfer(aCqi->getToken());
		if (!t) {
			return;
		}

		t->setState(TransferInfo::STATE_WAITING);
		t->setStatusString(STRING(CONNECTING));
		t->setHubUrl(aCqi->getHubUrl());

		updateQueueInfo(t);

		onTransferUpdated(
			t,
			TransferInfo::UpdateFlags::STATUS | TransferInfo::UpdateFlags::TARGET | TransferInfo::UpdateFlags::TYPE |
			TransferInfo::UpdateFlags::SIZE | TransferInfo::UpdateFlags::QUEUE_ID | TransferInfo::UpdateFlags::STATE |
			TransferInfo::UpdateFlags::USER
		);
	}


	void TransferInfoManager::on(ConnectionManagerListener::Forced, const ConnectionQueueItem* aCqi) noexcept {
		auto t = findTransfer(aCqi->getToken());
		if (!t) {
			return;
		}

		if (t->getState() != TransferInfo::STATE_RUNNING) {
			t->setState(TransferInfo::STATE_WAITING);
		}

		t->setStatusString(STRING(CONNECTING_FORCED));
		onTransferUpdated(t, TransferInfo::UpdateFlags::STATUS | TransferInfo::UpdateFlags::STATE);
	}

	void TransferInfoManager::on(ConnectionManagerListener::UserUpdated, const ConnectionQueueItem* aCqi) noexcept {
		auto t = findTransfer(aCqi->getToken());
		if (!t) {
			return;
		}

		t->setHubUrl(aCqi->getHubUrl());
		onTransferUpdated(t, TransferInfo::UpdateFlags::USER);
	}

	void TransferInfoManager::on(DownloadManagerListener::Failed, const Download* aDownload, const string& aReason) noexcept {
		auto t = findTransfer(aDownload->getConnectionToken());
		if (!t) {
			return;
		}

		auto status = aReason;
		if (aDownload->isSet(Download::FLAG_SLOWUSER)) {
			status += ": " + STRING(SLOW_USER);
		}
		else if (aDownload->getOverlapped() && !aDownload->isSet(Download::FLAG_OVERLAP)) {
			status += ": " + STRING(OVERLAPPED_SLOW_SEGMENT);
		}

		onFailed(t, status);
	}

	void TransferInfoManager::starting(TransferInfoPtr& aInfo, const Transfer* aTransfer) noexcept {
		aInfo->setBytesTransferred(aTransfer->getPos());
		aInfo->setTarget(aTransfer->getPath());
		aInfo->setStarted(GET_TICK());
		aInfo->setType(aTransfer->getType());
		aInfo->setSize(aTransfer->getSegmentSize());

		aInfo->setState(TransferInfo::STATE_RUNNING);
		aInfo->setIp(aTransfer->getUserConnection().getRemoteIp());
		aInfo->setEncryption(aTransfer->getUserConnection().getEncryptionInfo());

		{
			OrderedStringSet flags;
			aTransfer->appendFlags(flags);
			aInfo->setFlags(flags);
		}

		aInfo->setSupports(aTransfer->getUserConnection().getSupports().getAll());

		onTransferUpdated(
			aInfo,
			TransferInfo::UpdateFlags::STATUS | TransferInfo::UpdateFlags::SPEED |
			TransferInfo::UpdateFlags::BYTES_TRANSFERRED | TransferInfo::UpdateFlags::TIME_STARTED |
			TransferInfo::UpdateFlags::SIZE | TransferInfo::UpdateFlags::TARGET | TransferInfo::UpdateFlags::STATE |
			TransferInfo::UpdateFlags::QUEUE_ID | TransferInfo::UpdateFlags::TYPE |
			TransferInfo::UpdateFlags::IP | TransferInfo::UpdateFlags::ENCRYPTION | TransferInfo::UpdateFlags::FLAGS | 
			TransferInfo::UpdateFlags::SUPPORTS
		);


		fire(TransferInfoManagerListener::Starting(), aInfo);
	}

	void TransferInfoManager::on(DownloadManagerListener::Requesting, const Download* aDownload, bool /*hubChanged*/) noexcept {
		starting(aDownload, STRING(REQUESTING), true);
	}

	void TransferInfoManager::on(DownloadManagerListener::Idle, const UserConnection* aConn, const string& aError) noexcept {
		if (aError.empty()) {
			return;
		}

		auto t = findTransfer(aConn->getToken());
		if (!t) {
			return;
		}

		t->setStatusString(aError);
		onTransferUpdated(t, TransferInfo::UpdateFlags::STATUS);
	}

	void TransferInfoManager::starting(const Download* aDownload, const string& aStatus, bool aFullUpdate) noexcept {
		auto t = findTransfer(aDownload->getConnectionToken());
		if (!t) {
			return;
		}

		t->setStatusString(aStatus);

		if (aFullUpdate) {
			t->setBundle(aDownload->getBundle() ? Util::toString(aDownload->getBundle()->getToken()) : Util::emptyString);
			starting(t, aDownload);
		} else {
			// All flags weren't known when requesting
			OrderedStringSet flags;
			aDownload->appendFlags(flags);
			t->setFlags(flags);

			// Size was unknown for filelists when requesting
			t->setSize(aDownload->getSegmentSize());

			onTransferUpdated(
				t,
				TransferInfo::UpdateFlags::STATUS | TransferInfo::UpdateFlags::FLAGS |
				TransferInfo::UpdateFlags::SIZE
			);

			fire(TransferInfoManagerListener::Starting(), t);
		}
	}

	void TransferInfoManager::on(DownloadManagerListener::Starting, const Download* aDownload) noexcept {
		// No need for full update as it's done in the requesting phase
		starting(aDownload, STRING(DOWNLOAD_STARTING), false);
	}

	void TransferInfoManager::on(UploadManagerListener::Starting, const Upload* aUpload) noexcept {
		auto t = findTransfer(aUpload->getConnectionToken());
		if (!t) {
			return;
		}

		starting(t, aUpload);
	}

	TransferInfoPtr TransferInfoManager::findTransfer(const string& aToken) const noexcept {
		RLock l(cs);
		auto i = transfers.find(aToken);
		return i != transfers.end() ? i->second : nullptr;
	}

	TransferInfoPtr TransferInfoManager::findTransfer(TransferInfoToken aToken) const noexcept {
		RLock l(cs);
		auto ret = ranges::find_if(transfers | views::values, [&](const TransferInfoPtr& aInfo) {
			return aInfo->getToken() == aToken;
		});

		if (ret.base() == transfers.end()) {
			return nullptr;
		}

		return *ret;
	}

	void TransferInfoManager::on(DownloadManagerListener::Complete, const Download* aDownload, bool) noexcept {
		onTransferCompleted(aDownload, true);
	}

	void TransferInfoManager::on(UploadManagerListener::Complete, const Upload* aUpload) noexcept {
		onTransferCompleted(aUpload, false);
	}

	void TransferInfoManager::onTransferCompleted(const Transfer* aTransfer, bool aIsDownload) noexcept {
		auto t = findTransfer(aTransfer->getConnectionToken());
		if (!t) {
			return;
		}

		t->setStatusString(aIsDownload ? STRING(DOWNLOAD_FINISHED_IDLE) : STRING(UPLOAD_FINISHED_IDLE));
		t->setSpeed(0);
		t->setTimeLeft(0);
		t->setBytesTransferred(aTransfer->getSegmentSize());
		t->setState(TransferInfo::STATE_FINISHED);

		onTransferUpdated(
			t,
			TransferInfo::UpdateFlags::STATUS | TransferInfo::UpdateFlags::SPEED |
			TransferInfo::UpdateFlags::SECONDS_LEFT | TransferInfo::UpdateFlags::TIME_STARTED |
			TransferInfo::UpdateFlags::BYTES_TRANSFERRED | TransferInfo::UpdateFlags::STATE
		);

		fire(TransferInfoManagerListener::Completed(), t);
	}
}