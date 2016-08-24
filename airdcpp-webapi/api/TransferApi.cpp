/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <web-server/stdinc.h>
#include <web-server/Timer.h>

#include <api/TransferApi.h>
#include <api/TransferUtils.h>

#include <api/common/Serializer.h>

#include <airdcpp/DownloadManager.h>
#include <airdcpp/ConnectionManager.h>
#include <airdcpp/QueueManager.h>
#include <airdcpp/ThrottleManager.h>
#include <airdcpp/UploadManager.h>

namespace webserver {
	const PropertyList TransferApi::properties = {
		{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_TARGET, "target", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_DOWNLOAD, "download", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
		{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_STATUS, "status", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_BYTES_TRANSFERRED, "bytes_transferred", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_USER, "user", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_TIME_STARTED, "time_started", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_SPEED, "speed", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_SECONDS_LEFT, "seconds_left", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_IP, "ip", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_FLAGS, "flags", TYPE_LIST_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_ENCRYPTION, "encryption", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
	};

	const PropertyItemHandler<TransferInfoPtr> TransferApi::propertyHandler = {
		properties,
		TransferUtils::getStringInfo, TransferUtils::getNumericInfo, TransferUtils::compareItems, TransferUtils::serializeProperty
	};

	TransferApi::TransferApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::ANY),
		timer(getTimer([this] { onTimer(); }, 1000)),
		view("transfer_view", this, propertyHandler, std::bind(&TransferApi::getTransfers, this))
	{
		DownloadManager::getInstance()->addListener(this);
		UploadManager::getInstance()->addListener(this);
		ConnectionManager::getInstance()->addListener(this);

		METHOD_HANDLER("tranferred_bytes", Access::ANY, ApiRequest::METHOD_GET, (), false, TransferApi::handleGetTransferredBytes);
		METHOD_HANDLER("stats", Access::ANY, ApiRequest::METHOD_GET, (), false, TransferApi::handleGetTransferStats);

		METHOD_HANDLER("force", Access::TRANSFERS, ApiRequest::METHOD_POST, (TOKEN_PARAM), false, TransferApi::handleForce);
		METHOD_HANDLER("disconnect", Access::TRANSFERS, ApiRequest::METHOD_POST, (TOKEN_PARAM), false, TransferApi::handleDisconnect);

		createSubscription("transfer_statistics");
		timer->start(false);

		loadTransfers();
	}

	TransferApi::~TransferApi() {
		timer->stop(true);

		DownloadManager::getInstance()->removeListener(this);
		UploadManager::getInstance()->removeListener(this);
		ConnectionManager::getInstance()->removeListener(this);
	}

	void TransferApi::loadTransfers() noexcept {
		// add the existing connections
		{
			auto cm = ConnectionManager::getInstance();
			RLock l(cm->getCS());
			for (const auto& d : cm->getTransferConnections(true)) {
				auto info = addTransfer(d, "Inactive, waiting for status updates");
				updateQueueInfo(info);
			}

			for (const auto& u : cm->getTransferConnections(false)) {
				addTransfer(u, "Inactive, waiting for status updates");
			}
		}

		{
			auto um = UploadManager::getInstance();
			RLock l(um->getCS());
			for (const auto& u : um->getUploads()) {
				if (u->getUserConnection().getState() == UserConnection::STATE_RUNNING) {
					on(UploadManagerListener::Starting(), u);
				}
			}
		}

		{
			auto dm = DownloadManager::getInstance();
			RLock l(dm->getCS());
			for (const auto& d : dm->getDownloads()) {
				if (d->getUserConnection().getState() == UserConnection::STATE_RUNNING) {
					starting(d, "Downloading", true);
				}
			}
		}
	}

	void TransferApi::unloadTransfers() noexcept {
		WLock l(cs);
		transfers.clear();
	}

	TransferInfo::List TransferApi::getTransfers() const noexcept {
		TransferInfo::List ret;
		{
			RLock l(cs);
			boost::range::copy(transfers | map_values, back_inserter(ret));
		}

		return ret;
	}

	api_return TransferApi::handleGetTransferredBytes(ApiRequest& aRequest) {
		aRequest.setResponseBody({
			{ "session_downloaded", Socket::getTotalDown() },
			{ "session_uploaded", Socket::getTotalUp() },
			{ "start_total_downloaded", SETTING(TOTAL_DOWNLOAD) - Socket::getTotalDown() },
			{ "start_total_uploaded", SETTING(TOTAL_UPLOAD) - Socket::getTotalUp() },
		});

		return websocketpp::http::status_code::ok;
	}

	api_return TransferApi::handleForce(ApiRequest& aRequest) {
		auto item = getTransfer(aRequest);
		if (item->isDownload()) {
			ConnectionManager::getInstance()->force(item->getStringToken());
		}

		return websocketpp::http::status_code::ok;
	}

	api_return TransferApi::handleDisconnect(ApiRequest& aRequest) {
		auto item = getTransfer(aRequest);
		ConnectionManager::getInstance()->disconnect(item->getStringToken());

		return websocketpp::http::status_code::ok;
	}

	TransferInfoPtr TransferApi::getTransfer(ApiRequest& aRequest) const {
		// This isn't used ofter

		auto wantedId = aRequest.getTokenParam(0);

		RLock l(cs);
		auto ret = boost::find_if(transfers | map_values, [&](const TransferInfoPtr& aInfo) {
			return aInfo->getToken() == wantedId;
		});

		if (ret.base() == transfers.end()) {
			throw std::invalid_argument("Transfer not found");
		}

		return *ret;
	}

	api_return TransferApi::handleGetTransferStats(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeTransferStats());
		return websocketpp::http::status_code::ok;
	}

