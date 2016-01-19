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
		virtual void onAddedQueue(const string& aDir) noexcept;

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
	protected:
		virtual void onStateChanged() noexcept = 0;

	private:
		bool completedDownloads = false;
		State state = STATE_DOWNLOAD_PENDING;

		void updateState() noexcept;
		void onDownloadStateChanged(const Download* aDownload, bool aFailed) noexcept;

		void on(DownloadManagerListener::Failed, const Download* aDownload, const string& aReason) noexcept;
		void on(DownloadManagerListener::Starting, const Download* aDownload) noexcept;

		mutable SharedMutex cs;
		map<string, bool> downloads;
	};

} // namespace dcpp

#endif
