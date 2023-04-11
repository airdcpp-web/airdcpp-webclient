/*
* Copyright (C) 2011-2023 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_ONLINEUSER_UTILS_H
#define DCPLUSPLUS_DCPP_ONLINEUSER_UTILS_H

#include "stdinc.h"

#include <api/common/Property.h>

#include <airdcpp/typedefs.h>


namespace webserver {
	class OnlineUserUtils {
	public:
		static const PropertyList properties;
		static const PropertyItemHandler<OnlineUserPtr> propertyHandler;

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NICK,
			PROP_SHARED,
			PROP_DESCRIPTION,
			PROP_TAG,
			PROP_UPLOAD_SPEED,
			PROP_DOWNLOAD_SPEED,
			PROP_IP4,
			PROP_IP6,
			PROP_EMAIL,
			PROP_FILES,
			PROP_HUB_ID,
			PROP_HUB_URL,
			PROP_HUB_NAME,
			PROP_FLAGS,
			PROP_CID,
			PROP_UPLOAD_SLOTS,
			PROP_LAST
		};

		static json serializeUser(const OnlineUserPtr& aUser, int aPropertyName) noexcept;

		static int compareUsers(const OnlineUserPtr& a, const OnlineUserPtr& b, int aPropertyName) noexcept;
		static std::string getStringInfo(const OnlineUserPtr& a, int aPropertyName) noexcept;
		static double getNumericInfo(const OnlineUserPtr& a, int aPropertyName) noexcept;
	};
}

#endif