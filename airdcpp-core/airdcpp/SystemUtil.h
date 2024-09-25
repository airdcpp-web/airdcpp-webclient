/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SYSTEM_UTIL_H
#define DCPLUSPLUS_DCPP_SYSTEM_UTIL_H

#include <airdcpp/compiler.h>
#include <airdcpp/constants.h>
#include <airdcpp/typedefs.h>

namespace dcpp {

class SystemUtil  
{
public:
	static string getOsVersion(bool http = false) noexcept;
	static bool isOSVersionOrGreater(int major, int minor) noexcept;

	// Execute a background process and get exit code
	static int runSystemCommand(const string& aCommand) noexcept;

	static string getSystemUsername() noexcept;

	static string translateError(int aError) noexcept;
	static string formatLastError() noexcept;
private:
	static int osMinor;
	static int osMajor;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SYSTEM_UTIL_H)
