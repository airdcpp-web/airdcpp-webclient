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

#ifndef DCPLUSPLUS_DCPP_QUEUE_DOWNLOAD_INFO_H_
#define DCPLUSPLUS_DCPP_QUEUE_DOWNLOAD_INFO_H_

#include <airdcpp/typedefs.h>

#include <airdcpp/HintedUser.h>
#include <airdcpp/Priority.h>

namespace dcpp {

enum class QueueDownloadType {
	ANY,
	SMALL,
	MCN_NORMAL
};

struct QueueDownloadResultBase {
	string hubHint;

	// The last error why a file can't be started (not cleared if a download is found afterwards)
	string lastError;

	// Indicates that there's a valid file even if it can't be temporarily started e.g. due to configured download limits
	bool hasDownload = false;

	virtual void merge(const QueueDownloadResultBase& aOther) noexcept {
		hubHint = aOther.hubHint;
		hasDownload = aOther.hasDownload;
		lastError = aOther.lastError;
	}
};


// Queue results
struct QueueDownloadResult : QueueDownloadResultBase {
	QueueDownloadResult(const string& aHubHint) {
		hubHint = aHubHint;
	}

	// Whether the returned hubHint should be strictly followed (e.g. a filelist download)
	bool allowUrlChange = true;

	// Possible bundle
	optional<QueueToken> bundleToken;

	bool startDownload = false;
	QueueDownloadType downloadType = QueueDownloadType::ANY;

	QueueItemPtr qi = nullptr;
};


struct QueueDownloadQuery {
	QueueDownloadQuery(const UserPtr& aUser, const OrderedStringSet& aOnlineHubs, const QueueTokenSet& aRunningBundles) : user(aUser), onlineHubs(aOnlineHubs), runningBundles(aRunningBundles) {}

	const UserPtr user;

	QueueDownloadType downloadType = QueueDownloadType::ANY;

	int64_t wantedSize = 0;
	int64_t lastSpeed = 0;

	Priority minPrio = Priority::LOWEST;

	const OrderedStringSet& onlineHubs;
	const QueueTokenSet& runningBundles;
};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLEINFO_H_ */
