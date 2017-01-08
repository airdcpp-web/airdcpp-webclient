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

#ifndef DCPLUSPLUS_DCPP_QUEUEAPI_H
#define DCPLUSPLUS_DCPP_QUEUEAPI_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>

#include <airdcpp/DownloadManagerListener.h>
#include <airdcpp/QueueManagerListener.h>

#include <api/ApiModule.h>
#include <api/common/ListViewController.h>

#include <api/QueueBundleUtils.h>
#include <api/QueueFileUtils.h>


namespace webserver {
	class QueueApi : public SubscribableApiModule, private QueueManagerListener, private DownloadManagerListener {
	public:
		QueueApi(Session* aSession);
		~QueueApi();

		int getVersion() const noexcept override {
			return 0;
		}
	private:
		api_return handleFindDupePaths(ApiRequest& aRequest);
		api_return handleRemoveSource(ApiRequest& aRequest);

		api_return handleRemoveBundle(ApiRequest& aRequest);
		api_return handleRemoveTarget(ApiRequest& aRequest);

		api_return handleGetBundles(ApiRequest& aRequest);
		api_return handleGetBundleFiles(ApiRequest& aRequest);
		api_return handleRemoveFinishedBundles(ApiRequest& aRequest);
		api_return handleBundlePriorities(ApiRequest& aRequest);

		api_return handleGetFile(ApiRequest& aRequest);
		api_return handleGetBundle(ApiRequest& aRequest);

		api_return handleAddDirectoryBundle(ApiRequest& aRequest);
		api_return handleAddFileBundle(ApiRequest& aRequest);

		api_return handleGetBundleSources(ApiRequest& aRequest);
		api_return handleRemoveBundleSource(ApiRequest& aRequest);

		api_return handleUpdateBundle(ApiRequest& aRequest);

		api_return handleSearchBundle(ApiRequest& aRequest);
		api_return handleShareBundle(ApiRequest& aRequest);

		api_return handleUpdateFile(ApiRequest& aRequest);
		api_return handleSearchFile(ApiRequest& aRequest);

		// Throws if the bundle is not found
		BundlePtr getBundle(ApiRequest& aRequest);
		QueueItemPtr getFile(ApiRequest& aRequest);

		//bundle update listeners
		void on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundleSize, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundlePriority, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundleSources, const BundlePtr& aBundle) noexcept override;
		void on(FileRecheckFailed, const QueueItemPtr&, const string&) noexcept override;

		void on(DownloadManagerListener::BundleTick, const BundleList& tickBundles, uint64_t aTick) noexcept override;
		void on(DownloadManagerListener::BundleWaiting, const BundlePtr& aBundle) noexcept override;

		//QueueItem update listeners
		void on(QueueManagerListener::ItemRemoved, const QueueItemPtr& aQI, bool /*finished*/) noexcept override;
		void on(QueueManagerListener::ItemAdded, const QueueItemPtr& aQI) noexcept override;
		void on(QueueManagerListener::ItemSourcesUpdated, const QueueItemPtr& aQI) noexcept override;
		void on(QueueManagerListener::ItemStatusUpdated, const QueueItemPtr& aQI) noexcept override;

		void onFileUpdated(const QueueItemPtr& aQI, const PropertyIdSet& aUpdatedProperties);
		void onBundleUpdated(const BundlePtr& aBundle, const PropertyIdSet& aUpdatedProperties, const string& aSubscription);

		typedef ListViewController<BundlePtr, QueueBundleUtils::PROP_LAST> BundleListView;
		BundleListView bundleView;

		typedef ListViewController<QueueItemPtr, QueueFileUtils::PROP_LAST> FileListView;
		FileListView fileView;

		static BundleList getBundleList() noexcept;
		static QueueItemList getFileList() noexcept;
	};
}

#endif