	json TransferApi::serializeTransferStats() const noexcept {
		auto resetSpeed = [](int transfers, int64_t speed) {
			return (transfers == 0 && speed < 10 * 1024) || speed < 1024;
		};

		auto uploads = UploadManager::getInstance()->getUploadCount();
		auto downloads = DownloadManager::getInstance()->getDownloadCount();

		auto downSpeed = DownloadManager::getInstance()->getLastDownSpeed();
		if (resetSpeed(downloads, downSpeed)) {
			downSpeed = 0;
		}

		auto upSpeed = DownloadManager::getInstance()->getLastUpSpeed();
		if (resetSpeed(uploads, upSpeed)) {
			upSpeed = 0;
		}

		return {
			{ "speed_down", downSpeed },
			{ "speed_up", upSpeed },
			{ "limit_down", ThrottleManager::getDownLimit() },
			{ "limit_up", ThrottleManager::getUpLimit() },
			{ "upload_bundles", lastUploadBundles },
			{ "download_bundles", lastDownloadBundles },
			{ "uploads", uploads },
			{ "downloads", downloads },
			{ "queued_bytes", QueueManager::getInstance()->getTotalQueueSize() },
			{ "session_downloaded", Socket::getTotalDown() },
			{ "session_uploaded", Socket::getTotalUp() },
		};
	}

	void TransferApi::onTimer() {
		if (!subscriptionActive("transfer_statistics"))
			return;

		auto newStats = serializeTransferStats();
		if (previousStats == newStats)
			return;

		lastUploadBundles = 0;
		lastDownloadBundles = 0;

		send("transfer_statistics", JsonUtil::filterExactValues(newStats, previousStats));
		previousStats.swap(newStats);
	}

	void TransferApi::onTick(const Transfer* aTransfer, bool aIsDownload) noexcept {
		auto t = getTransfer(aTransfer->getToken());
		if (!t) {
			return;
		}

		t->setSpeed(aTransfer->getAverageSpeed());
		t->setBytesTransferred(aTransfer->getPos());
		t->setTimeLeft(aTransfer->getSecondsLeft());

		uint64_t timeSinceStarted = GET_TICK() - t->getStarted();
		if (timeSinceStarted < 1000) {
			t->setStatusString("Starting...");
		} else {
			t->setStatusString(STRING_F(RUNNING_PCT, t->getPercentage()));
		}

		view.onItemUpdated(t, { PROP_STATUS, PROP_BYTES_TRANSFERRED, PROP_SPEED, PROP_SECONDS_LEFT });
	}

