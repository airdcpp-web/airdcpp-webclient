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

#include <api/HashApi.h>

#include <api/common/Serializer.h>

namespace webserver {
	HashApi::HashApi(Session* aSession) : ApiModule(aSession, Access::SETTINGS_VIEW) {
		HashManager::getInstance()->addListener(this);

		createSubscription("hash_database_status");

		METHOD_HANDLER("database_status", Access::SETTINGS_VIEW, ApiRequest::METHOD_GET, (), false, HashApi::handleGetDbStatus);
		METHOD_HANDLER("optimize_database", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (), true, HashApi::handleOptimize);
	}

	HashApi::~HashApi() {
		HashManager::getInstance()->removeListener(this);
	}

	void HashApi::on(HashManagerListener::MaintananceStarted) noexcept {
		updateDbStatus(true);
	}

	void HashApi::on(HashManagerListener::MaintananceFinished) noexcept {
		updateDbStatus(false);
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
		return websocketpp::http::status_code::ok;
	}
}