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

#ifndef DCPLUSPLUS_DCPP_TRACKABLE_DOWNLOAD_H
#define DCPLUSPLUS_DCPP_TRACKABLE_DOWNLOAD_H

#include "forward.h"

#include "CriticalSection.h"
#include "DownloadManagerListener.h"

namespace dcpp {

	class TrackableDownloadItem : private DownloadManagerListener
	{
	public:
		virtual void onRemovedQueue(const string& aDir, bool aFinished) noexcept;

		// Leave the size unspecified if there is no tracking for download progress
		virtual void onAddedQueue(const string& aDir, int64_t aSize = -1) noexcept;

		virtual void onProgress(const string& aDir, int64_t aDownloadedBytes) noexcept;

		TrackableDownloadItem(bool aDownloaded) noexcept;
		~TrackableDownloadItem() noexcept;

		enum State : uint8_t {
			STATE_DOWNLOAD_PENDING,
			STATE_DOWNLOADING,
			STATE_DOWNLOADED,
		};

		State getDownloadState() const noexcept {
			return state;
		}

		bool hasCompletedDownloads() const noexcept {
			return completedDownloads;
		}

		bool hasDownloads() const noexcept;
		StringList getDownloads() const noexcept;

		IGETSET(time_t, timeFinished, TimeFinished, 0);

		string getStatusString() const noexcept;
	protected:
		virtual void onStateChanged() noexcept = 0;

	private:
		bool completedDownloads = false;
		State state = STATE_DOWNLOAD_PENDING;

		void updateState() noexcept;
		void onRunningStateChanged(const Download* aDownload, bool aFailed) noexcept;

		void on(DownloadManagerListener::Failed, const Download* aDownload, const string& aReason) noexcept;
		void on(DownloadManagerListener::Starting, const Download* aDownload) noexcept;

		struct PathInfo {
			struct IsRunning {
				bool operator()(const PathInfo& d) const {
					return d.running;
				}
			};

			PathInfo(int64_t aSize) : size(aSize) { }

			bool running = false;
			int64_t size = -1;
			int64_t downloaded = -1;

			bool trackProgress() const noexcept { return size != -1; }
			double getDownloadedPercentage() const noexcept;
		};

		mutable SharedMutex cs;
		map<string, PathInfo> downloads;

		string formatRunningStatus() const noexcept;
	};

} // namespace dcpp

#endif
