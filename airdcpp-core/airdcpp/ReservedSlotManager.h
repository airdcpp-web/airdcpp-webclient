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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_USER_SLOT_MANAGER_H
#define DCPLUSPLUS_DCPP_UPLOAD_USER_SLOT_MANAGER_H

#include "forward.h"

#include "CriticalSection.h"
#include "TimerManagerListener.h"
#include "User.h"

namespace dcpp {

class ReservedSlotManager : private TimerManagerListener
{
public:
	
	// @param aUser Reserve an upload slot for this user and connect.
	void reserveSlot(const HintedUser& aUser, uint64_t aTime) noexcept;
	void unreserveSlot(const UserPtr& aUser) noexcept;
	bool hasReservedSlot(const UserPtr& aUser) const noexcept;

	using SlotsUpdatedF = std::function<void (const UserPtr &)>;
	explicit ReservedSlotManager(SlotsUpdatedF&& aSlotsUpdatedF) noexcept;
	~ReservedSlotManager() override;
private:
	mutable SharedMutex cs;

	using SlotMap = unordered_map<UserPtr, uint64_t, User::Hash>;
	SlotMap reservedSlots;
	
	// TimerManagerListener
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;

	SlotsUpdatedF onSlotsUpdated;
};

} // namespace dcpp

#endif // !defined(UPLOAD_MANAGER_H)
