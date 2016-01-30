/*
* Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#include "TrackableDownloadItem.h"

#include "Download.h"
#include "DownloadManager.h"


namespace dcpp {
	TrackableDownloadItem::TrackableDownloadItem(bool aDownloaded) noexcept : state(aDownloaded ? STATE_DOWNLOADED : STATE_DOWNLOAD_PENDING) {
		if (aDownloaded) {
			timeFinished = GET_TIME();
		}
	}

	TrackableDownloadItem::~TrackableDownloadItem() {
		if (hasDownloads()) {
			DownloadManager::getInstance()->removeListener(this);
		}
	}

	void TrackableDownloadItem::updateState() noexcept {
		State newState;
		{
			RLock l(cs);
			if (downloads.empty()) {
				newState = completedDownloads ? STATE_DOWNLOADED : STATE_DOWNLOAD_PENDING;
			} else {
				auto hasRunning = boost::find_if(downloads | map_values, PathInfo::IsRunning()).base() != downloads.end();
				newState = hasRunning ? STATE_DOWNLOADING : STATE_DOWNLOAD_PENDING;
			}
		}

		dcdebug("download state: %d\n", newState);

		state = newState;
		onStateChanged();
	}

	void TrackableDownloadItem::onAddedQueue(const string& aPath, int64_t aSize) noexcept {
		bool first = false;

		{
			WLock l(cs);
			first = downloads.empty();
			downloads.emplace(aPath, PathInfo(aSize));
		}

		if (first) {
			DownloadManager::getInstance()->addListener(this);
		}

		updateState();
	}

	void TrackableDownloadItem::onRemovedQueue(const string& aPath, bool aFinished) noexcept {
		if (aFinished) {
			completedDownloads = true;
			timeFinished = GET_TIME();
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

		updateState();
	}

	bool TrackableDownloadItem::hasDownloads() const noexcept {
		RLock l(cs);
		return !downloads.empty();
	}

	StringList TrackableDownloadItem::getDownloads() const noexcept {
		StringList ret;

		RLock l(cs);
		for (const auto& p : downloads | map_keys) {
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

		updateState();
	}

	double TrackableDownloadItem::PathInfo::getDownloadedPercentage() const noexcept {
		return size > 0 ? (static_cast<double>(downloaded) * 100.0) / static_cast<double>(size) : 0;
	}

	string TrackableDownloadItem::formatRunningStatus() const noexcept {
		auto p = find_if(downloads | map_values, PathInfo::IsRunning());

		if (p.base() != downloads.end() && p->trackProgress()) {
			if (p->downloaded == -1) {
				return STRING(DOWNLOAD_STARTING);
			}

			return STRING_F(RUNNING_PCT, p->getDownloadedPercentage());
		}

		return "Downloading";
	}

	string TrackableDownloadItem::getStatusString() const noexcept {
		switch (state) {
			case TrackableDownloadItem::STATE_DOWNLOAD_PENDING: return "Download pending";
			case TrackableDownloadItem::STATE_DOWNLOADING: return formatRunningStatus();
			case TrackableDownloadItem::STATE_DOWNLOADED: return STRING(DOWNLOADED);
		}

		dcassert(0);
		return "";
	}

	void TrackableDownloadItem::on(DownloadManagerListener::Failed, const Download* aDownload, const string& /*aReason*/) noexcept {
		onRunningStateChanged(aDownload, true);
	}

	void TrackableDownloadItem::on(DownloadManagerListener::Starting, const Download* aDownload) noexcept {
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

		updateState();
	}

} // namespace dcpp
