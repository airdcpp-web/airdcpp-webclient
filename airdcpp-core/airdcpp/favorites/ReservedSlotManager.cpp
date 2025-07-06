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

#include "ReservedSlotManager.h"

#include <airdcpp/core/timer/TimerManager.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/transfer/upload/UploadManager.h>
#include <airdcpp/transfer/upload/UploadQueueManager.h>
#include <airdcpp/user/HintedUser.h>


namespace dcpp {

using ranges::find_if;

ReservedSlotManager::ReservedSlotManager(SlotsUpdatedF&& aSlotsUpdatedF) noexcept : onSlotsUpdated(std::move(aSlotsUpdatedF)) {
	TimerManager::getInstance()->addListener(this);
}

ReservedSlotManager::~ReservedSlotManager() {
	TimerManager::getInstance()->removeListener(this);
}

optional<UserConnectResult> ReservedSlotManager::reserveSlot(const HintedUser& aUser, uint64_t aTime) noexcept {
	{
		WLock l(cs);
		reservedSlots[aUser] = aTime > 0 ? (GET_TICK() + aTime * 1000) : 0;
	}

	onSlotsUpdated(aUser);

	return UploadManager::getInstance()->getQueue().connectUser(aUser);
}

bool ReservedSlotManager::hasReservedSlot(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	return reservedSlots.contains(aUser);
}

void ReservedSlotManager::unreserveSlot(const UserPtr& aUser) noexcept {
	bool found = false;
	{
		WLock l(cs);
		auto uis = reservedSlots.find(aUser);
		if (uis != reservedSlots.end()){
			reservedSlots.erase(uis);
			found = true;
		}
	}

	if (found) {
		onSlotsUpdated(aUser);
	}
}

void ReservedSlotManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	UserList reservedRemoved;
	{
		WLock l(cs);
		for (auto j = reservedSlots.begin(); j != reservedSlots.end();) {
			if ((j->second > 0) && j->second < aTick) {
				reservedRemoved.push_back(j->first);
				reservedSlots.erase(j++);
			} else {
				++j;
			}
		}
	}

	for (const auto& u: reservedRemoved) {
		onSlotsUpdated(u);
	}
}

} // namespace dcpp