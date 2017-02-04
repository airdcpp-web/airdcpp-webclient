/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_TRANSFERAPI_H
#define DCPLUSPLUS_DCPP_TRANSFERAPI_H

#include <web-server/stdinc.h>

#include <api/ApiModule.h>
#include <api/TransferInfo.h>
#include <api/TransferUtils.h>

#include <api/common/ListViewController.h>

#include <airdcpp/typedefs.h>

#include <airdcpp/Transfer.h>

#include <airdcpp/ConnectionManagerListener.h>
#include <airdcpp/DownloadManagerListener.h>
#include <airdcpp/UploadManagerListener.h>


namespace webserver {
	class TransferApi : public SubscribableApiModule, private ConnectionManagerListener, private DownloadManagerListener, private UploadManagerListener {
	public:
		TransferApi(Session* aSession);
		~TransferApi();
	private:
		void loadTransfers() noexcept;
		void unloadTransfers() noexcept;

		json serializeTransferStats() const noexcept;

		api_return handleGetTransfers(ApiRequest& aRequest);
		api_return handleGetTransfer(ApiRequest& aRequest);

		api_return handleGetTransferredBytes(ApiRequest& aRequest);
		api_return handleGetTransferStats(ApiRequest& aRequest);
		api_return handleForce(ApiRequest& aRequest);
		api_return handleDisconnect(ApiRequest& aRequest);

		TransferInfoPtr getTransfer(ApiRequest& aRequest) const;
		TransferInfoPtr findTransfer(const string& aToken) const noexcept;
		TransferInfo::List getTransfers() const noexcept;
		TransferInfoPtr addTransfer(const ConnectionQueueItem* aCqi, const string& aStatus) noexcept;

		void onTimer();

		void onFailed(TransferInfoPtr& aInfo, const string& aReason) noexcept;
		void starting(const Download* aDownload, const string& aStatus, bool aFullUpdate) noexcept;
		void starting(TransferInfoPtr& aInfo, const Transfer* aTransfer) noexcept;
		void onTransferCompleted(const Transfer* aTransfer, bool aIsDownload) noexcept;
		void onTick(const Transfer* aTransfer, bool aIsDownload) noexcept;
		void updateQueueInfo(TransferInfoPtr& aInfo) noexcept;

		void on(DownloadManagerListener::Tick, const DownloadList& aDownloads) noexcept override;
		void on(DownloadManagerListener::BundleTick, const BundleList& bundles, uint64_t aTick) noexcept override;

		void on(UploadManagerListener::Tick, const UploadList& aUploads) noexcept override;
		void on(UploadManagerListener::BundleTick, const UploadBundleList& bundles) noexcept override;

		void on(ConnectionManagerListener::Added, const ConnectionQueueItem* aCqi) noexcept override;
		void on(ConnectionManagerListener::Removed, const ConnectionQueueItem* aCqi) noexcept override;
		void on(ConnectionManagerListener::Failed, const ConnectionQueueItem* aCqi, const string &reason) noexcept override;
		void on(ConnectionManagerListener::Connecting, const ConnectionQueueItem* aCqi) noexcept override;
		void on(ConnectionManagerListener::UserUpdated, const ConnectionQueueItem* aCqi) noexcept override;

		void on(DownloadManagerListener::Starting, const Download* aDownload) noexcept override;
		void on(DownloadManagerListener::Complete, const Download* aDownload, bool) noexcept override;
		void on(DownloadManagerListener::Failed, const Download* aDownload, const string &reason) noexcept override;
		void on(DownloadManagerListener::Requesting, const Download* aDownload, bool hubChanged) noexcept override;

		void on(UploadManagerListener::Starting, const Upload* aUpload) noexcept override;
		void on(UploadManagerListener::Complete, const Upload* aUpload) noexcept override;


		json previousStats;

		int lastUploadBundles = 0;
		int lastDownloadBundles = 0;

		TimerPtr timer;

		mutable SharedMutex cs;
		TransferInfo::Map transfers;

		typedef ListViewController<TransferInfoPtr, TransferUtils::PROP_LAST> TransferListView;
		TransferListView view;

		void onTransferUpdated(const TransferInfoPtr& aTransfer, const PropertyIdSet& aUpdatedProperties, const string& aSubscriptionName) noexcept;
	};
}

#endif