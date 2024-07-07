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

#ifndef DCPLUSPLUS_DCPP_SEARCHUTILS_H
#define DCPLUSPLUS_DCPP_SEARCHUTILS_H

#include <api/common/Property.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/GroupedSearchResult.h>


namespace webserver {
	class SearchUtils {
	public:
		static const PropertyList properties;
		static const PropertyItemHandler<GroupedSearchResultPtr> propertyHandler;

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_RELEVANCE,
			PROP_HITS,
			PROP_USERS,
			PROP_TYPE,
			PROP_SIZE,
			PROP_DATE,
			PROP_PATH,
			PROP_CONNECTION,
			PROP_SLOTS,
			PROP_TTH,
			PROP_DUPE,
			PROP_LAST
		};

		static json serializeResult(const GroupedSearchResultPtr& aResult, int aPropertyName) noexcept;

		static int compareResults(const GroupedSearchResultPtr& a, const GroupedSearchResultPtr& b, int aPropertyName) noexcept;
		static std::string getStringInfo(const GroupedSearchResultPtr& a, int aPropertyName) noexcept;
		static double getNumericInfo(const GroupedSearchResultPtr& a, int aPropertyName) noexcept;
	};
}

#endif