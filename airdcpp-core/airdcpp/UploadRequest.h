/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_REQUEST_H
#define DCPLUSPLUS_DCPP_UPLOAD_REQUEST_H

#include "forward.h"

#include "Segment.h"
#include "Transfer.h"

namespace dcpp {

struct UploadRequest {
	UploadRequest(const string& aType, const string& aFile, const Segment& aSegment) : type(aType), file(aFile), segment(aSegment) {}
	UploadRequest(const string& aType, const string& aFile, const Segment& aSegment, const string& aUserSID, bool aListRecursive) : UploadRequest(aType, aFile, aSegment) {
		userSID = aUserSID;
		// isTTHList = aIsTTHList;
		listRecursive = aListRecursive;
	}

	bool validate() const noexcept {
		auto failed = file.empty() || segment.getStart() < 0 || segment.getSize() < -1 || segment.getSize() == 0;
		return !failed;
	}

	bool isFullFilelist() const noexcept {
		return file == Transfer::USER_LIST_NAME_BZ || file == Transfer::USER_LIST_NAME_EXTRACTED;
	}

	const string& type;
	const string& file;
	Segment segment;
	string userSID;
	bool listRecursive = false;
	// bool isTTHList = false;
};

} // namespace dcpp

#endif // !defined(UPLOAD_MANAGER_H)
