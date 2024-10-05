/*
* Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#include <airdcpp/transfer/download/TrackableDownloadItem.h>

#include <airdcpp/transfer/download/Download.h>
#include <airdcpp/transfer/download/DownloadManager.h>
#include <airdcpp/core/localization/ResourceManager.h>

namespace dcpp {
	TrackableDownloadItem::TrackableDownloadItem(bool aDownloaded) noexcept {
		if (aDownloaded) {
			lastTimeFinished = GET_TIME();
		}
	}

	TrackableDownloadItem::~TrackableDownloadItem() noexcept {
		if (hasDownloads()) {
			DownloadManager::getInstance()->removeListener(this);
		}
	}

	bool TrackableDownloadItem::isDownloaded() const noexcept {
		return getDownloadState() == STATE_DOWNLOADED;
	}

	TrackableDownloadItem::State TrackableDownloadItem::getDownloadState() const noexcept {
		if (!lastError.empty()) {
			return STATE_DOWNLOAD_FAILED;
		}

		State state;
		{
			RLock l(cs);
			if (downloads.empty()) {
				state = hasCompletedDownloads() ? STATE_DOWNLOADED : STATE_DOWNLOAD_PENDING;
			} else {
				auto hasRunning = ranges::find_if(downloads | views::values, PathInfo::IsRunning()).base() != downloads.end();
				state = hasRunning ? STATE_DOWNLOADING : STATE_DOWNLOAD_PENDING;
			}
		}

		// dcdebug("download state: %d\n", state);
		return state;
	}

	void TrackableDownloadItem::onAddedQueue(const string& aPath, int64_t aSize) noexcept {
		bool first = false;

		{
			WLock l(cs);
			first = downloads.empty();
			downloads.try_emplace(aPath, aSize);
		}

		if (first) {
			DownloadManager::getInstance()->addListener(this);
		}

		onStateChanged();
	}

	time_t TrackableDownloadItem::getLastTimeFinished() const noexcept {
		return lastTimeFinished;
	}

	bool TrackableDownloadItem::hasCompletedDownloads() const noexcept {
		return lastTimeFinished > 0;
	}

	void TrackableDownloadItem::onRemovedQueue(const string& aPath, bool aFinished) noexcept {
		if (aFinished) {
			lastTimeFinished = GET_TIME();
		}
			
		bool empty = false;
		{
			WLock l(cs);
			downloads.erase(aPath);
			empty = downloads.empty();
		}

		if (empty) {
			DownloadManager::getInstance()->removeListener(this);
		}

		onStateChanged();
	}

	bool TrackableDownloadItem::hasDownloads() const noexcept {
		RLock l(cs);
		return !downloads.empty();
	}

	StringList TrackableDownloadItem::getDownloads() const noexcept {
		StringList ret;

		RLock l(cs);
		for (const auto& p : downloads | views::keys) {
			ret.push_back(p);
		}

		return ret;
	}

	void TrackableDownloadItem::onRunningStateChanged(const Download* aDownload, bool aFailed) noexcept {
		{
			RLock l(cs);
			auto d = downloads.find(aDownload->getPath());
			if (d == downloads.end()) {
				return;
			}

			auto& di = d->second;
			di.running = !aFailed;
		}

		onStateChanged();
	}

	double TrackableDownloadItem::PathInfo::getDownloadedPercentage() const noexcept {
		return size > 0 ? (static_cast<double>(downloaded) * 100.0) / static_cast<double>(size) : 0;
	}

	string TrackableDownloadItem::formatRunningStatus() const noexcept {
		RLock l(cs);
		auto p = ranges::find_if(downloads | views::values, PathInfo::IsRunning());

		if (p.base() != downloads.end() && (*p).trackProgress()) {
			if ((*p).downloaded == -1) {
				return STRING(DOWNLOAD_STARTING);
			}

			return STRING_F(RUNNING_PCT, (*p).getDownloadedPercentage());
		}

		return "Downloading";
	}

	TrackableDownloadItem::StatusInfo TrackableDownloadItem::getStatusInfo() const noexcept {
		auto state = getDownloadState();
		string str;

		switch (state) {
			case TrackableDownloadItem::STATE_DOWNLOAD_PENDING: str = "Download pending"; break;
			case TrackableDownloadItem::STATE_DOWNLOADING: str = formatRunningStatus(); break;
			case TrackableDownloadItem::STATE_DOWNLOADED: str = STRING(DOWNLOADED); break;
			case TrackableDownloadItem::STATE_DOWNLOAD_FAILED: str = lastError; break;
			default: dcassert(0);
		}

		return { state, str };
	}

	void TrackableDownloadItem::clearLastError() noexcept {
		if (lastError.empty()) {
			return;
		}

		lastError = Util::emptyString;
		onStateChanged();
	}

	void TrackableDownloadItem::on(DownloadManagerListener::Failed, const Download* aDownload, const string& aReason) noexcept {
		lastError = aReason;
		onRunningStateChanged(aDownload, true);
	}

	void TrackableDownloadItem::on(DownloadManagerListener::Starting, const Download* aDownload) noexcept {
		lastError = Util::emptyString;
		onRunningStateChanged(aDownload, false);
	}

	void TrackableDownloadItem::onProgress(const string& aDir, int64_t aDownloadedBytes) noexcept {
		{
			RLock l(cs);
			auto i = downloads.find(aDir);
			if (i == downloads.end()) {
				return;
			}

			i->second.downloaded = aDownloadedBytes;
		}

		onStateChanged();
	}

} // namespace dcpp
