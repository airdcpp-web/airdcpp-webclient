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

#include "stdinc.h"

#include <api/QueueApi.h>

#include <web-server/JsonUtil.h>
#include <web-server/WebServerSettings.h>

#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>

#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/transfer/download/DownloadManager.h>
#include <airdcpp/search/SearchManager.h>



namespace webserver {
#define SEGMENT_START "segment_start"
#define SEGMENT_SIZE "segment_size"

#define HOOK_FILE_FINISHED "queue_file_finished_hook"
#define HOOK_BUNDLE_FINISHED "queue_bundle_finished_hook"
#define HOOK_ADD_BUNDLE "queue_add_bundle_hook"
#define HOOK_ADD_BUNDLE_FILE "queue_add_bundle_file_hook"
#define HOOK_ADD_SOURCE "queue_add_source_hook"

	QueueApi::QueueApi(Session* aSession) : 
		HookApiModule(aSession, Access::QUEUE_VIEW, Access::QUEUE_EDIT), 
		bundleView("queue_bundle_view", this, QueueBundleUtils::propertyHandler, getBundleList), 
		fileView("queue_file_view", this, QueueFileUtils::propertyHandler, getFileList) 
	{
		createSubscriptions({
			"queue_bundle_added",
			"queue_bundle_removed",
			"queue_bundle_updated",

			// These are included in queue_bundle_updated events as well
			"queue_bundle_tick",
			"queue_bundle_content",
			"queue_bundle_priority",
			"queue_bundle_status",
			"queue_bundle_sources",

			"queue_file_added",
			"queue_file_removed",
			"queue_file_updated",

			// These are included in queue_file_updated events as well
			"queue_file_priority",
			"queue_file_status",
			"queue_file_sources",
			"queue_file_tick",
		});

		// Hooks
		HOOK_HANDLER(HOOK_FILE_FINISHED,	QueueManager::getInstance()->fileCompletionHook,	QueueApi::fileCompletionHook);
		HOOK_HANDLER(HOOK_BUNDLE_FINISHED,	QueueManager::getInstance()->bundleCompletionHook,	QueueApi::bundleCompletionHook);

		HOOK_HANDLER(HOOK_ADD_BUNDLE,		QueueManager::getInstance()->bundleValidationHook,		QueueApi::bundleAddHook);
		HOOK_HANDLER(HOOK_ADD_BUNDLE_FILE,	QueueManager::getInstance()->bundleFileValidationHook,	QueueApi::bundleFileAddHook);
		HOOK_HANDLER(HOOK_ADD_SOURCE,		QueueManager::getInstance()->sourceValidationHook,		QueueApi::sourceAddHook);

		// Methods
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
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("bundles"), TOKEN_PARAM, EXACT_PARAM("priority")),			QueueApi::handleBundlePriority);

		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("bundles"), TOKEN_PARAM, EXACT_PARAM("search")),			QueueApi::handleSearchBundleAlternates);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("bundles"), TOKEN_PARAM, EXACT_PARAM("share")),			QueueApi::handleShareBundle);

		METHOD_HANDLER(Access::QUEUE_VIEW,	METHOD_GET,		(EXACT_PARAM("files"), TTH_PARAM),										QueueApi::handleGetFilesByTTH);

		METHOD_HANDLER(Access::QUEUE_VIEW,	METHOD_GET,		(EXACT_PARAM("files"), TOKEN_PARAM),									QueueApi::handleGetFile);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("files"), TOKEN_PARAM, EXACT_PARAM("search")),				QueueApi::handleSearchFileAlternates);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("files"), TOKEN_PARAM, EXACT_PARAM("priority")),			QueueApi::handleFilePriority);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("files"), TOKEN_PARAM, EXACT_PARAM("remove")),				QueueApi::handleRemoveFile);

		METHOD_HANDLER(Access::QUEUE_VIEW,	METHOD_GET,		(EXACT_PARAM("files"), TOKEN_PARAM, EXACT_PARAM("sources")),			QueueApi::handleGetFileSources);
		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_DELETE,	(EXACT_PARAM("files"), TOKEN_PARAM, EXACT_PARAM("sources"), CID_PARAM),	QueueApi::handleRemoveFileSource);

		METHOD_HANDLER(Access::QUEUE_VIEW,	METHOD_GET,		(EXACT_PARAM("files"), TOKEN_PARAM, EXACT_PARAM("segments")),														QueueApi::handleGetFileSegments);
		// METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_POST,	(EXACT_PARAM("files"), TOKEN_PARAM, EXACT_PARAM("segments"), NUM_PARAM(SEGMENT_START), NUM_PARAM(SEGMENT_SIZE)),	QueueApi::handleAddFileSegment);
		// METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_DELETE,	(EXACT_PARAM("files"), TOKEN_PARAM, EXACT_PARAM("segments")),														QueueApi::handleResetFileSegments);

		METHOD_HANDLER(Access::QUEUE_EDIT,	METHOD_DELETE,	(EXACT_PARAM("sources"), CID_PARAM),									QueueApi::handleRemoveSource);
		METHOD_HANDLER(Access::ANY,			METHOD_POST,	(EXACT_PARAM("find_dupe_paths")),										QueueApi::handleFindDupePaths);
		METHOD_HANDLER(Access::ANY,			METHOD_POST,	(EXACT_PARAM("check_path_queued")),										QueueApi::handleIsPathQueued);

		// Listeners
		QueueManager::getInstance()->addListener(this);
		DownloadManager::getInstance()->addListener(this);
	}

	QueueApi::~QueueApi() {
		QueueManager::getInstance()->removeListener(this);
		DownloadManager::getInstance()->removeListener(this);
	}

	ActionHookResult<BundleFileAddHookResult> QueueApi::bundleFileAddHook(const string& aTarget, BundleFileAddData& aInfo, const ActionHookResultGetter<BundleFileAddHookResult>& aResultGetter) noexcept {
		return HookCompletionData::toResult<BundleFileAddHookResult>(
			maybeFireHook(HOOK_ADD_BUNDLE_FILE, WEBCFG(QUEUE_ADD_BUNDLE_FILE_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "target_directory", aTarget },
					{ "file_data", serializeBundleFileInfo(aInfo) },
				});
			}),
			aResultGetter,
			this,
			[](const json& aData, const ActionHookResultGetter<BundleFileAddHookResult>& aResultGetter) {
				if (aData.is_null()) {
					return BundleFileAddHookResult();
				}

				BundleFileAddHookResult result = {
					Deserializer::deserializePriority(aData, true),
				};

				return result;
			}
		);
	}

	ActionHookResult<BundleAddHookResult> QueueApi::bundleAddHook(const string& aTarget, BundleAddData& aData, const HintedUser& aUser, const bool aIsFile, const ActionHookResultGetter<BundleAddHookResult>& aResultGetter) noexcept {
		return HookCompletionData::toResult<BundleAddHookResult>(
			maybeFireHook(HOOK_ADD_BUNDLE, WEBCFG(QUEUE_ADD_BUNDLE_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "target_directory", aTarget },
					{ "bundle_data", {
						{ "name", aData.name },
						{ "time", aData.date },
						{ "priority", Serializer::serializePriorityId(aData.prio) },
						{ "type", aIsFile ? Serializer::serializeFileType(aData.name) : Serializer::serializeFolderType(DirectoryContentInfo::uninitialized()) },
					} },
				});
			}),
			aResultGetter,
			this,
			getBundleAddHookDeserializer(session)
		);
	}

	QueueApi::BundleAddHookResultDeserializer QueueApi::getBundleAddHookDeserializer(const Session* aSession) {
		return [aSession](const json& aData, const ActionHookResultGetter<BundleAddHookResult>& aResultGetter) {
			if (aData.is_null()) {
				return BundleAddHookResult();
			}

			BundleAddHookResult result = {
				Deserializer::deserializeTargetDirectory(aData, aSession, Util::emptyString),
				Deserializer::deserializePriority(aData, true),
			};

			return result;
		};
	}

	ActionHookResult<> QueueApi::sourceAddHook(const HintedUser& aUser, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_ADD_SOURCE, WEBCFG(QUEUE_ADD_SOURCE_HOOK_TIMEOUT).num(), [&]() {
				return json({
					{ "user", Serializer::serializeHintedUser(aUser) },
				});
			}),
			aResultGetter,
			this
		);
	}

	ActionHookResult<> QueueApi::fileCompletionHook(const QueueItemPtr& aFile, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_FILE_FINISHED, WEBCFG(QUEUE_FILE_FINISHED_HOOK_TIMEOUT).num(), [&]() {
				return Serializer::serializeItem(aFile, QueueFileUtils::propertyHandler);
			}),
			aResultGetter,
			this
		);
	}

	ActionHookResult<> QueueApi::bundleCompletionHook(const BundlePtr& aBundle, const ActionHookResultGetter<>& aResultGetter) noexcept {
		return HookCompletionData::toResult(
			maybeFireHook(HOOK_BUNDLE_FINISHED, WEBCFG(QUEUE_BUNDLE_FINISHED_HOOK_TIMEOUT).num(), [&]() {
				return Serializer::serializeItem(aBundle, QueueBundleUtils::propertyHandler);
			}),
			aResultGetter,
			this
		);
	}

	BundleList QueueApi::getBundleList() noexcept {
		BundleList bundles;
		auto qm = QueueManager::getInstance();

		RLock l(qm->getCS());
		ranges::copy(qm->getBundlesUnsafe() | views::values, back_inserter(bundles));
		return bundles;
	}

	QueueItemList QueueApi::getFileList() noexcept {
		QueueItemList items;
		auto qm = QueueManager::getInstance();

		RLock l(qm->getCS());
		ranges::copy(qm->getFileQueueUnsafe() | views::values, back_inserter(items));
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

	api_return QueueApi::handleIsPathQueued(ApiRequest& aRequest) {
		auto path = JsonUtil::getField<string>("path", aRequest.getRequestBody());
		auto b = QueueManager::getInstance()->isRealPathQueued(path);
		aRequest.setResponseBody({
			{ "bundle", !b ? JsonUtil::emptyJson : json({
				{ "id", b->getToken() },
				{ "completed", b->isCompleted() },
			}) }
		});

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleFindDupePaths(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto ret = json::array();

		auto path = JsonUtil::getOptionalField<string>("path", reqJson);
		if (path) {
			// Note: non-standard/partial paths are allowed, no strict directory path validation
			ret = QueueManager::getInstance()->getAdcDirectoryDupePaths(*path);
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
		auto bundleId = aRequest.getTokenParam();
		auto b = QueueManager::getInstance()->findBundle(bundleId);
		if (!b) {
			throw RequestException(websocketpp::http::status_code::not_found, "Bundle " + Util::toString(bundleId) + " was not found");
		}

		return b;
	}

	api_return QueueApi::handleSearchBundleAlternates(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		auto searches = QueueManager::getInstance()->searchBundleAlternates(b, false);

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
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), aRequest.getSession().get(), targetDirectory, targetFileName, prio);

		addAsyncTask([
			size = JsonUtil::getField<int64_t>("size", reqJson, false),
				tth = Deserializer::deserializeTTH(reqJson),
				hintedUser = Deserializer::deserializeHintedUser(reqJson, false, true),
				time = JsonUtil::getOptionalFieldDefault<time_t>("time", reqJson, GET_TIME()),
				complete = aRequest.defer(),
				caller = aRequest.getOwnerPtr(),
				targetDirectory,
				targetFileName,
				prio
		]{
			BundleAddInfo bundleAddInfo;
			try {
				auto options = BundleAddOptions(targetDirectory, hintedUser, caller);
				auto fileInfo = BundleFileAddData(targetFileName, tth, size, prio, time);
				bundleAddInfo = QueueManager::getInstance()->createFileBundleHooked(
					options,
					fileInfo,
					0
				);
			} catch (const Exception& e) {
				complete(websocketpp::http::status_code::bad_request, nullptr, ApiRequest::toResponseErrorStr(e.getError()));
				return;
			}

			complete(websocketpp::http::status_code::ok, Serializer::serializeBundleAddInfo(bundleAddInfo), nullptr);
			return;
		});

		return CODE_DEFERRED;
	}

	BundleFileAddData QueueApi::deserializeBundleFileInfo(const json& aJson) {
		return BundleFileAddData(
			JsonUtil::getField<string>("name", aJson),
			Deserializer::deserializeTTH(aJson),
			JsonUtil::getField<int64_t>("size", aJson),
			Deserializer::deserializePriority(aJson, true),
			JsonUtil::getOptionalFieldDefault<time_t>("time", aJson, GET_TIME())
		);
	}

	json QueueApi::serializeBundleFileInfo(const BundleFileAddData& aInfo) noexcept {
		return {
			{ "name", aInfo.name },
			{ "size", aInfo.size },
			{ "tth", aInfo.tth },
			{ "priority", Serializer::serializePriorityId(aInfo.prio) },
			{ "time", aInfo.date },
		};
	}

	api_return QueueApi::handleAddDirectoryBundle(ApiRequest& aRequest) {
		const auto& bundleJson = aRequest.getRequestBody();

		string targetDirectory, targetFileName;
		Priority prio;
		Deserializer::deserializeDownloadParams(aRequest.getRequestBody(), aRequest.getSession().get(), targetDirectory, targetFileName, prio);

		addAsyncTask([
			hintedUser = Deserializer::deserializeHintedUser(bundleJson, false, true),
			time = JsonUtil::getOptionalFieldDefault<time_t>("time", bundleJson, GET_TIME()),
			complete = aRequest.defer(),
			caller = aRequest.getOwnerPtr(),
			targetDirectory,
			targetFileName,
			prio,
			filesJson = JsonUtil::getArrayField("files", bundleJson, false)
		] {
			// Parse files
			BundleFileAddData::List files;
			try {
				for (const auto& fileJson : filesJson) {
					files.push_back(deserializeBundleFileInfo(fileJson));
				}
			} catch (const ArgumentException& e) {
				complete(websocketpp::http::status_code::bad_request, nullptr, e.toJSON());
				return;
			}

			// Queue
			string errorMsg;
			auto addInfo = BundleAddData(targetFileName, prio, time);
			auto options = BundleAddOptions(targetDirectory, hintedUser, caller);
			auto result = QueueManager::getInstance()->createDirectoryBundleHooked(
				options,
				addInfo,
				files,
				errorMsg
			);

			// Handle results
			if (!result) {
				complete(websocketpp::http::status_code::bad_request, nullptr, ApiRequest::toResponseErrorStr(errorMsg));
				return;
			}

			complete(websocketpp::http::status_code::ok, Serializer::serializeDirectoryBundleAddResult(*result, errorMsg), nullptr);
			return;
		});

		return CODE_DEFERRED;
	}

	api_return QueueApi::handleRemoveBundle(ApiRequest& aRequest) {
		auto removeFinished = JsonUtil::getOptionalFieldDefault<bool>("remove_finished", aRequest.getRequestBody(), false);

		auto b = getBundle(aRequest);
		QueueManager::getInstance()->removeBundle(b, removeFinished);
		return websocketpp::http::status_code::no_content;
	}

	api_return QueueApi::handleBundlePriority(ApiRequest& aRequest) {
		auto b = getBundle(aRequest);
		auto priority = Deserializer::deserializePriority(aRequest.getRequestBody(), true);

		QueueManager::getInstance()->setBundlePriority(b, priority);
		return websocketpp::http::status_code::no_content;
	}

	// FILES (COMMON)
	QueueItemPtr QueueApi::getFile(ApiRequest& aRequest, bool aRequireBundle) {
		auto q = QueueManager::getInstance()->findFile(aRequest.getTokenParam());
		if (!q) {
			throw RequestException(websocketpp::http::status_code::not_found, "File not found");
		}

		if (aRequireBundle && !q->getBundle()) {
			throw RequestException(websocketpp::http::status_code::bad_request, "This method may only be used for bundle files");
		}

		return q;
	}

	api_return QueueApi::handleGetFile(ApiRequest& aRequest) {
		auto qi = getFile(aRequest, false);

		auto j = Serializer::serializeItem(qi, QueueFileUtils::propertyHandler);
		aRequest.setResponseBody(j);

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleGetFilesByTTH(ApiRequest& aRequest) {
		auto tth = aRequest.getTTHParam();

		const auto files = QueueManager::getInstance()->findFiles(tth);
		aRequest.setResponseBody(Serializer::serializeItemList(QueueFileUtils::propertyHandler, files));
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleRemoveFile(ApiRequest& aRequest) {
		auto removeFinished = JsonUtil::getOptionalFieldDefault<bool>("remove_finished", aRequest.getRequestBody(), false);

		auto qi = getFile(aRequest, false);
		QueueManager::getInstance()->removeFile(qi->getTarget(), removeFinished);
		return websocketpp::http::status_code::no_content;
	}

	api_return QueueApi::handleAddFileSegment(ApiRequest& aRequest) {
		auto qi = getFile(aRequest, true);
		auto segment = parseSegment(qi, aRequest);

		QueueManager::getInstance()->addDoneSegment(qi, segment);

		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleResetFileSegments(ApiRequest& aRequest) {
		auto qi = getFile(aRequest, true);

		QueueManager::getInstance()->resetDownloadedSegments(qi);

		return websocketpp::http::status_code::ok;
	}

	Segment QueueApi::parseSegment(const QueueItemPtr& qi, ApiRequest& aRequest) {
		if (!QueueManager::getInstance()->isWaiting(qi)) {
			throw RequestException(websocketpp::http::status_code::precondition_failed, "Segments can't be modified for running files");
		}

		auto segmentStart = aRequest.getSizeParam(SEGMENT_START);
		auto segmentSize = aRequest.getSizeParam(SEGMENT_SIZE);

		Segment s(segmentStart, segmentSize);

		if (s.getSize() != qi->getSize()) {
			auto blockSize = qi->getBlockSize();

			if (s.getStart() % blockSize != 0) {
				throw RequestException(websocketpp::http::status_code::bad_request, "Segment start must be aligned by " + Util::toString(blockSize));
			}

			if (s.getEnd() != qi->getSize()) {
				if (s.getEnd() > qi->getSize()) {
					throw RequestException(websocketpp::http::status_code::bad_request, "Segment end is beyond the end of the file");
				}

				if (s.getSize() % blockSize != 0) {
					throw RequestException(websocketpp::http::status_code::bad_request, "Segment size must be aligned by " + Util::toString(blockSize));
				}
			}
		}

		return s;
	}

	api_return QueueApi::handleGetFileSegments(ApiRequest& aRequest) {
		auto qi = getFile(aRequest, false);

		vector<Segment> running, downloaded, done;
		QueueManager::getInstance()->getChunksVisualisation(qi, running, downloaded, done);

		aRequest.setResponseBody({
			{ "block_size", qi->getBlockSize() },
			{ "running", Serializer::serializeList(running, serializeSegment) },
			{ "running_progress", Serializer::serializeList(downloaded, serializeSegment) },
			{ "done", Serializer::serializeList(done, serializeSegment) },
		});
		return websocketpp::http::status_code::ok;
	}

	json QueueApi::serializeSegment(const Segment& aSegment) noexcept {
		return{
			{ "start", aSegment.getStart() },
			{ "size", aSegment.getSize() },
		};
	}

	api_return QueueApi::handleFilePriority(ApiRequest& aRequest) {
		auto qi = getFile(aRequest, true);
		auto priority = Deserializer::deserializePriority(aRequest.getRequestBody(), true);

		QueueManager::getInstance()->setQIPriority(qi, priority);
		return websocketpp::http::status_code::no_content;
	}

	api_return QueueApi::handleSearchFileAlternates(ApiRequest& aRequest) {
		auto qi = getFile(aRequest, false);
		QueueManager::getInstance()->searchFileAlternates(qi);
		return websocketpp::http::status_code::no_content;
	}

	api_return QueueApi::handleGetFileSources(ApiRequest& aRequest) {
		auto qi = getFile(aRequest, false);
		auto sources = QueueManager::getInstance()->getSources(qi);

		auto ret = json::array();
		for (const auto& s : sources) {
			ret.push_back({
				{ "user", Serializer::serializeHintedUser(s.getUser()) },
				{ "last_speed", s.getUser().user->getSpeed() },
			});
		}

		aRequest.setResponseBody(ret);
		return websocketpp::http::status_code::ok;
	}

	api_return QueueApi::handleRemoveFileSource(ApiRequest& aRequest) {
		auto qi = getFile(aRequest, false);
		auto user = Deserializer::getUser(aRequest.getCIDParam(), false);

		QueueManager::getInstance()->removeFileSource(qi, user, QueueItem::Source::FLAG_REMOVED);
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

	void QueueApi::on(QueueManagerListener::FileRecheckFailed, const QueueItemPtr&, const string&) noexcept {
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

	void QueueApi::on(QueueManagerListener::BundleDownloadStatus, const BundlePtr& aBundle) noexcept {
		// "Waiting" isn't really a status (it's just meant to clear the props for running bundles...)
		onBundleUpdated(aBundle, TICK_PROPS, "queue_bundle_tick");
	}
}