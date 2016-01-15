/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <api/ShareUtils.h>
#include <api/ShareRootApi.h>

#include <api/common/Format.h>

#include <airdcpp/ShareManager.h>

namespace webserver {
	json ShareUtils::serializeItem(const ShareDirectoryInfoPtr& aItem, int aPropertyName) noexcept {
		json j;

		switch (aPropertyName) {
		case ShareRootApi::PROP_PROFILES:
		{
			return aItem->profiles;
		}
		}


		return j;
	}

	string ShareUtils::formatRefreshState(const ShareDirectoryInfoPtr& aItem) noexcept {
		switch (static_cast<ShareManager::RefreshState>(aItem->refreshState)) {
			case ShareManager::RefreshState::STATE_NORMAL: return STRING(NORMAL);
			case ShareManager::RefreshState::STATE_PENDING: return "Refresh pending";
			case ShareManager::RefreshState::STATE_RUNNING: return "Refreshing";
		}

		return Util::emptyString;
	}

	bool ShareUtils::filterItem(const ShareDirectoryInfoPtr& aItem, int aPropertyName, const StringMatch&, double aNumericMatcher) noexcept {
		switch (aPropertyName) {
		case ShareRootApi::PROP_PROFILES:
		{
			return aItem->profiles.find(static_cast<int>(aNumericMatcher)) != aItem->profiles.end();
		}
		}

		return false;
	}

	int ShareUtils::compareItems(const ShareDirectoryInfoPtr& a, const ShareDirectoryInfoPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		//case ShareApi::PROP_VIRTUAL_NAME: {
			//if (a->getType() == b->getType())
			//	return Util::stricmp(a->getName(), b->getName());
			//else
			//	return (a->getType() == FilelistItemInfo::DIRECTORY) ? -1 : 1;
		//}
		case ShareRootApi::PROP_PROFILES: {
			return compare(a->profiles.size(), b->profiles.size());
		}
		default:
			dcassert(0);
		}

		return 0;
	}

	std::string ShareUtils::getStringInfo(const ShareDirectoryInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case ShareRootApi::PROP_VIRTUAL_NAME: return aItem->virtualName;
		case ShareRootApi::PROP_PATH: return aItem->path;
		case ShareRootApi::PROP_REFRESH_STATE: return formatRefreshState(aItem);
		default: dcassert(0); return Util::emptyString;
		}
	}
	double ShareUtils::getNumericInfo(const ShareDirectoryInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case ShareRootApi::PROP_SIZE: return (double)aItem->size;
		case ShareRootApi::PROP_INCOMING: return (double)aItem->incoming;
		case ShareRootApi::PROP_LAST_REFRESH_TIME: return (double)aItem->lastRefreshTime;
		case ShareRootApi::PROP_REFRESH_STATE: return (double)aItem->refreshState;
		default: dcassert(0); return 0;
		}
	}
}