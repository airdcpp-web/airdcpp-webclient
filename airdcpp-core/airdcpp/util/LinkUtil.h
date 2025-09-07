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

#ifndef DCPLUSPLUS_DCPP_LINKUTIL_H
#define DCPLUSPLUS_DCPP_LINKUTIL_H

#include <airdcpp/core/header/compiler.h>

#include <airdcpp/core/types/Priority.h>
#include <airdcpp/settings/SettingsManager.h>

namespace dcpp {

class LinkUtil {
	
public:
	static boost::regex urlReg;

	static const string getUrlReg() noexcept;

	static bool isAdcHub(const string& aHubUrl) noexcept;
	static bool isSecure(const string& aHubUrl) noexcept;
	static bool isHubLink(const string& aHubUrl) noexcept;

	static string parseLink(const string& aLink) noexcept;
	static void sanitizeUrl(string& url) noexcept;
	static void decodeUrl(const string& aUrl, string& protocol, string& host, string& port, string& path, string& query, string& fragment) noexcept;
	static string encodeURI(const string& aString, bool reverse = false) noexcept;

	static map<string, string> decodeQuery(const string& query) noexcept;
};

}
#endif