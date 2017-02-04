/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_QUEUE_FILE_UTILS_H
#define DCPLUSPLUS_DCPP_QUEUE_FILE_UTILS_H

#include <web-server/stdinc.h>

#include <api/common/Property.h>

#include <airdcpp/typedefs.h>


namespace webserver {
	class QueueFileUtils {
	public:
		static const PropertyList properties;

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
			PROP_BUNDLE,
			PROP_TTH,
			PROP_LAST
		};

		static const PropertyItemHandler<QueueItemPtr> propertyHandler;

		static json serializeFileProperty(const QueueItemPtr& aItem, int aPropertyName) noexcept;

		static int compareFiles(const QueueItemPtr& a, const QueueItemPtr& b, int aPropertyName) noexcept;

		static std::string getStringInfo(const QueueItemPtr& a, int aPropertyName) noexcept;
		static double getNumericInfo(const QueueItemPtr& a, int aPropertyName) noexcept;

	private:
		static string getDisplayName(const QueueItemPtr& aItem) noexcept;

		static std::string formatDisplayStatus(const QueueItemPtr& aItem) noexcept;
		static std::string formatFileSources(const QueueItemPtr& aItem) noexcept;
	};
}

#endif