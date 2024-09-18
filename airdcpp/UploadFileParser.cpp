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

#include "stdinc.h"
#include "UploadFileParser.h"

#include "BZUtils.h"
#include "ClientManager.h"
#include "File.h"
#include "PathUtil.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "ShareManager.h"
#include "Streams.h"
#include "Upload.h"
#include "UserConnection.h"


namespace dcpp {


ProfileTokenSet UploadParser::getShareProfiles(const HintedUser& aUser) const noexcept {
	ProfileTokenSet profiles;

	const auto ouList = ClientManager::getInstance()->getOnlineUsers(aUser);
	for (const auto& ou : ouList) {
		profiles.insert(ou->getClient()->get(HubSettings::ShareProfile));
	}

	return profiles;
}

void UploadParser::toRealWithSize(const UploadRequest& aRequest, ProfileToken aProfile, const HintedUser& aUser) {
	// Get all hubs with file transfers
	auto profiles = getShareProfiles(aUser);

	// Ensure that the request profile exists
	profiles.insert(aProfile);

	auto result = ShareManager::getInstance()->toRealWithSize(aRequest.file, profiles, aUser, aRequest.segment);
	if (!result.found) {
		auto error = result.noAccess ? "You don't have access to this file" : UserConnection::FILE_NOT_AVAILABLE;
		throw UploadParserException(error, result.noAccess);
	}

	sourceFile = result.path;
	fileSize = result.size;
	provider = result.provider->getProviderName();
}

void UploadParser::parseFileInfo(const UploadRequest& aRequest, ProfileToken aProfile, const HintedUser& aUser) {

	if (aRequest.type == Transfer::names[Transfer::TYPE_FILE]) {
		auto fullFilelist = aRequest.isFullFilelist();
		type = fullFilelist ? Transfer::TYPE_FULL_LIST : Transfer::TYPE_FILE;

		//check that we have a file
		if (fullFilelist) {
			tie(fileSize, sourceFile) = std::move(ShareManager::getInstance()->getFileListInfo(aRequest.file, aProfile));
			miniSlot = true;
		} else {
			toRealWithSize(aRequest, aProfile, aUser);
			miniSlot = freeSlotMatcher.match(PathUtil::getFileName(sourceFile));
		}

		miniSlot = miniSlot || (fileSize <= Util::convertSize(SETTING(SET_MINISLOT_SIZE), Util::KB));
	} else if (aRequest.type == Transfer::names[Transfer::TYPE_TREE]) {
		toRealWithSize(aRequest, aProfile, aUser);
		type = Transfer::TYPE_TREE;
		miniSlot = true;

	} else if (aRequest.type == Transfer::names[Transfer::TYPE_PARTIAL_LIST]) {
		type = Transfer::TYPE_PARTIAL_LIST;
		miniSlot = true;
	} else if (aRequest.type == Transfer::names[Transfer::TYPE_TTH_LIST]) {
		type = Transfer::TYPE_TTH_LIST;
		miniSlot = true;
	} else {
		throw UploadParserException("Unknown file type", false);
	}
}

Upload* UploadParser::toUpload(UserConnection& aSource, const UploadRequest& aRequest, unique_ptr<InputStream>& is, ProfileToken aProfile) {
	bool resumed = is.get();
	auto startPos = aRequest.segment.getStart();
	auto bytes = aRequest.segment.getSize();

	switch (type) {
	case Transfer::TYPE_FULL_LIST:
		// handle below...
		[[fallthrough]];
	case Transfer::TYPE_FILE:
	{
		if (aRequest.file == Transfer::USER_LIST_NAME_EXTRACTED) {
			// Unpack before sending...
			string bz2 = File(sourceFile, File::READ, File::OPEN).read();
			string xml;
			BZUtil::decodeBZ2(reinterpret_cast<const uint8_t*>(bz2.data()), bz2.size(), xml);
			// Clear to save some memory...
			string().swap(bz2);
			is.reset(new MemoryInputStream(xml));
			startPos = 0;
			fileSize = bytes = is->getSize();
		} else {
			if (bytes == -1) {
				bytes = fileSize - startPos;
			}

			if ((startPos + bytes) > fileSize) {
				throw Exception("Bytes were requested beyond the end of the file");
			}

			if (!is) {
				auto f = make_unique<File>(sourceFile, File::READ, File::OPEN | File::SHARED_WRITE); // write for partial sharing
				is = std::move(f);
			}

			is->setPos(startPos);

			if ((startPos + bytes) < fileSize) {
				is.reset(new LimitedInputStream<true>(is.release(), bytes));
			}
		}
		break;
	}
	case Transfer::TYPE_TREE:
	{
		sourceFile = aRequest.file; // sourceFile was changed to the path
		unique_ptr<MemoryInputStream> mis(ShareManager::getInstance()->getTree(sourceFile, aProfile));
		if (!mis.get()) {
			return nullptr;
		}

		startPos = 0;
		fileSize = bytes = mis->getSize();
		is = std::move(mis);
		break;
	}
	case Transfer::TYPE_TTH_LIST:
	{
		unique_ptr<MemoryInputStream> mis = nullptr;
		if (!PathUtil::isAdcDirectoryPath(aRequest.file)) {
			BundlePtr bundle = nullptr;
			mis.reset(QueueManager::getInstance()->generateTTHList(Util::toUInt32(aRequest.file), aProfile != SP_HIDDEN, bundle));

			// We don't want to show the token in transfer view
			if (bundle) {
				sourceFile = bundle->getName();
			} else {
				dcassert(0);
			}
		} else {
			mis.reset(ShareManager::getInstance()->generateTTHList(aRequest.file, aRequest.listRecursive, aProfile));
		}
		if (!mis.get()) {
			return nullptr;
		}

		startPos = 0;
		fileSize = bytes = mis->getSize();
		is = std::move(mis);
		break;
	}
	case Transfer::TYPE_PARTIAL_LIST: {
		unique_ptr<MemoryInputStream> mis = nullptr;
		// Partial file list
		mis.reset(ShareManager::getInstance()->generatePartialList(aRequest.file, aRequest.listRecursive, aProfile));
		if (!mis.get()) {
			return nullptr;
		}

		startPos = 0;
		fileSize = bytes = mis->getSize();
		is = std::move(mis);
		break;
	}
	default:
		dcassert(0);
		break;
	}

	// Upload
	// auto size = is->getSize();
	dcassert(is);
	auto u = new Upload(aSource, sourceFile, TTHValue(), std::move(is));
	u->setSegment(Segment(startPos, bytes));
	if (u->getSegment().getEnd() != fileSize) {
		u->setFlag(Upload::FLAG_CHUNKED);
	}
	//if (partialFileSharing) {
	//	u->setFlag(Upload::FLAG_PARTIAL);
	//}
	if (resumed) {
		u->setFlag(Upload::FLAG_RESUMED);
	}

	u->setFileSize(fileSize);
	u->setType(type);
	dcdebug("Created upload for file %s (conn %s, resuming: %s)\n", u->getPath().c_str(), u->getConnectionToken().c_str(), resumed ? "true" : "false");
	return u;
}


bool UploadParser::usesSmallSlot() const noexcept {
	return type == Transfer::TYPE_PARTIAL_LIST || type == Transfer::TYPE_TTH_LIST || (type != Transfer::TYPE_FULL_LIST && fileSize <= 65792);
}

} // namespace dcpp