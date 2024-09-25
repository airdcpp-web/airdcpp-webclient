/*
 * Copyright (C) 2011-2024 AirDC++ Project
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

#include <airdcpp/QueueItemBase.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/Util.h>

namespace dcpp {

QueueItemBase::QueueItemBase(const string& aTarget, int64_t aSize, Priority aPriority, time_t aAdded, QueueToken aToken, Flags::MaskType aFlags) :
	Flags(aFlags), priority(aPriority), autoPriority(false), timeAdded(aAdded), size(aSize), token(aToken), target(aTarget) {

}

string QueueItemBase::getStringToken() const noexcept {
	return Util::toString(token);
}

double QueueItemBase::getPercentage(int64_t aDownloadedBytes) const noexcept {
	return size > 0 ? (double)aDownloadedBytes *100.0 / (double)size : 0;
}

string QueueItemBase::SourceCount::format() const noexcept {
	return total == 0 ? STRING(NONE) : STRING_F(USERS_ONLINE, online % total);
}

int QueueItemBase::SourceCount::compare(const SourceCount& a, const SourceCount& b) noexcept {
	if (a.online != b.online) {
		return dcpp::compare(a.online, b.online);
	}

	return dcpp::compare(a.total, b.total);
}

}