/*
* Copyright (C) 2011-2022 AirDC++ Project
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

#include "stdinc.h"

#include <airdcpp/SettingsManager.h>

#include <web-server/JsonUtil.h>
#include <web-server/Timer.h>

#include <api/HashApi.h>

#include <api/common/Serializer.h>

namespace webserver {
	HashApi::HashApi(Session* aSession) : 
		SubscribableApiModule(
			aSession, 
			Access::SETTINGS_VIEW, 
			{ 
				"hash_database_status", 
				"hash_statistics", 
				"hasher_directory_finished", 
				"hasher_finished",
			}
		),
		timer(getTimer([this] { onTimer(); }, 1000)) 
	{
		HashManager::getInstance()->addListener(this);

		METHOD_HANDLER(Access::SETTINGS_VIEW, METHOD_GET,	(EXACT_PARAM("database_status")),	HashApi::handleGetDbStatus);
		METHOD_HANDLER(Access::SETTINGS_EDIT, METHOD_POST,	(EXACT_PARAM("optimize_database")),	HashApi::handleOptimize);

		METHOD_HANDLER(Access::SETTINGS_VIEW, METHOD_GET,	(EXACT_PARAM("stats")),				HashApi::handleGetStats);

		METHOD_HANDLER(Access::SETTINGS_EDIT, METHOD_POST,	(EXACT_PARAM("pause")),				HashApi::handlePause);
		METHOD_HANDLER(Access::SETTINGS_EDIT, METHOD_POST,	(EXACT_PARAM("resume")),			HashApi::handleResume);
		METHOD_HANDLER(Access::SETTINGS_EDIT, METHOD_POST,	(EXACT_PARAM("stop")),				HashApi::handleStop);

		METHOD_HANDLER(Access::SETTINGS_EDIT, METHOD_POST,	(EXACT_PARAM("rename_path")),		HashApi::handleRenamePath);

		timer->start(false);
	}

	HashApi::~HashApi() {
		timer->stop(true);

		HashManager::getInstance()->removeListener(this);
	}

	api_return HashApi::handleResume(ApiRequest&) {
		HashManager::getInstance()->resumeHashing();
		return websocketpp::http::status_code::no_content;
	}

	api_return HashApi::handlePause(ApiRequest&) {
		HashManager::getInstance()->pauseHashing();
		return websocketpp::http::status_code::no_content;
	}

	api_return HashApi::handleStop(ApiRequest&) {
		HashManager::getInstance()->stop();
		return websocketpp::http::status_code::no_content;
	}

	api_return HashApi::handleGetStats(ApiRequest& aRequest) {
		auto stats = HashManager::getInstance()->getStats();
		aRequest.setResponseBody(serializeHashStatistics(stats));
		return websocketpp::http::status_code::ok;
	}

	json HashApi::serializeHashStatistics(const HashManager::HashStats& aStats) noexcept {
		return {
			{ "hash_speed", aStats.speed },
			{ "hash_bytes_left", aStats.bytesLeft },
			{ "hash_files_left", aStats.filesLeft },
			{ "hash_bytes_added", aStats.bytesAdded },
			{ "hash_files_added", aStats.filesAdded },
			{ "hashers", aStats.hashersRunning },
			{ "pause_forced", aStats.isPaused },
			{ "max_hash_speed", SETTING(MAX_HASH_SPEED) },
		};
	}

	void HashApi::onTimer() noexcept {
		if (!subscriptionActive("hash_statistics")) {
			return;
		}

		auto newStats = serializeHashStatistics(HashManager::getInstance()->getStats());
		if (previousStats == newStats)
			return;

		send("hash_statistics", Serializer::serializeChangedProperties(newStats, previousStats));
		previousStats.swap(newStats);
	}

	void HashApi::on(HashManagerListener::MaintananceStarted) noexcept {
		updateDbStatus(true);
	}

	void HashApi::on(HashManagerListener::MaintananceFinished) noexcept {
		updateDbStatus(false);
	}

	void HashApi::on(HashManagerListener::DirectoryHashed, const string& aPath, int aFilesHashed, int64_t aSizeHashed, time_t aHashDuration, int aHasherId) noexcept {
		maybeSend("hasher_directory_finished", [&] { 
			return json({
				{ "path", aPath },
				{ "size", aSizeHashed },
				{ "files", aFilesHashed },
				{ "duration", aHashDuration },
				{ "hasher_id", aHasherId },
			});
		});
	}

	void HashApi::on(HashManagerListener::HasherFinished, int aDirshashed, int aFilesHashed, int64_t aSizeHashed, time_t aHashDuration, int aHasherId) noexcept {
		maybeSend("hasher_finished", [&] {
			return json({
				{ "size", aSizeHashed },
				{ "files", aFilesHashed },
				{ "directories", aDirshashed },
				{ "duration", aHashDuration },
				{ "hasher_id", aHasherId },
			});
		});
	}

	void HashApi::updateDbStatus(bool aMaintenanceRunning) noexcept {
		if (!subscriptionActive("hash_database_status"))
			return;

		send("hash_database_status", formatDbStatus(aMaintenanceRunning));
	}

	json HashApi::formatDbStatus(bool aMaintenanceRunning) noexcept {
		int64_t indexSize = 0, storeSize = 0;
		HashManager::getInstance()->getDbSizes(indexSize, storeSize);
		return{
			{ "maintenance_running", aMaintenanceRunning },
			{ "file_index_size", indexSize },
			{ "hash_store_size", storeSize },
		};
	}

	api_return HashApi::handleGetDbStatus(ApiRequest& aRequest) {
		aRequest.setResponseBody(formatDbStatus(HashManager::getInstance()->maintenanceRunning()));
		return websocketpp::http::status_code::ok;
	}

	api_return HashApi::handleOptimize(ApiRequest& aRequest) {
		if (HashManager::getInstance()->maintenanceRunning()) {
			aRequest.setResponseErrorStr("Database maintenance is running already");
			return websocketpp::http::status_code::bad_request;
		}

		auto verify = JsonUtil::getField<bool>("verify", aRequest.getRequestBody());
		HashManager::getInstance()->startMaintenance(verify);
		return websocketpp::http::status_code::no_content;
	}

	api_return HashApi::handleRenamePath(ApiRequest& aRequest) {
		const auto& j = aRequest.getRequestBody();
		auto oldPath = JsonUtil::getField<string>("old_path", j);
		auto newPath = JsonUtil::getField<string>("new_path", j);

		try {
			HashManager::getInstance()->renameFileThrow(oldPath, newPath);
		} catch (const HashException& e) {
			aRequest.setResponseErrorStr(e.getError());
			return websocketpp::http::status_code::bad_request;
		}

		return websocketpp::http::status_code::no_content;
	}
}