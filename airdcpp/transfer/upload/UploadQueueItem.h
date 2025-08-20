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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_QUEUE_ITEM_H
#define DCPLUSPLUS_DCPP_UPLOAD_QUEUE_ITEM_H

#include <airdcpp/forward.h>

#include <airdcpp/core/classes/FastAlloc.h>
#include <airdcpp/user/HintedUser.h>
#include <airdcpp/user/UserInfoBase.h>

#include <airdcpp/core/classes/IncrementingIdCounter.h>

namespace dcpp {


using UploadQueueItemToken = uint32_t;

class UploadQueueItem : public FastAlloc<UploadQueueItem> {
public:
	UploadQueueItem(const HintedUser& _user, const string& _file, int64_t _pos, int64_t _size);

	int64_t getSize() const noexcept { return size; }
	uint64_t getTime() const noexcept { return time; }
	const string& getFile() const noexcept { return file; }

	const HintedUser& getHintedUser() const noexcept { return user; }
	UploadQueueItemToken getToken() const noexcept { return token; }

	GETSET(int64_t, pos, Pos);

private:
	const UploadQueueItemToken token;
	const HintedUser	user;
	const string		file;
	const int64_t		size;
	const uint64_t	time;

	static IncrementingIdCounter<UploadQueueItemToken> idCounter;
};

using UploadQueueItemPtr = std::shared_ptr<UploadQueueItem>;

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_UPLOAD_QUEUE_ITEM_H)