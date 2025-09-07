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

#include <web-server/Timer.h>

#include <api/TransferApi.h>

#include <airdcpp/transfer/download/Download.h>
#include <airdcpp/transfer/upload/Upload.h>

#include <airdcpp/transfer/download/DownloadManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/connection/ThrottleManager.h>
#include <airdcpp/transfer/TransferInfoManager.h>
#include <airdcpp/transfer/upload/UploadManager.h>


namespace webserver {
	TransferApi::TransferApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::TRANSFERS),
		timer(getTimer([this] { onTimer(); }, 1000)),
		view("transfer_view", this, TransferUtils::propertyHandler, std::bind(&TransferApi::getTransfers, this))
	{
		createSubscriptions({
			"transfer_statistics",
			"transfer_added",
			"transfer_updated",
			"transfer_removed",

			// These are included in transfer_updated events as well
			"transfer_starting",
			"transfer_completed",
			"transfer_failed",
		});

		METHOD_HANDLER(Access::TRANSFERS,	METHOD_GET,		(),											TransferApi::handleGetTransfers);
		METHOD_HANDLER(Access::TRANSFERS,	METHOD_GET,		(TOKEN_PARAM),								TransferApi::handleGetTransfer);

		METHOD_HANDLER(Access::TRANSFERS,	METHOD_POST,	(TOKEN_PARAM, EXACT_PARAM("force")),		TransferApi::handleForce);
		METHOD_HANDLER(Access::TRANSFERS,	METHOD_POST,	(TOKEN_PARAM, EXACT_PARAM("disconnect")),	TransferApi::handleDisconnect);

		METHOD_HANDLER(Access::TRANSFERS, METHOD_GET,		(EXACT_PARAM("tranferred_bytes")),			TransferApi::handleGetTransferredBytes); // DEPRECATED (typo)
		METHOD_HANDLER(Access::TRANSFERS, METHOD_GET,		(EXACT_PARAM("transferred_bytes")),			TransferApi::handleGetTransferredBytes);
		METHOD_HANDLER(Access::TRANSFERS,	METHOD_GET,		(EXACT_PARAM("stats")),						TransferApi::handleGetTransferStats);

		timer->start(false);

		TransferInfoManager::getInstance()->addListener(this);
	}

	TransferApi::~TransferApi() {
		timer->stop(true);

		TransferInfoManager::getInstance()->removeListener(this);
	}

	TransferInfo::List TransferApi::getTransfers() const noexcept {
		return TransferInfoManager::getInstance()->getTransfers();
	}

	api_return TransferApi::handleGetTransfers(ApiRequest& aRequest) {
		auto j = Serializer::serializeItemList(TransferUtils::propertyHandler, getTransfers());
		aRequest.setResponseBody(j);
		return http_status::ok;
	}

	api_return TransferApi::handleGetTransfer(ApiRequest& aRequest) {
		auto item = getTransfer(aRequest);

		aRequest.setResponseBody(Serializer::serializeItem(item, TransferUtils::propertyHandler));
		return http_status::ok;
	}

	api_return TransferApi::handleGetTransferredBytes(ApiRequest& aRequest) {
		aRequest.setResponseBody({
			{ "session_downloaded", Socket::getTotalDown() },
			{ "session_uploaded", Socket::getTotalUp() },
			{ "start_total_downloaded", SETTING(TOTAL_DOWNLOAD) - Socket::getTotalDown() },
			{ "start_total_uploaded", SETTING(TOTAL_UPLOAD) - Socket::getTotalUp() },
		});

		return http_status::ok;
	}

	api_return TransferApi::handleForce(ApiRequest& aRequest) {
		auto item = getTransfer(aRequest);
		if (item->isDownload()) {
			ConnectionManager::getInstance()->force(item->getStringToken());
		}

		return http_status::no_content;
	}

	api_return TransferApi::handleDisconnect(ApiRequest& aRequest) {
		auto item = getTransfer(aRequest);
		ConnectionManager::getInstance()->disconnect(item->getStringToken());

		return http_status::no_content;
	}

	TransferInfoPtr TransferApi::getTransfer(ApiRequest& aRequest) const {
		auto transferId = aRequest.getTokenParam();

		auto t = TransferInfoManager::getInstance()->findTransfer(transferId);
		if (!t) {
			throw RequestException(http_status::not_found, "Transfer " + Util::toString(transferId) + " was not found");
		}

		return t;
	}

	api_return TransferApi::handleGetTransferStats(ApiRequest& aRequest) {
		aRequest.setResponseBody(serializeTransferStats());
		return http_status::ok;
	}

	json TransferApi::serializeTransferStats() const noexcept {
		auto resetSpeed = [](int transfers, int64_t speed) {
			return (transfers == 0 && speed < 10 * 1024) || speed < 1024;
		};

		auto uploads = UploadManager::getInstance()->getUploadCount();
		auto downloads = DownloadManager::getInstance()->getTotalDownloadConnectionCount();

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
			{ "upload_bundles", 0 }, // API doesn't use upload bundles at the moment
			{ "download_bundles", DownloadManager::getInstance()->getRunningBundleCount() },
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

		send("transfer_statistics", Serializer::serializeChangedProperties(newStats, previousStats));
		previousStats.swap(newStats);
	}

	void TransferApi::on(TransferInfoManagerListener::Added, const TransferInfoPtr& aInfo) noexcept {
		view.onItemAdded(aInfo);
		if (subscriptionActive("transfer_added")) {
			send("transfer_added", Serializer::serializeItem(aInfo, TransferUtils::propertyHandler));
		}
	}

	PropertyIdSet TransferApi::updateFlagsToPropertyIds(int aUpdatedProperties) noexcept {
		PropertyIdSet updatedProps;
		if (aUpdatedProperties & TransferInfo::UpdateFlags::TARGET)
			updatedProps.insert(TransferUtils::PROP_TARGET);
			updatedProps.insert(TransferUtils::PROP_NAME);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::TYPE)
			updatedProps.insert(TransferUtils::PROP_TYPE);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::SIZE)
			updatedProps.insert(TransferUtils::PROP_SIZE);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::STATUS)
			updatedProps.insert(TransferUtils::PROP_STATUS);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::BYTES_TRANSFERRED)
			updatedProps.insert(TransferUtils::PROP_BYTES_TRANSFERRED);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::USER)
			updatedProps.insert(TransferUtils::PROP_USER);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::TIME_STARTED)
			updatedProps.insert(TransferUtils::PROP_TIME_STARTED);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::SPEED)
			updatedProps.insert(TransferUtils::PROP_SPEED);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::SECONDS_LEFT)
			updatedProps.insert(TransferUtils::PROP_SECONDS_LEFT);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::IP)
			updatedProps.insert(TransferUtils::PROP_IP);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::FLAGS)
			updatedProps.insert(TransferUtils::PROP_FLAGS);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::SUPPORTS)
			updatedProps.insert(TransferUtils::PROP_SUPPORTS);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::ENCRYPTION)
			updatedProps.insert(TransferUtils::PROP_ENCRYPTION);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::QUEUE_ID)
			updatedProps.insert(TransferUtils::PROP_QUEUE_ID);
		if (aUpdatedProperties & TransferInfo::UpdateFlags::STATE)
			updatedProps.insert(TransferUtils::PROP_STATUS);

		return updatedProps;
	}

	void TransferApi::on(TransferInfoManagerListener::Updated, const TransferInfoPtr& aInfo, int aUpdatedProperties, bool aTick) noexcept {
		auto updatedProps = updateFlagsToPropertyIds(aUpdatedProperties);

		view.onItemUpdated(aInfo, updatedProps);
		if (subscriptionActive("transfer_updated")) {
			send("transfer_updated", Serializer::serializePartialItem(aInfo, TransferUtils::propertyHandler, updatedProps));
		}
	}

	void TransferApi::on(TransferInfoManagerListener::Removed, const TransferInfoPtr& aInfo) noexcept {
		view.onItemRemoved(aInfo);
		if (subscriptionActive("transfer_removed")) {
			send("transfer_removed", Serializer::serializeItem(aInfo, TransferUtils::propertyHandler));
		}
	}

	void TransferApi::on(TransferInfoManagerListener::Failed, const TransferInfoPtr& aInfo) noexcept { 
		if (subscriptionActive("transfer_failed")) {
			send("transfer_failed", Serializer::serializeItem(aInfo, TransferUtils::propertyHandler));
		}
	}

	void TransferApi::on(TransferInfoManagerListener::Starting, const TransferInfoPtr& aInfo) noexcept {
		if (subscriptionActive("transfer_starting")) {
			send("transfer_starting", Serializer::serializeItem(aInfo, TransferUtils::propertyHandler));
		}
	}

	void TransferApi::on(TransferInfoManagerListener::Completed, const TransferInfoPtr& aInfo) noexcept {
		if (subscriptionActive("transfer_completed")) {
			send("transfer_completed", Serializer::serializeItem(aInfo, TransferUtils::propertyHandler));
		}
	}
}