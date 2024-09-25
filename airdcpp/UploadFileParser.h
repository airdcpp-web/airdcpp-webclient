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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_FILE_PARSER_H
#define DCPLUSPLUS_DCPP_UPLOAD_FILE_PARSER_H

#include <airdcpp/forward.h>

#include <airdcpp/Exception.h>
#include <airdcpp/Transfer.h>
#include <airdcpp/UploadRequest.h>

namespace dcpp {

struct StringMatch;
class UploadQueueManager;

struct ParsedUpload {
	string sourceFile;
	int64_t fileSize = 0;
	Transfer::Type type = Transfer::TYPE_LAST;

	string provider;
	bool miniSlot = false;
};

class UploadParser : public ParsedUpload {
public:
	class UploadParserException : public Exception {
	public:
		UploadParserException(const string& aError, bool aNoAccess) : Exception(aError), noAccess(aNoAccess) {

		}

		const bool noAccess;
	};

	UploadParser(const StringMatch& aFreeSlotMatcher) : freeSlotMatcher(aFreeSlotMatcher) {}

	void parseFileInfo(const UploadRequest& aRequest, ProfileToken aProfile, const HintedUser& aUser);
	Upload* toUpload(UserConnection& aSource, const UploadRequest& aRequest, unique_ptr<InputStream>& is, ProfileToken aProfile);

	bool usesSmallSlot() const noexcept;
private:
	ProfileTokenSet getShareProfiles(const HintedUser& aUser) const noexcept;

	void toRealWithSize(const UploadRequest& aRequest, ProfileToken aProfile, const HintedUser& aUser);
	const StringMatch& freeSlotMatcher;
};

} // namespace dcpp

#endif // !defined(UPLOAD_MANAGER_H)
