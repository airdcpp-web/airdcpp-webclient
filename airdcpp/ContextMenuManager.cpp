/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#include "stdinc.h"

#include "ContextMenuManager.h"
#include "DirectoryListingManager.h"
#include "QueueManager.h"
#include "SearchManager.h"


namespace dcpp {
	ContextMenuManager::ContextMenuManager() {

	}

	ContextMenuManager::~ContextMenuManager() {

	}

	ContextMenuItemList ContextMenuManager::normalizeMenuItems(const ActionHookDataList<ContextMenuItemList>& aResult) noexcept {
		ContextMenuItemList ret;
		for (const auto& i : aResult) {
			for (const auto& s : i->data) {
				ret.push_back(s);
			}
		}

		return ret;
	}

} // namespace dcpp