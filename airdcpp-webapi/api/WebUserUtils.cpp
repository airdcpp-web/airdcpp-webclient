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

#include <api/WebUserUtils.h>
#include <api/WebUserApi.h>

#include <api/common/Format.h>


namespace webserver {
	const PropertyList WebUserUtils::properties = {
		{ PROP_NAME, "username", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_PERMISSIONS, "permissions", TYPE_LIST_NUMERIC, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_ACTIVE_SESSIONS, "active_sessions", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_LAST_LOGIN, "last_login", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
	};

	const PropertyItemHandler<WebUserPtr> WebUserUtils::propertyHandler = {
		properties,
		WebUserUtils::getStringInfo, WebUserUtils::getNumericInfo, WebUserUtils::compareItems, WebUserUtils::serializeItem, WebUserUtils::filterItem
	};

	json WebUserUtils::serializeItem(const WebUserPtr& aItem, int aPropertyName) noexcept {
		json j;

		switch (aPropertyName) {
		case PROP_PERMISSIONS:
		{
			return Serializer::serializePermissions(aItem->getPermissions());
		}
		}


		return j;
	}

	bool WebUserUtils::filterItem(const WebUserPtr& aItem, int aPropertyName, const StringMatch& aStringMatch, double /*aNumericMatcher*/) noexcept {
		switch (aPropertyName) {
		case PROP_PERMISSIONS:
		{
			auto i = WebUser::stringToAccess(aStringMatch.pattern);
			if (i != Access::LAST) {
				return aItem->hasPermission(i);
			}
		}
		}

		return false;
	}

	int WebUserUtils::compareItems(const WebUserPtr& a, const WebUserPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_PERMISSIONS: {
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
		case PROP_NAME: return aItem->getUserName();
		default: dcassert(0); return 0;
		}
	}
	double WebUserUtils::getNumericInfo(const WebUserPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_LAST_LOGIN: return (double)aItem->getLastLogin();
		case PROP_ACTIVE_SESSIONS: return (double)aItem->getActiveSessions();
		default: dcassert(0); return 0;
		}
	}
}