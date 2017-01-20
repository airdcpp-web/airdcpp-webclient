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
			bundleView("queue_bundle_view", this, QueueBundleUtils::propertyHandler, getBundleList), fileView("queue_file_view", this, QueueFileUtils::propertyHandler, getFileList) {

		QueueManager::getInstance()->addListener(this);
		DownloadManager::getInstance()->addListener(this);

		createSubscription("queue_bundle_added");
		createSubscription("queue_bundle_removed");
		createSubscription("queue_bundle_updated");

		// These are included in queue_bundle_updated events as well
		createSubscription("queue_bundle_tick");
		createSubscription("queue_bundle_content");
		createSubscription("queue_bundle_priority");
		createSubscription("queue_bundle_status");
		createSubscription("queue_bundle_sources");

		createSubscription("queue_file_added");
		createSubscription("queue_file_removed");
		createSubscription("queue_file_updated");

		// These are included in queue_file_updated events as well
		createSubscription("queue_file_priority");
		createSubscription("queue_file_status");
		createSubscription("queue_file_sources");
		createSubscription("queue_file_tick");


		METHOD_HANDLER(Access::QUEUE_VIEW,	METHOD_GET,		(EXACT_PARAM("bundles"), RANGE_START_PARAM, RANGE_MAX_PARAM),			QueueApi::handleGetBundles);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("bundles"), EXACT_PARAM("remove_completed")),				QueueApi::handleRemoveCompletedBundles);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("bundles"), EXACT_PARAM("priority")),						QueueApi::handleBundlePriorities);

		METHOD_HANDLER(Access::DOWNLOAD,	METHOD_POST,	(EXACT_PARAM("bundles"), EXACT_PARAM("file")),							QueueApi::handleAddFileBundle);
		METHOD_HANDLER(Access::DOWNLOAD,	METHOD_POST,	(EXACT_PARAM("bundles"), EXACT_PARAM("directory")),						QueueApi::handleAddDirectoryBundle);

		METHOD_HANDLER(Access::QUEUE_VIEW,	METHOD_GET,		(EXACT_PARAM("bundles"), TOKEN_PARAM, EXACT_PARAM("files"), RANGE_START_PARAM, RANGE_MAX_PARAM),	QueueApi::handleGetBundleFiles);
		METHOD_HANDLER(Access::QUEUE_VIEW,	METHOD_GET,		(EXACT_PARAM("bundles"), TOKEN_PARAM, EXACT_PARAM("sources")),										QueueApi::handleGetBundleSources);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_DELETE,	(EXACT_PARAM("bundles"), TOKEN_PARAM, EXACT_PARAM("sources"), CID_PARAM),							QueueApi::handleRemoveBundleSource);

		METHOD_HANDLER(Access::QUEUE_VIEW,	METHOD_GET,		(EXACT_PARAM("bundles"), TOKEN_PARAM),									QueueApi::handleGetBundle);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("bundles"), TOKEN_PARAM, EXACT_PARAM("remove")),			QueueApi::handleRemoveBundle);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_PATCH,	(EXACT_PARAM("bundles"), TOKEN_PARAM),									QueueApi::handleUpdateBundle);

		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("bundles"), TOKEN_PARAM, EXACT_PARAM("search")),			QueueApi::handleSearchBundle);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("bundles"), TOKEN_PARAM, EXACT_PARAM("share")),			QueueApi::handleShareBundle);

		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("files"), TOKEN_PARAM, EXACT_PARAM("search")),				QueueApi::handleSearchFile);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_PATCH,	(EXACT_PARAM("files"), TOKEN_PARAM),									QueueApi::handleUpdateFile);

		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_DELETE,	(EXACT_PARAM("sources"), CID_PARAM),									QueueApi::handleRemoveSource);

		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("remove_file")),											QueueApi::handleRemoveTarget);
		METHOD_HANDLER(Access::ANY,			METHOD_POST,	(EXACT_PARAM("find_dupe_paths")),										QueueApi::handleFindDupePaths);
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
		auto user = Deserializer::getUser(aRequest.getCIDParam(), false);

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
		int start = aRequest.getRangeParam(START_POS);
		int count = aRequest.getRangeParam(MAX_COUNT);

		auto j = Serializer::serializeItemList(start, count, QueueBundleUtils::propertyHandler, getBundleList());

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleRemoveCompletedBundles(ApiRequest& aRequest) {
		auto removed = QueueManager::getInstance()->removeCompletedBundles();

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
		auto b = QueueManager::getInstance()->findBundle(aRequest.getTokenParam());
		if (!b) {
			throw RequestException(websocketpp::http::status_code::not_found, "Bundle not found");
		}

		return b;
	}

	QueueItemPtr QueueApi::getFile(ApiRequest& aRequest) {
		auto q = QueueManager::getInstance()->findFile(aRequest.getTokenParam());
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

		int start = aRequest.getRangeParam(START_POS);
		int count = aRequest.getRangeParam(MAX_COUNT);
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
		auto user = Deserializer::getUser(aRequest.getCIDParam(), false);

		auto removed = QueueManager::getInstance()->removeBundleSource(b, user, QueueItem::Source::FLAG_REMOVED);
		aRequest.setResponseBody({
			{ "count", removed },
		});

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleShareBundle(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		if (b->getStatus() != Bundle::STATUS_VALIDATION_ERROR) {
			aRequest.setResponseErrorStr("This action can only be performed for bundles that have failed content validation");
			return websocketpp::http::status_code::precondition_failed;
		}

		auto skipScan = JsonUtil::getOptionalFieldDefault<bool>("skip_validation", aRequest.getRequestBody(), false);
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

		aRequest.setResponseBody(Serializer::serializeItem(qi, QueueFileUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleSearchFile(ApiRequest& aRequest) {
		auto qi = getFile(aRequest);
		qi->searchAlternates();
		return websocketpp::http::status_code::ok;
	}

	// FILES (COMMON)
	api_return QueueApi::handleGetFile(ApiRequest& aRequest) {
		//auto success = QueueManager::getInstance()->findFile(aRequest.getTokenParam(TOKEN_PARAM_ID));
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

		return websocketpp::http::status_code::no_content;
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

	void QueueApi::onFileUpdated(const QueueItemPtr& aQI, const PropertyIdSet& aUpdatedProperties, const string& aSubscription) {
		fileView.onItemUpdated(aQI, aUpdatedProperties);
		if (subscriptionActive(aSubscription)) {
			// Serialize full item for more specific updates to make reading of data easier 
			// (such as cases when the script is interested only in finished files)
			send(aSubscription, Serializer::serializeItem(aQI, QueueFileUtils::propertyHandler));
		}

		if (subscriptionActive("queue_file_updated")) {
			// Serialize updated properties only
			send("queue_file_updated", Serializer::serializePartialItem(aQI, QueueFileUtils::propertyHandler, aUpdatedProperties));
		}
	}

	void QueueApi::on(QueueManagerListener::ItemSources, const QueueItemPtr& aQI) noexcept {
		onFileUpdated(aQI, { QueueFileUtils::PROP_SOURCES }, "queue_file_sources");
	}

	void QueueApi::on(QueueManagerListener::ItemStatus, const QueueItemPtr& aQI) noexcept {
		onFileUpdated(aQI, { 
			QueueFileUtils::PROP_STATUS, QueueFileUtils::PROP_TIME_FINISHED, QueueFileUtils::PROP_BYTES_DOWNLOADED, 
			QueueFileUtils::PROP_SECONDS_LEFT, QueueFileUtils::PROP_SPEED 
		}, "queue_file_status");
	}

	void QueueApi::on(QueueManagerListener::ItemPriority, const QueueItemPtr& aQI) noexcept {
		onFileUpdated(aQI, {
			QueueFileUtils::PROP_STATUS, QueueFileUtils::PROP_PRIORITY
		}, "queue_file_priority");
	}

	void QueueApi::on(QueueManagerListener::ItemTick, const QueueItemPtr& aQI) noexcept {
		onFileUpdated(aQI, {
			QueueFileUtils::PROP_STATUS, QueueFileUtils::PROP_BYTES_DOWNLOADED,
			QueueFileUtils::PROP_SECONDS_LEFT, QueueFileUtils::PROP_SPEED
		}, "queue_file_tick");
	}

	void QueueApi::on(QueueManagerListener::FileRecheckFailed, const QueueItemPtr& aQI, const string& aError) noexcept {
		//onFileUpdated(qi);
	}


	// BUNDLE LISTENERS
	void QueueApi::on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept {
		bundleView.onItemAdded(aBundle);
		if (!subscriptionActive("queue_bundle_added"))
			return;

		send("queue_bundle_added", Serializer::serializeItem(aBundle, QueueBundleUtils::propertyHandler));
	}
	void QueueApi::on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept {
		bundleView.onItemRemoved(aBundle);
		if (!subscriptionActive("queue_bundle_removed"))
			return;

		send("queue_bundle_removed", Serializer::serializeItem(aBundle, QueueBundleUtils::propertyHandler));
	}

	void QueueApi::onBundleUpdated(const BundlePtr& aBundle, const PropertyIdSet& aUpdatedProperties, const string& aSubscription) {
		bundleView.onItemUpdated(aBundle, aUpdatedProperties);
		if (subscriptionActive(aSubscription)) {
			// Serialize full item for more specific updates to make reading of data easier 
			// (such as cases when the script is interested only in finished bundles)
			send(aSubscription, Serializer::serializeItem(aBundle, QueueBundleUtils::propertyHandler));
		}

		if (subscriptionActive("queue_bundle_updated")) {
			// Serialize updated properties only
			send("queue_bundle_updated", Serializer::serializePartialItem(aBundle, QueueBundleUtils::propertyHandler, aUpdatedProperties));
		}
	}

	void QueueApi::on(QueueManagerListener::BundleSize, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { QueueBundleUtils::PROP_SIZE, QueueBundleUtils::PROP_TYPE }, "queue_bundle_content");
	}

	void QueueApi::on(QueueManagerListener::BundlePriority, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { QueueBundleUtils::PROP_PRIORITY, QueueBundleUtils::PROP_STATUS }, "queue_bundle_priority");
	}

	void QueueApi::on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { QueueBundleUtils::PROP_STATUS, QueueBundleUtils::PROP_TIME_FINISHED }, "queue_bundle_status");
	}

	void QueueApi::on(QueueManagerListener::BundleSources, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { QueueBundleUtils::PROP_SOURCES }, "queue_bundle_sources");
	}

#define TICK_PROPS { QueueBundleUtils::PROP_SECONDS_LEFT, QueueBundleUtils::PROP_SPEED, QueueBundleUtils::PROP_STATUS, QueueBundleUtils::PROP_BYTES_DOWNLOADED }
	void QueueApi::on(DownloadManagerListener::BundleTick, const BundleList& aTickBundles, uint64_t /*aTick*/) noexcept {
		for (const auto& b : aTickBundles) {
			onBundleUpdated(b, TICK_PROPS, "queue_bundle_tick");
		}
	}

	void QueueApi::on(DownloadManagerListener::BundleWaiting, const BundlePtr& aBundle) noexcept {
		// "Waiting" isn't really a status (it's just meant to clear the props for running bundles...)
		onBundleUpdated(aBundle, TICK_PROPS, "queue_bundle_tick");
	}
}