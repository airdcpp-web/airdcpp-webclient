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

#ifndef DCPLUSPLUS_DCPP_TRANSFERINFOMANAGER_H
#define DCPLUSPLUS_DCPP_TRANSFERINFOMANAGER_H


#include "TransferInfo.h"

#include "typedefs.h"

#include "Transfer.h"
#include "Singleton.h"
#include "Speaker.h"

#include "ConnectionManagerListener.h"
#include "DownloadManagerListener.h"
#include "UploadManagerListener.h"


namespace dcpp {
	class TransferInfoManagerListener {
	public:
		virtual ~TransferInfoManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> Added;
		typedef X<1> Updated;
		typedef X<2> Removed;
		typedef X<3> Failed;
		typedef X<4> Starting;
		typedef X<5> Completed;
		typedef X<6> Tick;

		virtual void on(Added, const TransferInfoPtr&) noexcept { }
		virtual void on(Updated, const TransferInfoPtr&, int, bool) noexcept {}
		virtual void on(Removed, const TransferInfoPtr&) noexcept { }
		virtual void on(Failed, const TransferInfoPtr&) noexcept { }
		virtual void on(Starting, const TransferInfoPtr&) noexcept { }
		virtual void on(Completed, const TransferInfoPtr&) noexcept { }
		virtual void on(Tick, const TransferInfo::List&, int) noexcept { }
	};


	class TransferInfoManager : public Singleton<TransferInfoManager>, public Speaker<TransferInfoManagerListener>, private ConnectionManagerListener, private DownloadManagerListener, private UploadManagerListener {
	public:
		TransferInfoManager();
		~TransferInfoManager();

		TransferInfo::List getTransfers() const noexcept;
		TransferInfoPtr findTransfer(const string& aToken) const noexcept;
		TransferInfoPtr findTransfer(TransferInfoToken aToken) const noexcept;
	private:
		TransferInfoPtr addTransfer(const ConnectionQueueItem* aCqi, const string& aStatus) noexcept;

		void onFailed(TransferInfoPtr& aInfo, const string& aReason) noexcept;
		void starting(const Download* aDownload, const string& aStatus, bool aFullUpdate) noexcept;
		void starting(TransferInfoPtr& aInfo, const Transfer* aTransfer) noexcept;
		void onTransferCompleted(const Transfer* aTransfer, bool aIsDownload) noexcept;
		TransferInfoPtr onTick(const Transfer* aTransfer, bool aIsDownload) noexcept;
		void updateQueueInfo(TransferInfoPtr& aInfo) noexcept;

		void on(DownloadManagerListener::Tick, const DownloadList& aDownloads, uint64_t) noexcept override;
		void on(UploadManagerListener::Tick, const UploadList& aUploads) noexcept override;

		void on(ConnectionManagerListener::Added, const ConnectionQueueItem* aCqi) noexcept override;
		void on(ConnectionManagerListener::Removed, const ConnectionQueueItem* aCqi) noexcept override;
		void on(ConnectionManagerListener::Failed, const ConnectionQueueItem* aCqi, const string& reason) noexcept override;
		void on(ConnectionManagerListener::Connecting, const ConnectionQueueItem* aCqi) noexcept override;
		void on(ConnectionManagerListener::Forced, const ConnectionQueueItem* aCqi) noexcept override;
		void on(ConnectionManagerListener::UserUpdated, const ConnectionQueueItem* aCqi) noexcept override;

		void on(DownloadManagerListener::Starting, const Download* aDownload) noexcept override;
		void on(DownloadManagerListener::Complete, const Download* aDownload, bool) noexcept override;
		void on(DownloadManagerListener::Failed, const Download* aDownload, const string& reason) noexcept override;
		void on(DownloadManagerListener::Requesting, const Download* aDownload, bool hubChanged) noexcept override;
		void on(DownloadManagerListener::Idle, const UserConnection* aConn, const string& aError) noexcept override;

		void on(UploadManagerListener::Starting, const Upload* aUpload) noexcept override;
		void on(UploadManagerListener::Complete, const Upload* aUpload) noexcept override;

		mutable SharedMutex cs;
		TransferInfo::Map transfers;

		void onTransferUpdated(const TransferInfoPtr& aTransfer, int aUpdatedProperties, bool aTick = false) noexcept;
	};
}

#endif