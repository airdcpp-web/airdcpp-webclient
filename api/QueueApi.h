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

#ifndef DCPLUSPLUS_DCPP_QUEUEAPI_H
#define DCPLUSPLUS_DCPP_QUEUEAPI_H

#include <airdcpp/typedefs.h>

#include <airdcpp/DownloadManagerListener.h>
#include <airdcpp/QueueManagerListener.h>

#include <api/common/ListViewController.h>
#include <api/base/HookApiModule.h>

#include <api/QueueBundleUtils.h>
#include <api/QueueFileUtils.h>

namespace dcpp {
	class Segment;

	struct BundleAddHookResult;
	struct BundleFileAddHookResult;
}

namespace webserver {
	class QueueApi : public HookApiModule, private QueueManagerListener, private DownloadManagerListener {
	public:
		QueueApi(Session* aSession);
		~QueueApi();
	private:
		ActionHookResult<> bundleCompletionHook(const BundlePtr& aBundle, const ActionHookResultGetter<>& aResultGetter) noexcept;
		ActionHookResult<> fileCompletionHook(const QueueItemPtr& aFile, const ActionHookResultGetter<>& aResultGetter) noexcept;
		ActionHookResult<BundleFileAddHookResult> bundleFileAddHook(const string& aTarget, BundleFileAddData& aInfo, const ActionHookResultGetter<BundleFileAddHookResult>& aResultGetter) noexcept;
		ActionHookResult<BundleAddHookResult> bundleAddHook(const string& aTarget, BundleAddData& aData, const HintedUser& aUser, const bool aIsFile, const ActionHookResultGetter<BundleAddHookResult>& aResultGetter) noexcept;
		ActionHookResult<> sourceAddHook(const HintedUser& aUser, const ActionHookResultGetter<>& aResultGetter) noexcept;

		typedef std::function<BundleAddHookResult(const json&, const ActionHookResultGetter<BundleAddHookResult>&)> BundleAddHookResultDeserializer;
		static BundleAddHookResultDeserializer getBundleAddHookDeserializer(const Session* aSession);

		// COMMON
		api_return handleIsPathQueued(ApiRequest& aRequest);
		api_return handleFindDupePaths(ApiRequest& aRequest);
		api_return handleRemoveSource(ApiRequest& aRequest);

		static BundleFileAddData deserializeBundleFileInfo(const json& aJson);
		static json serializeBundleFileInfo(const BundleFileAddData& aInfo) noexcept;

		// BUNDLES

		// Throws if the bundle is not found
		static BundlePtr getBundle(ApiRequest& aRequest);

		api_return handleAddDirectoryBundle(ApiRequest& aRequest);
		api_return handleAddFileBundle(ApiRequest& aRequest);
		api_return handleRemoveBundle(ApiRequest& aRequest);

		api_return handleGetBundle(ApiRequest& aRequest);
		api_return handleGetBundles(ApiRequest& aRequest);
		api_return handleGetBundleFiles(ApiRequest& aRequest);

		api_return handleRemoveCompletedBundles(ApiRequest& aRequest);
		api_return handleBundlePriorities(ApiRequest& aRequest);

		api_return handleGetBundleSources(ApiRequest& aRequest);
		api_return handleRemoveBundleSource(ApiRequest& aRequest);

		api_return handleBundlePriority(ApiRequest& aRequest);
		api_return handleSearchBundle(ApiRequest& aRequest);
		api_return handleShareBundle(ApiRequest& aRequest);

		// FILES

		// Throws if the file can't be found
		static QueueItemPtr getFile(ApiRequest& aRequest, bool aRequireBundle);

		api_return handleGetFile(ApiRequest& aRequest);
		api_return handleRemoveFile(ApiRequest& aRequest);

		api_return handleGetFileSources(ApiRequest& aRequest);
		api_return handleRemoveFileSource(ApiRequest& aRequest);

		api_return handleFilePriority(ApiRequest& aRequest);
		api_return handleSearchFile(ApiRequest& aRequest);

		api_return handleGetFileSegments(ApiRequest& aRequest);
		api_return handleAddFileSegment(ApiRequest& aRequest);
		api_return handleResetFileSegments(ApiRequest& aRequest);
		static json serializeSegment(const Segment& aSegment) noexcept;
		static Segment parseSegment(const QueueItemPtr& aQI, ApiRequest& aRequest);

		// Bundle update listeners
		void on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundleSize, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundlePriority, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::BundleSources, const BundlePtr& aBundle) noexcept override;
		void on(FileRecheckFailed, const QueueItemPtr&, const string&) noexcept override;

		void on(DownloadManagerListener::BundleTick, const BundleList& tickBundles, uint64_t aTick) noexcept override;

		// QueueItem update listeners
		void on(QueueManagerListener::BundleDownloadStatus, const BundlePtr& aBundle) noexcept override;
		void on(QueueManagerListener::ItemRemoved, const QueueItemPtr& aQI, bool /*finished*/) noexcept override;
		void on(QueueManagerListener::ItemAdded, const QueueItemPtr& aQI) noexcept override;
		void on(QueueManagerListener::ItemSources, const QueueItemPtr& aQI) noexcept override;
		void on(QueueManagerListener::ItemStatus, const QueueItemPtr& aQI) noexcept override;
		void on(QueueManagerListener::ItemPriority, const QueueItemPtr& aQI) noexcept override;
		void on(QueueManagerListener::ItemTick, const QueueItemPtr& aQI) noexcept override;

		void onFileUpdated(const QueueItemPtr& aQI, const PropertyIdSet& aUpdatedProperties, const string& aSubscription);
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