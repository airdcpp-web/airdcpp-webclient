/*
 * Copyright (C) 2012-2023 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_UPLOADBUNDLE_H_
#define DCPLUSPLUS_DCPP_UPLOADBUNDLE_H_

#include <string>
#include <set>

#include "Pointer.h"
#include "forward.h"
#include "GetSet.h"
#include "Upload.h"
#include "TimerManager.h"

namespace dcpp {

using std::string;

class UploadBundle : public intrusive_ptr_base<UploadBundle> {
public:
	UploadBundle(const string& aTarget, const string& aToken, int64_t aSize, bool aSingleUser, int64_t aUploaded);

	GETSET(int64_t, size, Size);

	IGETSET(int64_t, speed, Speed, 0);
	IGETSET(int64_t, totalSpeed, TotalSpeed, 0);
	IGETSET(int64_t, actual, Actual, 0);
	IGETSET(int64_t, uploadedSegments, UploadedSegments, 0);
	GETSET(string, target, Target);
	int delayTime = 0;

	const UploadList& getUploads() const noexcept { return uploads; }
	
	int getRunning() const noexcept { return (int)uploads.size(); }

	uint64_t getStart() const noexcept { return start; }

	bool getSingleUser() const noexcept { return singleUser; }
	void setSingleUser(bool aSingleUser, int64_t aUploadedSegments = 0) noexcept;

	string getName() const noexcept;
	string getToken() const noexcept { return token; }

	uint64_t getSecondsLeft() const noexcept;
	uint64_t getUploaded() const noexcept { return uploaded + uploadedSegments; }

	void findBundlePath(const string& aName) noexcept;

	/* DownloadManager */
	void addUploadedSegment(int64_t aSize) noexcept;

	void addUpload(Upload* u) noexcept;
	bool removeUpload(Upload* u) noexcept;

	uint64_t countSpeed() noexcept;

private:
	uint64_t uploaded = 0;
	bool singleUser = true;
	time_t start = GET_TICK();

	UploadList uploads;

	string token;
};

}

#endif /* DCPLUSPLUS_DCPP_UPLOADBUNDLE_H_ */
