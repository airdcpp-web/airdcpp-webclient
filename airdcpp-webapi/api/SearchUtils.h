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

#ifndef DCPLUSPLUS_DCPP_SEARCHUTILS_H
#define DCPLUSPLUS_DCPP_SEARCHUTILS_H

#include <api/SearchResultInfo.h>
#include <api/common/Property.h>

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>


namespace webserver {
	class SearchUtils {
	public:
		static const PropertyList properties;
		static const PropertyItemHandler<SearchResultInfoPtr> propertyHandler;

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

		static json serializeResult(const SearchResultInfoPtr& aResult, int aPropertyName) noexcept;

		static int compareResults(const SearchResultInfoPtr& a, const SearchResultInfoPtr& b, int aPropertyName) noexcept;
		static std::string getStringInfo(const SearchResultInfoPtr& a, int aPropertyName) noexcept;
		static double getNumericInfo(const SearchResultInfoPtr& a, int aPropertyName) noexcept;
	};
}

#endif