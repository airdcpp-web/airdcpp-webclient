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

#ifndef DCPLUSPLUS_DCPP_TRANSFERAPI_H
#define DCPLUSPLUS_DCPP_TRANSFERAPI_H

#include <api/base/ApiModule.h>
#include <api/TransferUtils.h>

#include <api/common/ListViewController.h>

#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/transfer/TransferInfoManagerListener.h>


namespace webserver {
	class TransferApi : public SubscribableApiModule, private TransferInfoManagerListener {
	public:
		TransferApi(Session* aSession);
		~TransferApi();
	private:
		json serializeTransferStats() const noexcept;

		api_return handleGetTransfers(ApiRequest& aRequest);
		api_return handleGetTransfer(ApiRequest& aRequest);

		api_return handleGetTransferredBytes(ApiRequest& aRequest);
		api_return handleGetTransferStats(ApiRequest& aRequest);
		api_return handleForce(ApiRequest& aRequest);
		api_return handleDisconnect(ApiRequest& aRequest);

		void onTimer();

		void on(TransferInfoManagerListener::Added, const TransferInfoPtr& aInfo) noexcept override;
		void on(TransferInfoManagerListener::Updated, const TransferInfoPtr& aInfo, int aUpdatedProperties, bool aTick) noexcept override;
		void on(TransferInfoManagerListener::Removed, const TransferInfoPtr& aInfo) noexcept override;
		void on(TransferInfoManagerListener::Failed, const TransferInfoPtr& aInfo) noexcept override;
		void on(TransferInfoManagerListener::Starting, const TransferInfoPtr& aInfo) noexcept override;
		void on(TransferInfoManagerListener::Completed, const TransferInfoPtr& aInfo) noexcept override;

		json previousStats;

		TimerPtr timer;

		typedef ListViewController<TransferInfoPtr, TransferUtils::PROP_LAST> TransferListView;
		TransferListView view;

		TransferInfoPtr getTransfer(ApiRequest& aRequest) const;
		TransferInfo::List getTransfers() const noexcept;
		static PropertyIdSet updateFlagsToPropertyIds(int aUpdatedProperties) noexcept;
	};
}

#endif