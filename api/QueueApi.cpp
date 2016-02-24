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
#include <api/QueueUtils.h>

#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>

#include <airdcpp/QueueManager.h>
#include <airdcpp/DownloadManager.h>

#include <boost/range/algorithm/copy.hpp>

namespace webserver {
	const PropertyList QueueApi::bundleProperties = {
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

	const PropertyItemHandler<BundlePtr> QueueApi::bundlePropertyHandler = {
		bundleProperties,
		QueueUtils::getStringInfo, QueueUtils::getNumericInfo, QueueUtils::compareBundles, QueueUtils::serializeBundleProperty
	};

	QueueApi::QueueApi(Session* aSession) : ApiModule(aSession, Access::QUEUE_VIEW),
			bundleView("bundle_view", this, bundlePropertyHandler, QueueUtils::getBundleList) {

		QueueManager::getInstance()->addListener(this);
		DownloadManager::getInstance()->addListener(this);

		createSubscription("bundle_added");
		createSubscription("bundle_removed");
		createSubscription("bundle_updated");
		createSubscription("bundle_status");
		createSubscription("bundle_tick");

		createSubscription("file_added");
		createSubscription("file_removed");
		createSubscription("file_updated");

		METHOD_HANDLER("bundles", Access::QUEUE_VIEW, ApiRequest::METHOD_GET, (NUM_PARAM, NUM_PARAM), false, QueueApi::handleGetBundles);
		METHOD_HANDLER("bundles", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("remove_finished")), false, QueueApi::handleRemoveFinishedBundles);
		METHOD_HANDLER("bundles", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("priority")), true, QueueApi::handleBundlePriorities);

		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("file")), true, QueueApi::handleAddFileBundle);
		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("directory")), true, QueueApi::handleAddDirectoryBundle);

		METHOD_HANDLER("bundle", Access::QUEUE_VIEW, ApiRequest::METHOD_GET, (TOKEN_PARAM), false, QueueApi::handleGetBundle);
		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("remove")), false, QueueApi::handleRemoveBundle);
		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_PATCH, (TOKEN_PARAM), true, QueueApi::handleUpdateBundle);

		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("search")), false, QueueApi::handleSearchBundle);
		METHOD_HANDLER("bundle", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("share")), false, QueueApi::handleShareBundle);

		/*METHOD_HANDLER("temp_item", ApiRequest::METHOD_POST, (), true, QueueApi::handleAddTempItem);
		METHOD_HANDLER("temp_item", ApiRequest::METHOD_GET, (TOKEN_PARAM), false, QueueApi::handleGetFile);
		METHOD_HANDLER("temp_item", ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, QueueApi::handleRemoveFile);

		METHOD_HANDLER("filelist", ApiRequest::METHOD_POST, (), true, QueueApi::handleAddFilelist);
		METHOD_HANDLER("filelist", ApiRequest::METHOD_GET, (TOKEN_PARAM), false, QueueApi::handleGetFile);
		METHOD_HANDLER("filelist", ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, QueueApi::handleRemoveFile);*/

		METHOD_HANDLER("remove_source", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (), true, QueueApi::handleRemoveSource);
		METHOD_HANDLER("remove_file", Access::QUEUE_EDIT, ApiRequest::METHOD_POST, (), true, QueueApi::handleRemoveFile);
		METHOD_HANDLER("find_dupe_paths", Access::ANY, ApiRequest::METHOD_POST, (), true, QueueApi::handleFindDupePaths);
	}

	QueueApi::~QueueApi() {
		QueueManager::getInstance()->removeListener(this);
		DownloadManager::getInstance()->removeListener(this);
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
			ret = QueueManager::getInstance()->getDirPaths(Util::toNmdcFile(*path));
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

		auto j = Serializer::serializeItemList(start, count, bundlePropertyHandler, QueueUtils::getBundleList());

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

		auto j = Serializer::serializeItem(b, bundlePropertyHandler);
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	BundlePtr QueueApi::getBundle(ApiRequest& aRequest) {
		auto b = QueueManager::getInstance()->findBundle(aRequest.getTokenParam(0));
		if (!b) {
			throw std::invalid_argument("Bundle not found");
		}

		return b;
	}

	QueueItemPtr QueueApi::getFile(ApiRequest& aRequest) {
		auto q = QueueManager::getInstance()->findFile(aRequest.getTokenParam(0));
		if (!q) {
			throw std::invalid_argument("File not found");
		}

		return q;
	}

	api_return QueueApi::handleSearchBundle(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		QueueManager::getInstance()->searchBundleAlternates(b, true);
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
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleAddFileBundle(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		string targetDirectory, targetFileName;
		TargetUtil::TargetType targetType;
		QueueItemBase::Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), targetDirectory, targetFileName, targetType, prio);

		BundlePtr b = nullptr;
		try {
			b = QueueManager::getInstance()->createFileBundle(
				targetDirectory + targetFileName,
				JsonUtil::getField<int64_t>("size", reqJson, false),
				Deserializer::deserializeTTH(reqJson),
				Deserializer::deserializeHintedUser(reqJson),
				JsonUtil::getField<time_t>("time", reqJson, false),
				0,
				prio
				);
		}
		catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::internal_server_error;
		}

		if (b) {
			json retJson = {
				{ "id", b->getToken() }
			};

			aRequest.setResponseBody(retJson);
		}

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleAddDirectoryBundle(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		BundleFileInfo::List files;
		for (const auto& fileJson : reqJson["files"]) {
			files.push_back(BundleFileInfo(
				JsonUtil::getField<string>("name", reqJson),
				Deserializer::deserializeTTH(fileJson),
				JsonUtil::getField<int64_t>("size", reqJson),
				JsonUtil::getField<time_t>("time", reqJson),
				Deserializer::deserializePriority(fileJson, true))
			);
		}

		BundlePtr b = nullptr;
		std::string errors;
		try {
			b = QueueManager::getInstance()->createDirectoryBundle(
				JsonUtil::getField<string>("target", reqJson),
				Deserializer::deserializeHintedUser(reqJson),
				files,
				Deserializer::deserializePriority(reqJson, true),
				JsonUtil::getField<time_t>("time", reqJson),
				errors
			);
		}
		catch (const QueueException& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::internal_server_error;
		}

		if (b) {
			json retJson = {
				{ "id", b->getToken() },
				{ "errors", errors }
			};

			aRequest.setResponseBody(retJson);
		}

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleRemoveBundle(ApiRequest& aRequest) {
		auto removeFinished = JsonUtil::getOptionalFieldDefault<bool>("remove_finished", aRequest.getRequestBody(), false);

		auto b = getBundle(aRequest);
		QueueManager::getInstance()->removeBundle(b, removeFinished);
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleUpdateBundle(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		const auto& reqJson = aRequest.getRequestBody();

		// Priority
		if (reqJson.find("priority") != reqJson.end()) {
			QueueManager::getInstance()->setBundlePriority(b, Deserializer::deserializePriority(reqJson, false));
		}

		return websocketpp::http::status_code::ok;
	}



	// TEMP ITEMS
	/*api_return QueueApi::handleAddTempItem(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		try {
			QueueManager::getInstance()->addOpenedItem(reqJson["filename"],
				JsonUtil::getField<int64_t>("size", reqJson),
				Deserializer::deserializeTTH(reqJson),
				Deserializer::deserializeHintedUser(reqJson),
				false
				);
		}
		catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::internal_server_error;
		}

		return websocketpp::http::status_code::ok;
	}



	// FILELISTS
	api_return QueueApi::handleAddFilelist(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto i = reqJson.find("directory");
		auto directory = i != reqJson.end() ? Util::toNmdcFile(*i) : Util::emptyString;

		auto flags = QueueItem::FLAG_PARTIAL_LIST;
		//if (j["match"])
		//	flags = QueueItem::FLAG_MATCH_QUEUE | QueueItem::FLAG_RECURSIVE_LIST;

		try {
			QueueManager::getInstance()->addList(Deserializer::deserializeHintedUser(reqJson), flags, directory);
		} catch (const Exception& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::internal_server_error;
		}

		return websocketpp::http::status_code::ok;
	}*/


	// FILES (COMMON)
	api_return QueueApi::handleGetFile(ApiRequest& aRequest) {
		//auto success = QueueManager::getInstance()->findFile(aRequest.getTokenParam(0));
		//return success ? websocketpp::http::status_code::ok : websocketpp::http::status_code::not_found;
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleRemoveFile(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("target", aRequest.getRequestBody(), false);
		if (!QueueManager::getInstance()->removeFile(path, false)) {
			aRequest.setResponseErrorStr("File not found");
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::ok;
	}



	// LISTENERS
	// All subscriber events from QueueManager should be handled asynchronously
	// This avoids deadlocks as some events are fired from inside locks
	void QueueApi::on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept {
		bundleView.onItemAdded(aBundle);
		if (!subscriptionActive("bundle_added"))
			return;

		addAsyncTask([=] {
			send("bundle_added", Serializer::serializeItem(aBundle, bundlePropertyHandler));
		});
	}
	void QueueApi::on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept {
		bundleView.onItemRemoved(aBundle);
		if (!subscriptionActive("bundle_removed"))
			return;

		addAsyncTask([=] {
			send("bundle_removed", Serializer::serializeItem(aBundle, bundlePropertyHandler));
		});
	}

	void QueueApi::on(QueueManagerListener::Removed, const QueueItemPtr& aQI, bool /*finished*/) noexcept {
		if (!subscriptionActive("file_removed"))
			return;

		//send("file_removed", QueueUtils::serializeQueueItem(aQI));
	}
	void QueueApi::on(QueueManagerListener::Added, QueueItemPtr& aQI) noexcept {
		if (!subscriptionActive("file_added"))
			return;

		//send("file_added", QueueUtils::serializeQueueItem(aQI));
	}

	void QueueApi::onFileUpdated(const QueueItemPtr& aQI) {
		if (!subscriptionActive("file_updated"))
			return;

		//send("file_updated", QueueUtils::serializeQueueItem(aQI));
	}
	void QueueApi::onBundleUpdated(const BundlePtr& aBundle, const PropertyIdSet& aUpdatedProperties, const string& aSubscription) {
		bundleView.onItemUpdated(aBundle, aUpdatedProperties);
		if (!subscriptionActive(aSubscription))
			return;

		addAsyncTask([=] {
			send(aSubscription, Serializer::serializeItem(aBundle, bundlePropertyHandler));
		});
	}

	void QueueApi::on(DownloadManagerListener::BundleTick, const BundleList& tickBundles, uint64_t /*aTick*/) noexcept {
		bundleView.onItemsUpdated(tickBundles, { PROP_SPEED, PROP_SECONDS_LEFT, PROP_BYTES_DOWNLOADED, PROP_STATUS });
		if (!subscriptionActive("bundle_tick"))
			return;

		addAsyncTask([=] {
			json j;
			for (auto& b : tickBundles) {
				j.push_back(Serializer::serializeItem(b, bundlePropertyHandler));
			}

			send("bundle_tick", j);
		});
	}

	void QueueApi::on(QueueManagerListener::BundleMoved, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { PROP_TARGET, PROP_NAME, PROP_SIZE });
	}
	void QueueApi::on(QueueManagerListener::BundleMerged, const BundlePtr& aBundle, const string&) noexcept {
		onBundleUpdated(aBundle, { PROP_TARGET, PROP_NAME, PROP_SIZE });
	}
	void QueueApi::on(QueueManagerListener::BundleSize, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { PROP_SIZE });
	}
	void QueueApi::on(QueueManagerListener::BundlePriority, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { PROP_PRIORITY, PROP_STATUS });
	}
	void QueueApi::on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { PROP_STATUS }, "bundle_status");
	}
	void QueueApi::on(QueueManagerListener::BundleSources, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { PROP_SOURCES });
	}

	void QueueApi::on(QueueManagerListener::SourcesUpdated, const QueueItemPtr& aQI) noexcept {
		onFileUpdated(aQI);
	}
	void QueueApi::on(QueueManagerListener::StatusUpdated, const QueueItemPtr& aQI) noexcept {
		onFileUpdated(aQI);
	}

	void QueueApi::on(FileRecheckFailed, const QueueItemPtr& aQI, const string& aError) noexcept {
		//onFileUpdated(qi);
	}

	void QueueApi::on(DownloadManagerListener::BundleWaiting, const BundlePtr& aBundle) noexcept {
		onBundleUpdated(aBundle, { PROP_SECONDS_LEFT, PROP_SPEED, PROP_STATUS });
	}
}