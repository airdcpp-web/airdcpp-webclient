/*
 * Copyright (C) 2012-2015 AirDC++ Project
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
	int delayTime = 0;

	GETSET(UploadList, uploads, Uploads);
	UploadList& getUploads() { return uploads; }
	
	int getRunning() const { return (int)uploads.size(); }

	uint64_t getStart() const { return start; }

	bool getSingleUser() const { return singleUser; }
	void setSingleUser(bool aSingleUser, int64_t uploadedSegments = 0);

	string getName() const;
	string getTarget() const { return target; }
	void setTarget(string targetNew) { target = targetNew; }

	string getToken() const { return token; }

	uint64_t getSecondsLeft() const;
	uint64_t getUploaded() const { return uploaded + uploadedSegments; }

	void findBundlePath(const string& aName);

	/* DownloadManager */
	void addUploadedSegment(int64_t aSize);

	void addUpload(Upload* u);
	bool removeUpload(Upload* u);

	uint64_t countSpeed();

private:
	uint64_t uploaded = 0;
	bool singleUser = true;
	time_t start = GET_TICK();

	string token;
	string target;
};

}

#endif /* DCPLUSPLUS_DCPP_UPLOADBUNDLE_H_ */