	void TransferApi::on(UploadManagerListener::Tick, const UploadList& aUploads) noexcept {
		for (const auto& ul : aUploads) {
			if (ul->getPos() == 0) continue;

			onTick(ul, false);
		}
	}

	void TransferApi::on(DownloadManagerListener::Tick, const DownloadList& aDownloads) noexcept {
		for (const auto& dl : aDownloads) {
			onTick(dl, true);
		}
	}

	void TransferApi::on(DownloadManagerListener::BundleTick, const BundleList& bundles, uint64_t aTick) noexcept {
		lastDownloadBundles = bundles.size();
	}

	void TransferApi::on(UploadManagerListener::BundleTick, const UploadBundleList& bundles) noexcept {
		lastUploadBundles = bundles.size();
	}

	TransferInfoPtr TransferApi::addTransfer(const ConnectionQueueItem* aCqi, const string& aStatus) noexcept {
		auto t = std::make_shared<TransferInfo>(aCqi->getHintedUser(), aCqi->getConnType() == ConnectionType::CONNECTION_TYPE_DOWNLOAD, aCqi->getToken());

		{
			WLock l(cs);
			transfers[aCqi->getToken()] = t;
		}

		t->setStatusString(aStatus);
		return t;
	}

	void TransferApi::on(ConnectionManagerListener::Added, const ConnectionQueueItem* aCqi) noexcept {
		if (aCqi->getConnType() == CONNECTION_TYPE_PM)
			return;

		auto t = addTransfer(aCqi, STRING(CONNECTING));
		view.onItemAdded(t);
	}

	void TransferApi::on(ConnectionManagerListener::Removed, const ConnectionQueueItem* aCqi) noexcept {
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

		view.onItemRemoved(t);
	}

	void TransferApi::onFailed(TransferInfoPtr& aInfo, const string& aReason) noexcept {
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

		view.onItemUpdated(aInfo, { PROP_STATUS, PROP_SPEED, PROP_BYTES_TRANSFERRED, PROP_SECONDS_LEFT });
	}

	void TransferApi::on(ConnectionManagerListener::Failed, const ConnectionQueueItem* aCqi, const string& aReason) noexcept {
		auto t = getTransfer(aCqi->getToken());
		if (!t) {
			return;
		}

		onFailed(t, aCqi->getUser()->isSet(User::OLD_CLIENT) ? STRING(SOURCE_TOO_OLD) : aReason);
	}

	void TransferApi::updateQueueInfo(TransferInfoPtr& aInfo) noexcept {
		QueueToken bundleToken = 0;
		string aTarget;
		int64_t aSize; int aFlags = 0;
		if (!QueueManager::getInstance()->getQueueInfo(aInfo->getHintedUser(), aTarget, aSize, aFlags, bundleToken)) {
			return;
		}

		auto type = Transfer::TYPE_FILE;
		if (aFlags & QueueItem::FLAG_PARTIAL_LIST)
			type = Transfer::TYPE_PARTIAL_LIST;
		else if (aFlags & QueueItem::FLAG_USER_LIST)
			type = Transfer::TYPE_FULL_LIST;

		aInfo->setType(type);
		aInfo->setTarget(aTarget);
		aInfo->setSize(aSize);

		aInfo->setState(TransferInfo::STATE_WAITING);
		aInfo->setStatusString(STRING(CONNECTING));

		aInfo->setState(TransferInfo::STATE_WAITING);

		view.onItemUpdated(aInfo, { PROP_STATUS, PROP_TARGET, PROP_TYPE, PROP_NAME, PROP_SIZE });
	}

	void TransferApi::on(ConnectionManagerListener::Connecting, const ConnectionQueueItem* aCqi) noexcept {
		auto t = getTransfer(aCqi->getToken());
		if (!t) {
			return;
		}

		updateQueueInfo(t);
	}

