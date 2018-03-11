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

#ifndef DCPLUSPLUS_DCPP_QUEUE_BUNDLE_UTILS_H
#define DCPLUSPLUS_DCPP_QUEUE_BUNDLE_UTILS_H

#include <api/common/Property.h>

#include <airdcpp/typedefs.h>


namespace webserver {
	class QueueBundleUtils {
	public:
		static const PropertyList properties;
		static const PropertyItemHandler<BundlePtr> propertyHandler;

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_TARGET,
			PROP_TYPE,
			PROP_SIZE,
			PROP_STATUS,
			PROP_BYTES_DOWNLOADED,
			PROP_PRIORITY,
			PROP_TIME_ADDED,
			PROP_TIME_FINISHED,
			PROP_SPEED,
			PROP_SECONDS_LEFT,
			PROP_SOURCES,
			PROP_LAST
		};

		static json serializeBundleProperty(const BundlePtr& aBundle, int aPropertyName) noexcept;

		static int compareBundles(const BundlePtr& a, const BundlePtr& b, int aPropertyName) noexcept;

		static std::string getStringInfo(const BundlePtr& a, int aPropertyName) noexcept;
		static double getNumericInfo(const BundlePtr& a, int aPropertyName) noexcept;

	private:
		static std::string formatStatusId(const BundlePtr& aBundle) noexcept;
		static std::string formatBundleSources(const BundlePtr& aBundle) noexcept;
		static std::string formatBundleType(const BundlePtr& aBundle) noexcept;
	};
}

#endif