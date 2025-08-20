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

#ifndef DCPLUSPLUS_DCPP_FINISHED_ITEM_H
#define DCPLUSPLUS_DCPP_FINISHED_ITEM_H

#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/user/HintedUser.h>

#include <airdcpp/core/classes/IncrementingIdCounter.h>

namespace dcpp {

using FinishedItemToken = uint32_t;

class FinishedItem
{
public:
	FinishedItem(string const& aTarget, const HintedUser& aUser, int64_t aSize, int64_t aSpeed, time_t aTime);

	GETSET(string, target, Target);
	GETSET(HintedUser, user, User);
	GETSET(int64_t, size, Size);
	GETSET(int64_t, avgSpeed, AvgSpeed);
	GETSET(time_t, time, Time);

	FinishedItemToken getToken() const noexcept {
		return token;
	}
private:
	FinishedItemToken token;

	static IncrementingIdCounter<FinishedItemToken> idCounter;
};

typedef std::shared_ptr<FinishedItem> FinishedItemPtr;
typedef std::vector<FinishedItemPtr> FinishedItemList;

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_FINISHED_ITEM_H)