	void TransferApi::on(ConnectionManagerListener::UserUpdated, const ConnectionQueueItem* aCqi) noexcept {
		auto t = getTransfer(aCqi->getToken());
		if (!t) {
			return;
		}

		view.onItemUpdated(t, { PROP_USER });
	}

	void TransferApi::on(DownloadManagerListener::Failed, const Download* aDownload, const string& aReason) noexcept {
		auto t = getTransfer(aDownload->getToken());
		if (!t) {
			return;
		}

		auto status = aReason;
		if (aDownload->isSet(Download::FLAG_SLOWUSER)) {
			status += ": " + STRING(SLOW_USER);
		} else if (aDownload->getOverlapped() && !aDownload->isSet(Download::FLAG_OVERLAP)) {
			status += ": " + STRING(OVERLAPPED_SLOW_SEGMENT);
		}

		onFailed(t, status);
	}

	void TransferApi::starting(TransferInfoPtr& aInfo, const Transfer* aTransfer) noexcept {
		aInfo->setBytesTransferred(aTransfer->getPos());
		aInfo->setTarget(aTransfer->getPath());
		aInfo->setStarted(GET_TICK());
		aInfo->setType(aTransfer->getType());
		aInfo->setSize(aTransfer->getSegmentSize());

		aInfo->setState(TransferInfo::STATE_RUNNING);
		aInfo->setIp(aTransfer->getUserConnection().getRemoteIp());
		aInfo->setEncryption(aTransfer->getUserConnection().getEncryptionInfo());

		OrderedStringSet flags;
		aTransfer->appendFlags(flags);
		aInfo->setFlags(flags);

		view.onItemUpdated(aInfo, { PROP_STATUS, PROP_SPEED, PROP_BYTES_TRANSFERRED, PROP_TIME_STARTED, PROP_SIZE, PROP_TARGET, PROP_NAME, PROP_TYPE, PROP_IP, PROP_ENCRYPTION, PROP_FLAGS });
	}

	void TransferApi::on(DownloadManagerListener::Requesting, const Download* aDownload, bool hubChanged) noexcept {
		starting(aDownload, STRING(REQUESTING), true);
	}

	void TransferApi::starting(const Download* aDownload, const string& aStatus, bool aFullUpdate) noexcept {
		auto t = getTransfer(aDownload->getToken());
		if (!t) {
			return;
		}

		t->setStatusString(aStatus);

		if (aFullUpdate) {
			starting(t, aDownload);
		} else {
			// All flags weren't known when requesting
			OrderedStringSet flags;
			aDownload->appendFlags(flags);
			t->setFlags(flags);

			// Size was unknown for filelists when requesting
			t->setSize(aDownload->getSegmentSize());

			view.onItemUpdated(t, { PROP_STATUS, PROP_FLAGS, PROP_SIZE });
		}
	}

	void TransferApi::on(DownloadManagerListener::Starting, const Download* aDownload) noexcept {
		// No need for full update as it's done in the requesting phase
		starting(aDownload, "Starting...", false);
	}

	void TransferApi::on(UploadManagerListener::Starting, const Upload* aUpload) noexcept {
		auto t = getTransfer(aUpload->getToken());
		if (!t) {
			return;
		}

		starting(t, aUpload);
	}

	TransferInfoPtr TransferApi::getTransfer(const string& aToken) const noexcept {
		RLock l(cs);
		auto i = transfers.find(aToken);
		return i != transfers.end() ? i->second : nullptr;
	}

	void TransferApi::onTransferCompleted(const Transfer* aTransfer, bool aIsDownload) noexcept {
		auto t = getTransfer(aTransfer->getToken());
		if (!t) {
			return;
		}

		t->setStatusString("Finished, idle...");
		t->setSpeed(-1);
		t->setTimeLeft(-1);
		t->setBytesTransferred(aTransfer->getSegmentSize());
		t->setState(TransferInfo::STATE_FINISHED);

		view.onItemUpdated(t, { PROP_STATUS, PROP_SPEED, PROP_SECONDS_LEFT, PROP_TIME_STARTED, PROP_BYTES_TRANSFERRED });
	}
}