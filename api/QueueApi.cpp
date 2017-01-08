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
#include <web-server/JsonUtil.h>

#include <api/QueueApi.h>

#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>

#include <airdcpp/QueueManager.h>
#include <airdcpp/DownloadManager.h>

#include <boost/range/algorithm/copy.hpp>

namespace webserver {
	QueueApi::QueueApi(Session* aSession) : SubscribableApiModule(aSession, Access::QUEUE_VIEW),
			bundleView("bundle_view", this, QueueBundleUtils::propertyHandler, getBundleList), fileView("queue_file_view", this, QueueFileUtils::propertyHandler, getFileList) {

		QueueManager::getInstance()->addListener(this);
		DownloadManager::getInstance()->addListener(this);

		createSubscription("bundle_added");
		createSubscription("bundle_removed");
		createSubscription("bundle_updated");

		// These are included in bundle_updated events as well
		createSubscription("bundle_tick");
		createSubscription("bundle_content");
		createSubscription("bundle_priority");
		createSubscription("bundle_status");
		createSubscription("bundle_sources");

		createSubscription("queue_file_added");
		createSubscription("queue_file_removed");
		createSubscription("queue_file_updated");

		METHOD_HANDLER("bundles", Access::QUEUE_VIEW, ApiRequest::METHOD_GET, (NUM_PARAM, NUM_PARAM), false, QueueApi::handleGetBundles);
		METHOD_HANDLER("bundles", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("remove_finished")), false, QueueApi::handleRemoveFinishedBundles);
		METHOD_HANDLER("bundles", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("priority")), true, QueueApi::handleBundlePriorities);

		METHOD_HANDLER("bundle", Access::DOWNLOAD, ApiRequest::METHOD_POST, (EXACT_PARAM("file")), true, QueueApi::handleAddFileBundle);
		METHOD_HANDLER("bundle", Access::DOWNLOAD, ApiRequest::METHOD_POST, (EXACT_PARAM("directory")), true, QueueApi::handleAddDirectoryBundle);

		METHOD_HANDLER("bundle", Access::QUEUE_VIEW, ApiRequest::METHOD_GET, (TOKEN_PARAM, EXACT_PARAM("files"), NUM_PARAM, NUM_PARAM), false, QueueApi::handleGetBundleFiles);
		METHOD_HANDLER("bundle", Access::QUEUE_VIEW, ApiRequest::METHOD_GET, (TOKEN_PARAM, EXACT_PARAM("sources")), false, QueueApi::handleGetBundleSources);
		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_DELETE, (TOKEN_PARAM, EXACT_PARAM("source"), CID_PARAM), false, QueueApi::handleRemoveBundleSource);

		METHOD_HANDLER("bundle", Access::QUEUE_VIEW, ApiRequest::METHOD_GET, (TOKEN_PARAM), false, QueueApi::handleGetBundle);
		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("remove")), false, QueueApi::handleRemoveBundle);
		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_PATCH, (TOKEN_PARAM), true, QueueApi::handleUpdateBundle);

		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("search")), false, QueueApi::handleSearchBundle);
		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("share")), false, QueueApi::handleShareBundle);

		METHOD_HANDLER("file", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("search")), false, QueueApi::handleSearchFile);
		METHOD_HANDLER("file", Access::QUEUE_EDIT, ApiRequest::METHOD_PATCH, (TOKEN_PARAM), true, QueueApi::handleUpdateFile);

		METHOD_HANDLER("remove_source", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (), true, QueueApi::handleRemoveSource);
		METHOD_HANDLER("remove_file", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (), true, QueueApi::handleRemoveTarget);
		METHOD_HANDLER("find_dupe_paths", Access::ANY, ApiRequest::METHOD_POST, (), true, QueueApi::handleFindDupePaths);
	}

	QueueApi::~QueueApi() {
		QueueManager::getInstance()->removeListener(this);
		DownloadManager::getInstance()->removeListener(this);
	}

	BundleList QueueApi::getBundleList() noexcept {
		BundleList bundles;
		auto qm = QueueManager::getInstance();

		RLock l(qm->getCS());
		boost::range::copy(qm->getBundles() | map_values, back_inserter(bundles));
		return bundles;
	}

	QueueItemList QueueApi::getFileList() noexcept {
		QueueItemList items;
		auto qm = QueueManager::getInstance();

		RLock l(qm->getCS());
		boost::range::copy(qm->getFileQueue() | map_values, back_inserter(items));
		return items;
	}

	api_return QueueApi::handleRemoveSource(ApiRequest& aRequest) {
		auto user = Deserializer::deserializeUser(aRequest.getRequestBody());

		auto removed = QueueManager::getInstance()->removeSource(user, QueueItem::Source::FLAG_REMOVED);
		aRequest.setResponseBody({
			{ "count", removed }
		});

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleFindDupePaths(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto ret = json::array();

		auto path = JsonUtil::getOptionalField<string>("path", reqJson, false);
		if (path) {
			ret = QueueManager::getInstance()->getNmdcDirPaths(Util::toNmdcFile(*path));
		} else {
			auto tth = Deserializer::deserializeTTH(reqJson);
			ret = QueueManager::getInstance()->getTargets(tth);
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	// BUNDLES
	api_return QueueApi::handleGetBundles(ApiRequest& aRequest)  {
		int start = aRequest.getRangeParam(0);
		int count = aRequest.getRangeParam(1);

		auto j = Serializer::serializeItemList(start, count, QueueBundleUtils::propertyHandler, getBundleList());

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleRemoveFinishedBundles(ApiRequest& aRequest) {
		auto removed = QueueManager::getInstance()->removeFinishedBundles();

		aRequest.setResponseBody({
			{ "count", removed }
		});
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleBundlePriorities(ApiRequest& aRequest) {
		auto priority = Deserializer::deserializePriority(aRequest.getRequestBody(), true);
		QueueManager::getInstance()->setPriority(priority);

		return websocketpp::http::status_code::no_content;
	}

	api_return QueueApi::handleGetBundle(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);

		auto j = Serializer::serializeItem(b, QueueBundleUtils::propertyHandler);
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	BundlePtr QueueApi::getBundle(ApiRequest& aRequest) {
		auto b = QueueManager::getInstance()->findBundle(aRequest.getTokenParam(0));
		if (!b) {
			throw RequestException(websocketpp::http::status_code::not_found, "Bundle not found");
		}

		return b;
	}

	QueueItemPtr QueueApi::getFile(ApiRequest& aRequest) {
		auto q = QueueManager::getInstance()->findFile(aRequest.getTokenParam(0));
		if (!q) {
			throw RequestException(websocketpp::http::status_code::not_found, "File not found");
		}

		return q;
	}

	api_return QueueApi::handleSearchBundle(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		auto searches = QueueManager::getInstance()->searchBundleAlternates(b, true);

		if (searches == 0) {
			aRequest.setResponseErrorStr("No files to search for");
			return websocketpp::http::status_code::bad_request;
		}

		aRequest.setResponseBody({
			{ "sent", searches },
		});

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleGetBundleFiles(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		QueueItemList files;

		{
			RLock l(QueueManager::getInstance()->getCS());
			files = b->getQueueItems();
		}

		int start = aRequest.getRangeParam(2);
		int count = aRequest.getRangeParam(3);
		auto j = Serializer::serializeItemList(start, count, QueueFileUtils::propertyHandler, files);

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleGetBundleSources(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		auto sources = QueueManager::getInstance()->getBundleSources(b);

		auto ret = json::array();
		for (const auto& s : sources) {
			ret.push_back({
				{ "user", Serializer::serializeHintedUser(s.getUser()) },
				{ "last_speed", s.getUser().user->getSpeed() },
				{ "files", s.files },
				{ "size", s.size },
			});
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleRemoveBundleSource(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		auto user = Deserializer::getUser(aRequest.getStringParam(2), false);

		auto removed = QueueManager::getInstance()->removeBundleSource(b, user, QueueItem::Source::FLAG_REMOVED);
		aRequest.setResponseBody({
			{ "count", removed },
		});

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleShareBundle(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		if (!b->isFinished()) {
			aRequest.setResponseErrorStr("This action can only be performed for finished bundles");
			return websocketpp::http::status_code::bad_request;
		}

		auto skipScan = JsonUtil::getOptionalFieldDefault<bool>("skip_scan", aRequest.getRequestBody(), false);
		QueueManager::getInstance()->shareBundle(b, skipScan);
		return websocketpp::http::status_code::no_content;
	}

	api_return QueueApi::handleAddFileBundle(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		string targetDirectory, targetFileName;
		Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), aRequest.getSession(), targetDirectory, targetFileName, prio);

		BundleAddInfo bundleAddInfo;
		try {
			bundleAddInfo = QueueManager::getInstance()->createFileBundle(
				targetDirectory + targetFileName,
				JsonUtil::getField<int64_t>("size", reqJson, false),
				Deserializer::deserializeTTH(reqJson),
				Deserializer::deserializeHintedUser(reqJson),
				JsonUtil::getOptionalFieldDefault<time_t>("time", reqJson, GET_TIME()),
				0,
				prio
			);
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::bad_request;
		}

		aRequest.setResponseBody(Serializer::serializeBundleAddInfo(bundleAddInfo));
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleAddDirectoryBundle(ApiRequest& aRequest) {
		const auto& bundleJson = aRequest.getRequestBody();

		BundleDirectoryItemInfo::List files;
		for (const auto& fileJson : JsonUtil::getRawField("files", bundleJson)) {
			files.push_back(BundleDirectoryItemInfo(
				JsonUtil::getField<string>("name", fileJson),
				Deserializer::deserializeTTH(fileJson),
				JsonUtil::getField<int64_t>("size", fileJson),
				JsonUtil::getField<time_t>("time", fileJson),
				Deserializer::deserializePriority(fileJson, true))
			);
		}

		if (files.empty()) {
			JsonUtil::throwError("files", JsonUtil::ERROR_INVALID, "No files were supplied");
		}

		string targetDirectory, targetFileName;
		Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), aRequest.getSession(), targetDirectory, targetFileName, prio);

		string errorMsg;
		auto info = QueueManager::getInstance()->createDirectoryBundle(
			targetDirectory + targetFileName,
			Deserializer::deserializeHintedUser(bundleJson),
			files,
			prio,
			JsonUtil::getOptionalFieldDefault<time_t>("time", bundleJson, GET_TIME()),
			errorMsg
		);

		if (!info) {
			aRequest.setResponseErrorStr(errorMsg);
			return websocketpp::http::status_code::bad_request;
		}

		aRequest.setResponseBody(Serializer::serializeDirectoryBundleAddInfo(*info, errorMsg));
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleRemoveBundle(ApiRequest& aRequest) {
		auto removeFinished = JsonUtil::getOptionalFieldDefault<bool>("remove_finished", aRequest.getRequestBody(), false);

		auto b = getBundle(aRequest);
		QueueManager::getInstance()->removeBundle(b, removeFinished);
		return websocketpp::http::status_code::no_content;
	}

	api_return QueueApi::handleUpdateBundle(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		const auto& reqJson = aRequest.getRequestBody();

		// Priority
		if (reqJson.find("priority") != reqJson.end()) {
			QueueManager::getInstance()->setBundlePriority(b, Deserializer::deserializePriority(reqJson, false));
		}

		if (reqJson.find("auto_priority") != reqJson.end()) {
			auto autoPrio = JsonUtil::getField<bool>("auto_priority", reqJson);
			if (autoPrio != b->getAutoPriority()) {
				QueueManager::getInstance()->toggleBundleAutoPriority(b);
			}
		}

		aRequest.setResponseBody(Serializer::serializeItem(b, QueueBundleUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleUpdateFile(ApiRequest& aRequest) {
		auto qi = getFile(aRequest);
		const auto& reqJson = aRequest.getRequestBody();

		// Priority
		if (reqJson.find("priority") != reqJson.end()) {
			QueueManager::getInstance()->setQIPriority(qi, Deserializer::deserializePriority(reqJson, false));
		}

		if (reqJson.find("auto_priority") != reqJson.end()) {
			auto autoPrio = JsonUtil::getField<bool>("auto_priority", reqJson);
			if (autoPrio != qi->getAutoPriority()) {
				QueueManager::getInstance()->setQIAutoPriority(qi->getTarget());
			}
		}

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleSearchFile(ApiRequest& aRequest) {
		auto qi = getFile(aRequest);
		qi->searchAlternates();
		return websocketpp::http::status_code::ok;
	}

	// FILES (COMMON)
	api_return QueueApi::handleGetFile(ApiRequest& aRequest) {
		//auto success = QueueManager::getInstance()->findFile(aRequest.getTokenParam(0));
		//return success ? websocketpp::http::status_code::ok : websocketpp::http::status_code::not_found;
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleRemoveTarget(ApiRequest& aRequest) {
		auto removeFinished = JsonUtil::getOptionalFieldDefault<bool>("remove_finished", aRequest.getRequestBody(), false);
		auto path = JsonUtil::getField<string>("target", aRequest.getRequestBody(), false);
		if (!QueueManager::getInstance()->removeFile(path, removeFinished)) {
			aRequest.setResponseErrorStr("File not found");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
	}


	// FILE LISTENERS
	void QueueApi::on(QueueManagerListener::ItemAdded, const QueueItemPtr& aQI) noexcept {
		fileView.onItemAdded(aQI);
		if (!subscriptionActive("queue_file_added"))
			return;

		send("queue_file_added", Serializer::serializeItem(aQI, QueueFileUtils::propertyHandler));
	}

	void QueueApi::on(QueueManagerListener::ItemRemoved, const QueueItemPtr& aQI, bool /*finished*/) noexcept {
		fileView.onItemRemoved(aQI);
		if (!subscriptionActive("queue_file_removed"))
			return;

		send("queue_file_removed", Serializer::serializeItem(aQI, QueueFileUtils::propertyHandler));
	}

	void QueueApi::onFileUpdated(const QueueItemPtr& aQI, const PropertyIdSet& aUpdatedProperties) {
		fileView.onItemUpdated(aQI, aUpdatedProperties);
		if (!subscriptionActive("queue_file_updated"))
			return;

		send("queue_file_updated", Serializer::serializeItem(aQI, QueueFileUtils::propertyHandler));
	}

	void QueueApi::on(QueueManagerListener::ItemSourcesUpdated, const QueueItemPtr& aQI) noexcept {
		onFileUpdated(aQI, { QueueFileUtils::PROP_SOURCES });
	}

	void QueueApi::on(QueueManagerListener::ItemStatusUpdated, const QueueItemPtr& aQI) noexcept {
		onFileUpdated(aQI, { 
			QueueFileUtils::PROP_STATUS, QueueFileUtils::PROP_TIME_FINISHED, QueueFileUtils::PROP_BYTES_DOWNLOADED, 
			QueueFileUtils::PROP_SECONDS_LEFT, QueueFileUtils::PROP_SPEED, QueueFileUtils::PROP_PRIORITY 
		});
	}

	void QueueApi::on(QueueManagerListener::FileRecheckFailed, const QueueItemPtr& aQI, const string& aError) noexcept {
		//onFileUpdated(qi);
	}


	// BUNDLE LISTENERS
	void QueueApi::on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept {
		bundleView.onItemAdded(aBundle);
		if (!subscriptionActive("bundle_added"))
			return;

		send("bundle_added", Serializer::serializeItem(aBundle, QueueBundleUtils::propertyHandler));
	}
	void QueueApi::on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept {
		bundleView.onItemRemoved(aBundle);
		if (!subscriptionActive("bundle_removed"))
			return;

		send("bundle_removed", Serializer::serializeItem(aBundle, QueueBundleUtils::propertyHandler));
	}

	void QueueApi::onBundleUpdated(const BundlePtr& aBundle, const PropertyIdSet& aUpdatedProperties, const string& aSubscription) {
		bundleView.onItemUpdated(aBundle, aUpdatedProperties);
		if (subscriptionActive(aSubscription)) {
			send(aSubscription, Serializer::serializeItemProperties(aBundle, aUpdatedProperties, QueueBundleUtils::propertyHandler));
		}

		if (subscriptionActive("bundle_updated")) {
			send("bundle_updated", Serializer::serializeItemProperties(aBundle, aUpdatedProperties, QueueBundleUtils::propertyHandler));
		}
	}

	void QueueApi::on(QueueManagerListener::BundleSize, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { QueueBundleUtils::PROP_SIZE, QueueBundleUtils::PROP_TYPE }, "bundle_content");
	}

	void QueueApi::on(QueueManagerListener::BundlePriority, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { QueueBundleUtils::PROP_PRIORITY, QueueBundleUtils::PROP_STATUS }, "bundle_priority");
	}

	void QueueApi::on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { QueueBundleUtils::PROP_STATUS, QueueBundleUtils::PROP_TIME_FINISHED }, "bundle_status");
	}

	void QueueApi::on(QueueManagerListener::BundleSources, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { QueueBundleUtils::PROP_SOURCES }, "bundle_sources");
	}

#define TICK_PROPS { QueueBundleUtils::PROP_SECONDS_LEFT, QueueBundleUtils::PROP_SPEED, QueueBundleUtils::PROP_STATUS, QueueBundleUtils::PROP_BYTES_DOWNLOADED }
	void QueueApi::on(DownloadManagerListener::BundleTick, const BundleList& aTickBundles, uint64_t /*aTick*/) noexcept {
		for (const auto& b : aTickBundles) {
			onBundleUpdated(b, TICK_PROPS, "bundle_tick");
		}
	}

	void QueueApi::on(DownloadManagerListener::BundleWaiting, const BundlePtr& aBundle) noexcept {
		// "Waiting" isn't really a status (it's just meant to clear the props for running bundles...)
		onBundleUpdated(aBundle, TICK_PROPS, "bundle_tick");
	}
}