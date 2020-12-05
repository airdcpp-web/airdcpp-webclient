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

#ifndef DCPLUSPLUS_DCPP_FILESEARCH_PARSER_H
#define DCPLUSPLUS_DCPP_FILESEARCH_PARSER_H

#include <airdcpp/typedefs.h>
#include <airdcpp/Search.h>

namespace webserver {
	class FileSearchParser {
	public:
		static SearchPtr parseSearch(const json& aJson, bool aIsDirectSearch, const string& aToken);

		static string parseSearchType(const string& aType);
		static string serializeSearchType(const string& aType);
	private:
		static void parseMatcher(const json& aJson, const SearchPtr& aSearch);
		static void parseOptions(const json& aJson, const SearchPtr& aSearch);

		static Search::MatchType parseMatchType(const string& aTypeStr);
	};
}

#endif