/*
 * Copyright (C) 2011 AirDC++ Project
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
#include "CID.h"
#include "GetSet.h"
#include "Upload.h"

namespace dcpp {

using std::string;

/**
 * A bundle is a set of related files that can be searched for by a single hash value.
 *
 * The hash is defined as follows:
 * For each file in the set, ordered by name (byte-order, not linguistic), except those specially marked,
 * compute the compute the hash. Then calculate the combined hash value by passing the concatenated hashes
 * of each file through the hash function.
 */
class UploadBundle : public intrusive_ptr_base<UploadBundle> {
public:

	typedef unordered_map<CID, string> CIDList;
	typedef unordered_map<CID, uint8_t> RunningMap;


	UploadBundle(const string& target, const string& token) : target(target), token(token), size(0), uploaded(0), speed(0), totalSpeed(0), 
		singleUser(true), uploadedSegments(0) { }

	GETSET(int64_t, size, Size);
	GETSET(int64_t, speed, Speed);
	GETSET(int64_t, totalSpeed, TotalSpeed);
	GETSET(int64_t, actual, Actual);
	GETSET(uint64_t, start, Start);
	GETSET(int64_t, uploadedSegments, UploadedSegments);

	GETSET(UploadList, uploads, Uploads);
	
	string token;
	string target;
	int getRunning() { return (int)uploads.size(); }

	void addUpload(Upload* u);
	bool removeUpload(Upload* u);

	uint64_t countSpeed();
	void addUploadedSegment(int64_t aSize);
	uint64_t getUploaded() const {
		return uploaded + uploadedSegments;
	}

	bool getSingleUser() { return singleUser; }
	void setSingleUser(bool aSingleUser);
	void findBundlePath(const string& aName);
	uint64_t getSecondsLeft();


	string getTarget() {
		return target;
	}

	string getToken() {
		return token;
	}

	void setTarget(string targetNew) {
		target =  targetNew;
	}

	string getName();

private:
	uint64_t uploaded;
	bool singleUser;
};

}

#endif /* DCPLUSPLUS_DCPP_UPLOADBUNDLE_H_ */
