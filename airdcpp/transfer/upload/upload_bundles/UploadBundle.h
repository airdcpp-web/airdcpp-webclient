/*
 * Copyright (C) 2012-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_UPLOADBUNDLE_H_
#define DCPLUSPLUS_DCPP_UPLOADBUNDLE_H_

#include <string>
#include <set>

#include <airdcpp/forward.h>
#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/transfer/upload/Upload.h>
#include <airdcpp/core/timer/TimerManager.h>

namespace dcpp {

using std::string;

class UploadBundle {
public:
	using BundleUploadList = set<TransferToken>;

	UploadBundle(const string& aTarget, const string& aToken, int64_t aSize, bool aSingleUser, int64_t aUploaded);
	~UploadBundle();

	GETSET(int64_t, size, Size);

	IGETSET(int64_t, speed, Speed, 0);
	IGETSET(int64_t, totalSpeed, TotalSpeed, 0);
	IGETSET(int64_t, actual, Actual, 0);
	IGETSET(int64_t, uploadedSegments, UploadedSegments, 0);
	GETSET(string, target, Target);

	const BundleUploadList& getUploads() const noexcept;
	
	int getConnectionCount() const noexcept;

	uint64_t getStart() const noexcept { return start; }

	bool getSingleUser() const noexcept { return singleUser; }
	void setSingleUser(bool aSingleUser, int64_t aUploadedSegments = 0) noexcept;

	string getName() const noexcept;
	const string& getToken() const noexcept { return token; }

	uint64_t getSecondsLeft() const noexcept;
	uint64_t getUploaded() const noexcept;

	void setBundlePath(const string& aBundleName, const string& aFilePath) noexcept;

	/* DownloadManager */
	void addUploadedSegment(int64_t aSize) noexcept;

	void addUpload(const Upload* u) noexcept;
	bool removeUpload(const Upload* u) noexcept;

	uint64_t countSpeed(const UploadList& aUploads) noexcept;

	// Should be called on every second
	// Returns true if the bundle should be removed
	bool checkDelaySecond() noexcept;
private:
	int delayTime = 0;
	uint64_t currentUploaded = 0;
	bool singleUser = true;
	time_t start = GET_TICK();

	BundleUploadList uploads;

	const string token;
};

using UploadBundlePtr = std::shared_ptr<UploadBundle>;
using UploadBundleList = std::vector<UploadBundlePtr>;

using TickUploadBundleList = std::vector<pair<UploadBundlePtr, OrderedStringSet>>;

}

#endif /* DCPLUSPLUS_DCPP_UPLOADBUNDLE_H_ */
