/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#include <api/WebUserUtils.h>
#include <api/WebUserApi.h>

#include <api/common/Format.h>


namespace webserver {
	json WebUserUtils::serializeItem(const WebUserPtr& aItem, int aPropertyName) noexcept {
		json j;

		switch (aPropertyName) {
		case WebUserApi::PROP_PERMISSIONS:
		{
			return aItem->getPermissions();
		}
		}


		return j;
	}

	bool WebUserUtils::filterItem(const WebUserPtr& aItem, int aPropertyName, const StringMatch& aStringMatch, double aNumericMatcher) noexcept {
		switch (aPropertyName) {
		case WebUserApi::PROP_PERMISSIONS:
		{
			auto i = WebUser::toAccess(aStringMatch.pattern);
			if (i != Access::LAST) {
				return aItem->hasPermission(i);
			}
		}
		}

		return false;
	}

	int WebUserUtils::compareItems(const WebUserPtr& a, const WebUserPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case WebUserApi::PROP_PERMISSIONS: {
			if (a->isAdmin() != b->isAdmin()) {
				return a->isAdmin() ? 1 : -1;
			}

			return compare(a->countPermissions(), b->countPermissions());
		}
		default:
			dcassert(0);
		}

		return 0;
	}

	std::string WebUserUtils::getStringInfo(const WebUserPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case WebUserApi::PROP_NAME: return aItem->getUserName();
		default: dcassert(0); return 0;
		}
	}
	double WebUserUtils::getNumericInfo(const WebUserPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case WebUserApi::PROP_LAST_LOGIN: return (double)aItem->getLastLogin();
		case WebUserApi::PROP_ACTIVE_SESSIONS: return (double)aItem->getActiveSessions();
		default: dcassert(0); return 0;
		}
	}
}