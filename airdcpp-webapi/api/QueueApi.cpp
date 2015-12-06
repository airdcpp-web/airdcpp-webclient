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
	QueueApi::QueueApi(Session* aSession) : ApiModule(aSession),
			bundlePropertyHandler(bundleProperties, 
				QueueUtils::getStringInfo, QueueUtils::getNumericInfo, QueueUtils::compareBundles, QueueUtils::serializeBundleProperty),
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

		METHOD_HANDLER("bundles", ApiRequest::METHOD_GET, (NUM_PARAM, NUM_PARAM), false, QueueApi::handleGetBundles);
		METHOD_HANDLER("bundles", ApiRequest::METHOD_POST, (EXACT_PARAM("remove_finished")), false, QueueApi::handleRemoveFinishedBundles);
		METHOD_HANDLER("bundles", ApiRequest::METHOD_POST, (EXACT_PARAM("priority")), true, QueueApi::handleBundlePriorities);

		METHOD_HANDLER("bundle", ApiRequest::METHOD_POST, (EXACT_PARAM("file")), true, QueueApi::handleAddFileBundle);
		METHOD_HANDLER("bundle", ApiRequest::METHOD_POST, (EXACT_PARAM("directory")), true, QueueApi::handleAddDirectoryBundle);
		METHOD_HANDLER("bundle", ApiRequest::METHOD_GET, (TOKEN_PARAM), false, QueueApi::handleGetBundle);
		METHOD_HANDLER("bundle", ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, QueueApi::handleRemoveBundle);
		METHOD_HANDLER("bundle", ApiRequest::METHOD_PATCH, (TOKEN_PARAM), true, QueueApi::handleUpdateBundle);

		METHOD_HANDLER("bundle", ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("search")), false, QueueApi::handleSearchBundle);

		METHOD_HANDLER("temp_item", ApiRequest::METHOD_POST, (), true, QueueApi::handleAddTempItem);
		METHOD_HANDLER("temp_item", ApiRequest::METHOD_GET, (TOKEN_PARAM), false, QueueApi::handleGetFile);
		METHOD_HANDLER("temp_item", ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, QueueApi::handleRemoveFile);

		METHOD_HANDLER("filelist", ApiRequest::METHOD_POST, (), true, QueueApi::handleAddFilelist);
		METHOD_HANDLER("filelist", ApiRequest::METHOD_GET, (TOKEN_PARAM), false, QueueApi::handleGetFile);
		METHOD_HANDLER("filelist", ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, QueueApi::handleRemoveFile);

		METHOD_HANDLER("find_dupe_paths", ApiRequest::METHOD_POST, (), true, QueueApi::handleFindDupePaths);
	}

	QueueApi::~QueueApi() {
		QueueManager::getInstance()->removeListener(this);
		DownloadManager::getInstance()->removeListener(this);
	}

	api_return QueueApi::handleFindDupePaths(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		json ret;

		StringList paths;
		auto path = JsonUtil::getOptionalField<string>("path", reqJson, false, false);
		if (path) {
			paths = QueueManager::getInstance()->getDirPaths(Util::toNmdcFile(*path));
		} else {
			auto tth = Deserializer::deserializeTTH(reqJson);
			paths = QueueManager::getInstance()->getTargets(tth);
		}

		if (!paths.empty()) {
			for (const auto& p : paths) {
				ret.push_back(p);
			}
		} else {
			ret = json::array();
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
		auto b = QueueManager::getInstance()->findBundle(aRequest.getRangeParam(0));
		if (b) {
			auto j = Serializer::serializeItem(b, bundlePropertyHandler);
			aRequest.setResponseBody(j);
			return websocketpp::http::status_code::ok;
		}

		return websocketpp::http::status_code::not_found;
	}

	api_return QueueApi::handleSearchBundle(ApiRequest& aRequest) {
		auto b = QueueManager::getInstance()->findBundle(aRequest.getTokenParam(0));
		if (b) {
			QueueManager::getInstance()->searchBundleAlternates(b, true);
			return  websocketpp::http::status_code::ok;
		}

		return websocketpp::http::status_code::not_found;
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
		auto removeFinished = JsonUtil::getOptionalField<bool>("remove_finished", aRequest.getRequestBody());
		auto success = QueueManager::getInstance()->removeBundle(aRequest.getTokenParam(0), removeFinished ? *removeFinished : false);
		return success ? websocketpp::http::status_code::ok : websocketpp::http::status_code::not_found;
	}

	api_return QueueApi::handleUpdateBundle(ApiRequest& aRequest) {
		auto b = QueueManager::getInstance()->findBundle(aRequest.getTokenParam(0));
		if (!b) {
			return websocketpp::http::status_code::not_found;
		}

		const auto& reqJson = aRequest.getRequestBody();

		// Priority
		if (reqJson.find("priority") != reqJson.end()) {
			QueueManager::getInstance()->setBundlePriority(b, Deserializer::deserializePriority(reqJson, false));
		}

		// Target
		//auto target = reqJson.find("target");
		//if (target != reqJson.end()) {
		//	QueueManager::getInstance()->moveBundle(b, *target, true);
		//}

		return websocketpp::http::status_code::ok;
	}



	// TEMP ITEMS
	api_return QueueApi::handleAddTempItem(ApiRequest& aRequest) {
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
	}


	// FILES (COMMON)
	api_return QueueApi::handleGetFile(ApiRequest& aRequest) {
		auto success = QueueManager::getInstance()->findFile(aRequest.getTokenParam(0));
		return success ? websocketpp::http::status_code::ok : websocketpp::http::status_code::not_found;
	}

	api_return QueueApi::handleRemoveFile(ApiRequest& aRequest) {
		auto success = QueueManager::getInstance()->removeFile(aRequest.getTokenParam(0), false);
		return success ? websocketpp::http::status_code::ok : websocketpp::http::status_code::not_found;
	}



	// LISTENERS
	// All listener events from QueueManager should be handled asynchronously
	// This avoids deadlocks as some events are fired from inside locks
	void QueueApi::on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept {
		addAsyncSubscriptionTask([=] {
			bundleView.onItemAdded(aBundle);
			if (!subscriptionActive("bundle_added"))
				return;

			send("bundle_added", Serializer::serializeItem(aBundle, bundlePropertyHandler));
		});
	}
	void QueueApi::on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept {
		addAsyncSubscriptionTask([=] {
			bundleView.onItemRemoved(aBundle);
			if (!subscriptionActive("bundle_removed"))
				return;

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
		addAsyncSubscriptionTask([=] {
			bundleView.onItemUpdated(aBundle, aUpdatedProperties);

			if (!subscriptionActive(aSubscription))
				return;

			send(aSubscription, Serializer::serializeItem(aBundle, bundlePropertyHandler));
		});
	}

	void QueueApi::on(DownloadManagerListener::BundleTick, const BundleList& tickBundles, uint64_t /*aTick*/) noexcept {
		addAsyncSubscriptionTask([=] {
			bundleView.onItemsUpdated(tickBundles, { PROP_SPEED, PROP_SECONDS_LEFT, PROP_BYTES_DOWNLOADED, PROP_STATUS });
			if (!subscriptionActive("bundle_tick"))
				return;

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

	void QueueApi::on(DownloadManagerListener::BundleWaiting, const BundlePtr aBundle) noexcept {
		onBundleUpdated(aBundle, { PROP_SECONDS_LEFT, PROP_SPEED, PROP_STATUS });
	}
}