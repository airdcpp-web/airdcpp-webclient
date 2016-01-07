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
#include <web-server/WebServerManager.h>

#include <api/TransferApi.h>

#include <api/common/Serializer.h>

#include <airdcpp/DownloadManager.h>
#include <airdcpp/UploadManager.h>

namespace webserver {
	TransferApi::TransferApi(Session* aSession) : ApiModule(aSession, Access::ANY), timer(WebServerManager::getInstance()->addTimer([this] { onTimer(); }, 1000)) {
		DownloadManager::getInstance()->addListener(this);
		UploadManager::getInstance()->addListener(this);

		METHOD_HANDLER("stats", Access::ANY, ApiRequest::METHOD_GET, (), false, TransferApi::handleGetStats);

		createSubscription("transfer_statistics");
		timer->start();
	}

	TransferApi::~TransferApi() {
		timer->stop(true);

		DownloadManager::getInstance()->removeListener(this);
		UploadManager::getInstance()->removeListener(this);
	}

	api_return TransferApi::handleGetStats(ApiRequest& aRequest) {
		aRequest.setResponseBody({
			{ "session_downloaded", Socket::getTotalDown() },
			{ "session_uploaded", Socket::getTotalUp() },
			{ "start_total_downloaded", SETTING(TOTAL_DOWNLOAD) - Socket::getTotalDown() },
			{ "start_total_uploaded", SETTING(TOTAL_UPLOAD) - Socket::getTotalUp() },
		});

		return websocketpp::http::status_code::ok;
	}

	void TransferApi::onTimer() {
		if (!subscriptionActive("transfer_statistics"))
			return;

		auto resetSpeed = [](int transfers, int64_t speed) {
			return (transfers == 0 && speed < 10 * 1024) || speed < 1024;
		};

		auto downSpeed = DownloadManager::getInstance()->getLastDownSpeed();
		if (resetSpeed(lastDownloads, downSpeed)) {
			downSpeed = 0;
		}

		auto upSpeed = DownloadManager::getInstance()->getLastUpSpeed();
		if (resetSpeed(lastUploads, upSpeed)) {
			upSpeed = 0;
		}

		json j = {
			{ "speed_down", downSpeed },
			{ "speed_up", upSpeed },
			{ "upload_bundles", lastUploadBundles },
			{ "download_bundles", lastDownloadBundles },
			{ "uploads", lastUploads },
			{ "downloads", lastDownloads },
		};

		lastUploadBundles = 0;
		lastDownloadBundles = 0;

		lastUploads = 0;
		lastDownloads = 0;

		if (previousStats == j)
			return;

		previousStats = j;
		send("transfer_statistics", j);
	}

	void TransferApi::on(UploadManagerListener::Tick, const UploadList& aUploads) noexcept {
		lastUploads = aUploads.size();
	}

	void TransferApi::on(DownloadManagerListener::Tick, const DownloadList& aDownloads) noexcept {
		lastDownloads = aDownloads.size();
	}

	void TransferApi::on(DownloadManagerListener::BundleTick, const BundleList& bundles, uint64_t aTick) noexcept {
		lastDownloadBundles = bundles.size();
	}

	void TransferApi::on(UploadManagerListener::BundleTick, const UploadBundleList& bundles) noexcept {
		lastUploadBundles = bundles.size();
	}
}