/*
* Copyright (C) 2011-2015 AirDC++ Project
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

namespace webserver {
	class QueueApi : public ApiModule, private QueueManagerListener, private DownloadManagerListener {
	public:
		const PropertyList bundleProperties = {
			{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
			{ PROP_TARGET, "target", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
			{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
			{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_STATUS, "status", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
			{ PROP_BYTES_DOWNLOADED, "downloaded_bytes", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_PRIORITY, "priority", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
			{ PROP_TIME_ADDED, "time_added", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_TIME_FINISHED, "time_finished", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_SPEED, "speed", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_SECONDS_LEFT, "seconds_left", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_SOURCES, "sources", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		};

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_TARGET,
			PROP_TYPE,
			PROP_SIZE,
			PROP_STATUS,
			PROP_BYTES_DOWNLOADED,
			PROP_PRIORITY,
			PROP_TIME_ADDED,
			PROP_TIME_FINISHED,
			PROP_SPEED,
			PROP_SECONDS_LEFT,
			PROP_SOURCES,
			PROP_LAST
		};

		/*const PropertyTypeList propertyTypes = {
			TYPE_TEXT,
			TYPE_TEXT,
			TYPE_TEXT,
			TYPE_SIZE,
			TYPE_TEXT,
			TYPE_SIZE,
			TYPE_TEXT,
			TYPE_TIME,
			TYPE_TIME,
			TYPE_SPEED,
			TYPE_TIME
		};*/

		QueueApi(Session* aSession);
		~QueueApi();

		int getVersion() const noexcept {
			return 0;
		}
	private:
		api_return handleFindDupePaths(ApiRequest& aRequest);

		api_return handleRemoveBundle(ApiRequest& aRequest);
		//api_return handleRemoveTempItem(ApiRequest& aRequest);
		//api_return handleRemoveFileList(ApiRequest& aRequest);
		api_return handleRemoveFile(ApiRequest& aRequest);

		api_return handleGetBundles(ApiRequest& aRequest);
		api_return handleRemoveFinishedBundles(ApiRequest& aRequest);
		api_return handleBundlePriorities(ApiRequest& aRequest);

		//api_return handleGetFilelist(ApiRequest& aRequest);
		//api_return handleGetTempItem(ApiRequest& aRequest);
		api_return handleGetFile(ApiRequest& aRequest);

		api_return handleGetBundle(ApiRequest& aRequest);

		api_return handleAddFilelist(ApiRequest& aRequest);
		api_return handleAddTempItem(ApiRequest& aRequest);
		api_return handleAddDirectoryBundle(ApiRequest& aRequest);
		api_return handleAddFileBundle(ApiRequest& aRequest);

		api_return handleUpdateBundle(ApiRequest& aRequest);

		api_return handleSearchBundle(ApiRequest& aRequest);
		api_return handleShareBundle(ApiRequest& aRequest);

		// Throws if the bundle is not found
		BundlePtr getBundle(ApiRequest& aRequest);
		QueueItemPtr getFile(ApiRequest& aRequest);
		//api_return handleUpdateTempItem(ApiRequest& aRequest);
		//api_return handleUpdateFileList(ApiRequest& aRequest);

		//bundle update listeners
		void on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept;
		void on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept;
		void on(QueueManagerListener::BundleMoved, const BundlePtr& aBundle) noexcept;
		void on(QueueManagerListener::BundleMerged, const BundlePtr& aBundle, const string&) noexcept;
		void on(QueueManagerListener::BundleSize, const BundlePtr& aBundle) noexcept;
		void on(QueueManagerListener::BundlePriority, const BundlePtr& aBundle) noexcept;
		void on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept;
		void on(QueueManagerListener::BundleSources, const BundlePtr& aBundle) noexcept;
		void on(FileRecheckFailed, const QueueItemPtr&, const string&) noexcept;

		void on(DownloadManagerListener::BundleTick, const BundleList& tickBundles, uint64_t aTick) noexcept;
		void on(DownloadManagerListener::BundleWaiting, const BundlePtr& aBundle) noexcept;

		//QueueItem update listeners
		void on(QueueManagerListener::Removed, const QueueItemPtr& aQI, bool /*finished*/) noexcept;
		void on(QueueManagerListener::Added, QueueItemPtr& aQI) noexcept;
		void on(QueueManagerListener::SourcesUpdated, const QueueItemPtr& aQI) noexcept;
		void on(QueueManagerListener::StatusUpdated, const QueueItemPtr& aQI) noexcept;

		void onFileUpdated(const QueueItemPtr& qi);
		void onBundleUpdated(const BundlePtr& aBundle, const PropertyIdSet& aUpdatedProperties, const string& aSubscription = "bundle_updated");

		PropertyItemHandler<BundlePtr> bundlePropertyHandler;

		typedef ListViewController<BundlePtr, PROP_LAST> BundleListView;
		BundleListView bundleView;
	};
}

#endif