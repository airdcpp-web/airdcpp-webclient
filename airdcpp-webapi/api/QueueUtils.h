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

#ifndef DCPLUSPLUS_DCPP_QUEUEUTILS_H
#define DCPLUSPLUS_DCPP_QUEUEUTILS_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/QueueItemBase.h>

namespace webserver {
	class QueueUtils {
	public:
		static BundleList getBundleList() noexcept;

		static json serializeBundleProperty(const BundlePtr& aBundle, int aPropertyName) noexcept;
		//static json serializeBundle(const BundlePtr& aBundle) noexcept;
		//static json serializeQueueItem(const QueueItemPtr& aQI) noexcept;
		//static json serializeQueueItemBase(const QueueItemBase& aItem) noexcept;
		static json serializePriority(const QueueItemBase& aItem) noexcept;

		static int compareBundles(const BundlePtr& a, const BundlePtr& b, int aPropertyName) noexcept;

		static std::string getStringInfo(const BundlePtr& a, int aPropertyName) noexcept;
		static double getNumericInfo(const BundlePtr& a, int aPropertyName) noexcept;

	private:
		static void getBundleSourceInfo(const BundlePtr& aBundle, int& online_, int& total_, string& str_) noexcept;

		static std::string formatBundleStatus(const BundlePtr& aBundle) noexcept;
		static std::string formatBundleSources(const BundlePtr& aBundle) noexcept;
		static std::string formatBundleType(const BundlePtr& aBundle) noexcept;
	};
}

#endif