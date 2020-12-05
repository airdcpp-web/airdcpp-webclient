/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_WEBUSER_UTILS_H
#define DCPLUSPLUS_DCPP_WEBUSER_UTILS_H

#include <api/common/Property.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/StringMatch.h>

#include <web-server/WebUser.h>


namespace webserver {
	class WebUserUtils {
	public:
		static const PropertyList properties;
		static const PropertyItemHandler<WebUserPtr> propertyHandler;

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_PERMISSIONS,
			PROP_ACTIVE_SESSIONS,
			PROP_LAST_LOGIN,
			PROP_LAST
		};

		static json serializeItem(const WebUserPtr& aItem, int aPropertyName) noexcept;
		static bool filterItem(const WebUserPtr& aItem, int aPropertyName, const StringMatch& aTextMatcher, double aNumericMatcher) noexcept;

		static int compareItems(const WebUserPtr& a, const WebUserPtr& b, int aPropertyName) noexcept;
		static std::string getStringInfo(const WebUserPtr& a, int aPropertyName) noexcept;
		static double getNumericInfo(const WebUserPtr& a, int aPropertyName) noexcept;
	};
}

#endif