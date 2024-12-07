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

#ifndef DCPLUSPLUS_DCPP_NETWORK_UTIL_H
#define DCPLUSPLUS_DCPP_NETWORK_UTIL_H

#include <airdcpp/core/header/compiler.h>
#include <airdcpp/core/header/constants.h>
#include <airdcpp/core/header/typedefs.h>

namespace dcpp {

struct AdapterInfo {
	AdapterInfo(const string& aName, const string& aIP, uint8_t aPrefix) : adapterName(aName), ip(aIP), prefix(aPrefix) { }

	string adapterName;
	string ip;
	uint8_t prefix;
};

using AdapterInfoList = vector<AdapterInfo>;


class NetworkUtil  
{
public:
	// Return whether the IP is localhost or a link-local address (169.254.0.0/16 or fe80)
	static bool isLocalIp(const string& ip, bool v6) noexcept;

	// Returns whether the IP is a private one (non-local)
	//
	// Private ranges:
	// IPv4: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16
	// IPv6: fd prefix
	static bool isPrivateIp(const string& ip, bool v6) noexcept;

	static bool isPublicIp(const string& ip, bool v6) noexcept;


	// Get a list of network adapters for the wanted protocol
	static AdapterInfoList getNetworkAdapters(bool v6);

	// Get a sorted list of available bind adapters for the wanted protocol
	// Ensures that the current bind address is listed as well
	static AdapterInfoList getCoreBindAdapters(bool v6);

	static void ensureBindAddress(AdapterInfoList& adapters_, const string& aBindAddress) noexcept;
	static int adapterSort(const AdapterInfo& a, const AdapterInfo& b) noexcept;

	// Get current bind address
	// The best adapter address is returned if no bind address is configured
	// (public addresses are preferred over local/private ones)
	static string getLocalIp(bool v6) noexcept;
};

} // namespace dcpp

#endif // !defined(UTIL_H)
