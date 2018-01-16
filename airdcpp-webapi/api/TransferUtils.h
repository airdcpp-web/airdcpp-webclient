/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_TRANSFERUTILS_H
#define DCPLUSPLUS_DCPP_TRANSFERUTILS_H

#include <web-server/stdinc.h>

#include <api/TransferInfo.h>
#include <api/common/Property.h>

namespace webserver {
	class TransferUtils {
	public:
		static const PropertyList properties;
		static const PropertyItemHandler<TransferInfoPtr> propertyHandler;

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_TARGET,
			PROP_TYPE,
			PROP_DOWNLOAD,
			PROP_SIZE,
			PROP_STATUS,
			PROP_BYTES_TRANSFERRED,
			PROP_USER,
			PROP_TIME_STARTED,
			PROP_SPEED,
			PROP_SECONDS_LEFT,
			PROP_IP,
			PROP_FLAGS,
			PROP_ENCRYPTION,
			PROP_QUEUE_ID,
			PROP_LAST
		};

		static json serializeProperty(const TransferInfoPtr& aItem, int aPropertyName) noexcept;

		static int compareItems(const TransferInfoPtr& a, const TransferInfoPtr& b, int aPropertyName) noexcept;

		static std::string getStringInfo(const TransferInfoPtr& aItem, int aPropertyName) noexcept;
		static double getNumericInfo(const TransferInfoPtr& aItem, int aPropertyName) noexcept;

	private:

	};
}

#endif