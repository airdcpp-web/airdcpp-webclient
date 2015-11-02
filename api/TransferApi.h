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

#ifndef DCPLUSPLUS_DCPP_TRANSFERAPI_H
#define DCPLUSPLUS_DCPP_TRANSFERAPI_H

#include <web-server/stdinc.h>
#include <web-server/Timer.h>

#include <api/ApiModule.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/DownloadManagerListener.h>
#include <airdcpp/UploadManagerListener.h>

namespace webserver {
	class TransferApi : public ApiModule, private DownloadManagerListener, private UploadManagerListener {
	public:
		TransferApi(Session* aSession);
		~TransferApi();

		int getVersion() const noexcept {
			return 0;
		}
	private:
		void onTimer();

		void on(DownloadManagerListener::Tick, const DownloadList& aDownloads) noexcept;
		void on(DownloadManagerListener::BundleTick, const BundleList& bundles, uint64_t aTick) noexcept;

		void on(UploadManagerListener::Tick, const UploadList& aUploads) noexcept;
		void on(UploadManagerListener::BundleTick, const UploadBundleList& bundles) noexcept;

		json previousStats;

		int lastUploadBundles = 0;
		int lastDownloadBundles = 0;

		int lastUploads = 0;
		int lastDownloads = 0;

		TimerPtr timer;
	};
}

#endif