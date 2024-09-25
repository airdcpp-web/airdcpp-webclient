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
#include <airdcpp/HubEntry.h>

#include <airdcpp/LinkUtil.h>
#include <airdcpp/ShareManager.h>
#include <airdcpp/ShareProfile.h>

namespace dcpp {

FavoriteHubEntry::FavoriteHubEntry() noexcept : 
	token(ValueGenerator::randInt()) { }

bool FavoriteHubEntry::isAdcHub() const noexcept {
	return LinkUtil::isAdcHub(server);
}

string FavoriteHubEntry::getShareProfileName() const noexcept {
	auto sp = ShareManager::getInstance()->getShareProfile(get(HubSettings::ShareProfile));
	if (sp) {
		return sp->getDisplayName();
	}

	return ShareManager::getInstance()->getShareProfile(SETTING(DEFAULT_SP))->getDisplayName();
}

}