/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_BUNDLE_H_
#define DCPLUSPLUS_DCPP_BUNDLE_H_

#include <string>
#include <set>

#include "TigerHash.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "QueueItem.h"
#include "forward.h"

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
class Bundle : public intrusive_ptr_base<Bundle> {
public:
	typedef QueueItem* Ptr;
	typedef vector<Ptr> qiList;
	typedef qiList::const_iterator Iter;
	typedef unordered_map<CID, string> CIDList;
	typedef unordered_map<CID, uint8_t> RunningMap;


	Bundle(const string& target, bool download) : target(target), download(download), token(Util::toString(Util::rand())), size(0), downloaded(0), speed(0), totalSpeed(0), 
		running(0), lastPercent(0), singleUser(false) { }

	GETSET(int64_t, size, Size);
	GETSET(int64_t, downloaded, Downloaded);
	GETSET(double, lastPercent, LastPercent);
	GETSET(string, token, Token);
	GETSET(int64_t, speed, Speed);
	GETSET(int64_t, totalSpeed, TotalSpeed);
	GETSET(uint64_t, start, Start);
	GETSET(uint16_t, running, Running);
	GETSET(bool, singleUser, SingleUser);
	
	bool download;
	qiList items;
	CIDList notifiedUsers;
	RunningMap runningUsers;
	HintedUserList uploadReports;

	bool getDownload() {
		return download;
	}
	void increaseSize(int64_t aSize) {
		size += aSize;
	}

	void decreaseSize(int64_t aSize) {
		size -= aSize;
	}

	void increaseDownloaded(int64_t aSize) {
		downloaded += aSize;
	}

	void decreaseDownloaded(int64_t aSize) {
		downloaded -= aSize;
	}

	void increaseRunning() {
		running++;
	}

	void decreaseRunning() {
		running--;
	}

	uint64_t getSecondsLeft();


	string getTarget() {
		return target;
	}


	void setTarget(string targetNew) {
		target =  targetNew;
	}

	string getName();

	string target;

	/*
	bool addUploadReport(const CID cid) {
		CIDList::const_iterator j = uploadReports.find(cid);
		if (j != uploadReports.end()) {
			return false;
		}
		uploadReports.insert(cid);
		return true;
	}

	void removeUploadReport(const CID cid) {
		uploadReports.erase(cid);
	}

	checkUploadReports(const CID cid) {

	}
	*/

};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLE_H_ */
