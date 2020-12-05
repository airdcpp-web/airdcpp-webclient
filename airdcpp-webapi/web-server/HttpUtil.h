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

#ifndef DCPLUSPLUS_DCPP_HTTPUTIL_H
#define DCPLUSPLUS_DCPP_HTTPUTIL_H

#include "stdinc.h"

#include <airdcpp/typedefs.h>


namespace webserver {
	class HttpUtil {
	public:
		static const char* getMimeType(const string& aFileName) noexcept;
		static bool unespaceUrl(const std::string& in, std::string& out) noexcept;
		static string getExtension(const string& aResource) noexcept;

		// Parses start and end position from a range HTTP request field
		// Initial value of end_ should be the file size
		// Returns true if the partial range was parsed successfully
		static bool parsePartialRange(const string& aHeaderData, int64_t& start_, int64_t& end_) noexcept;

		static string formatPartialRange(int64_t aStart, int64_t aEnd, int64_t aFileSize) noexcept;

		static void addCacheControlHeader(StringPairList& headers_, int aDaysValid) noexcept;

		static bool isStatusOk(int aCode) noexcept;
		static bool parseStatus(const string& aResponse, int& code_, string& text_) noexcept;
		static string parseAuthToken(const websocketpp::http::parser::request& aRequest) noexcept;
	};
}

#endif