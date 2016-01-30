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

#ifndef DCPLUSPLUS_DCPP_TRANSFERAPI_H
#define DCPLUSPLUS_DCPP_TRANSFERAPI_H

#include <web-server/stdinc.h>

#include <api/ApiModule.h>
#include <api/TransferInfo.h>
#include <api/common/ListViewController.h>

#include <airdcpp/typedefs.h>

#include <airdcpp/Download.h>
#include <airdcpp/HintedUser.h>
#include <airdcpp/Transfer.h>
#include <airdcpp/Upload.h>

#include <airdcpp/ConnectionManagerListener.h>
#include <airdcpp/DownloadManagerListener.h>
#include <airdcpp/UploadManagerListener.h>

namespace webserver {
	class TransferApi : public ApiModule, private ConnectionManagerListener, private DownloadManagerListener, private UploadManagerListener {
	public:
		TransferApi(Session* aSession);
		~TransferApi();

		int getVersion() const noexcept {
			return 0;
		}

		const PropertyList properties = {
			{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
			{ PROP_TARGET, "target", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
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
			{ PROP_ENCRYPTION, "encryption", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		};

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_TARGET,
			PROP_DOWNLOAD,
			PROP_SIZE,
			PROP_STATUS,
			//PROP_STATE,
			PROP_BYTES_TRANSFERRED,
			PROP_USER,
			PROP_TIME_STARTED,
			PROP_SPEED,
			PROP_SECONDS_LEFT,
			PROP_IP,
			PROP_FLAGS,
			PROP_ENCRYPTION,
			PROP_LAST
		};
	private:
		void loadTransfers() noexcept;
		void unloadTransfers() noexcept;

		api_return handleGetStats(ApiRequest& aRequest);
		api_return handleForce(ApiRequest& aRequest);
		api_return handleDisconnect(ApiRequest& aRequest);

		TransferInfoPtr getTransfer(ApiRequest& aRequest) const;
		TransferInfoPtr getTransfer(const string& aToken) const noexcept;
		TransferInfo::List getTransfers() const noexcept;
		TransferInfoPtr addTransfer(const ConnectionQueueItem* aCqi, const string& aStatus) noexcept;

		void onTimer();

		void onFailed(TransferInfoPtr& aInfo, const string& aReason) noexcept;
		void starting(const Download* aDownload, const string& aStatus, bool aFullUpdate) noexcept;
		void starting(TransferInfoPtr& aInfo, const Transfer* aTransfer) noexcept;
		void onTransferCompleted(const Transfer* aTransfer, bool aIsDownload) noexcept;
		void onTick(const Transfer* aTransfer, bool aIsDownload) noexcept;
		void updateQueueInfo(TransferInfoPtr& aInfo) noexcept;

		void on(DownloadManagerListener::Tick, const DownloadList& aDownloads) noexcept;
		void on(DownloadManagerListener::BundleTick, const BundleList& bundles, uint64_t aTick) noexcept;

		void on(UploadManagerListener::Tick, const UploadList& aUploads) noexcept;
		void on(UploadManagerListener::BundleTick, const UploadBundleList& bundles) noexcept;

		void on(ConnectionManagerListener::Added, const ConnectionQueueItem* aCqi) noexcept;
		void on(ConnectionManagerListener::Removed, const ConnectionQueueItem* aCqi) noexcept;
		void on(ConnectionManagerListener::Failed, const ConnectionQueueItem* aCqi, const string &reason) noexcept;
		void on(ConnectionManagerListener::Connecting, const ConnectionQueueItem* aCqi) noexcept;
		void on(ConnectionManagerListener::UserUpdated, const ConnectionQueueItem* aCqi) noexcept;

		void on(DownloadManagerListener::Starting, const Download* aDownload) noexcept;
		void on(DownloadManagerListener::Complete, const Download* aDownload, bool) noexcept { onTransferCompleted(aDownload, true); }
		void on(DownloadManagerListener::Failed, const Download* aDownload, const string &reason) noexcept;
		void on(DownloadManagerListener::Requesting, const Download* aDownload, bool hubChanged) noexcept;

		void on(UploadManagerListener::Starting, const Upload* aUpload) noexcept;
		void on(UploadManagerListener::Complete, const Upload* aUpload) noexcept { onTransferCompleted(aUpload, false); }


		json previousStats;

		int lastUploadBundles = 0;
		int lastDownloadBundles = 0;

		int lastUploads = 0;
		int lastDownloads = 0;

		TimerPtr timer;

		mutable SharedMutex cs;
		TransferInfo::Map transfers;

		PropertyItemHandler<TransferInfoPtr> propertyHandler;

		typedef ListViewController<TransferInfoPtr, PROP_LAST> TransferListView;
		TransferListView view;
	};
}

#endif