/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"
#include "Mapper_MiniUPnPc.h"
#include "AirUtil.h"

#include "Util.h"
#include "Socket.h"

extern "C" {
#ifndef STATICLIB
#define STATICLIB
#endif
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
}

namespace dcpp {

const string Mapper_MiniUPnPc::name = "MiniUPnP";

Mapper_MiniUPnPc::Mapper_MiniUPnPc(const string& localIp, bool v6) :
Mapper(localIp, v6)
{
}

bool Mapper_MiniUPnPc::supportsProtocol(bool /*v6*/) const {
	return true;
}

uint32_t IPToUInt(const string& ip) {
    int a, b, c, d;
    uint32_t addr = 0;
 
    if (sscanf(ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
        return 0;
	
	addr = a << 24;
	addr |= b << 16;
	addr |= c << 8;
    addr |= d;
    return addr;
}

bool isIPInRange(const string& aIP1, const string& aIP2, uint8_t mask, bool v6) {
	if (!v6) { 
		uint32_t mmask = (~0u) << (32-mask);
		uint32_t result1 = IPToUInt(aIP1) & mmask;
		uint32_t result2 = IPToUInt(aIP2) & mmask;

		return result1 == result2;
	} else {
		if (mask & 16)
			return false;

		in6_addr addr1, addr2;

		auto p = aIP1.find("%");
		inet_pton(AF_INET6, (p != string::npos ? aIP1.substr(0, p) : aIP1).c_str(), &addr1);

		p = aIP2.find("%");
		inet_pton(AF_INET6, (p != string::npos ? aIP2.substr(0, p) : aIP2).c_str(), &addr2);

#ifdef _WIN32
		//reset the non-common bytes
		int resetPos = 16-((128-mask) / 16);
		for (int i = resetPos; i < 16; ++i) {
			addr1.u.Byte[i] = 0;
			addr2.u.Byte[i] = 0;
		}

		return memcmp(addr1.u.Byte, addr2.u.Byte, 16) == 0;
#else
		return true;
#endif
	}
}

bool Mapper_MiniUPnPc::init() {
	if(!url.empty())
		return true;

#ifdef HAVE_OLD_MINIUPNPC
	UPNPDev* devices = upnpDiscover(2000, localIp.empty() ? nullptr : localIp.c_str(), 0, 0);
#else
	UPNPDev* devices = upnpDiscover(2000, localIp.empty() ? nullptr : localIp.c_str(), 0, 0, v6, 0);
#endif
	if(!devices)
		return false;

	UPNPUrls urls;
	IGDdatas data;

	auto ret = UPNP_GetValidIGD(devices, &urls, &data, 0, 0);

	bool ok = ret == 1;
	if(ok) {
		if (localIp.empty()) {
			AirUtil::IpList addresses;
			AirUtil::getIpAddresses(addresses, v6);
	
			auto remoteIP = string(string(data.urlbase).empty() ?  urls.controlURL : data.urlbase);
			auto start = remoteIP.find("//");
			if (start != string::npos) {
				start = start+2;
				auto end = remoteIP.find(":", start);
				if (end != string::npos) {
					remoteIP = Socket::resolve(remoteIP.substr(start, end-start), v6 ? AF_INET6 : AF_INET);
					if (!remoteIP.empty()) {
						auto p = boost::find_if(addresses, [&remoteIP, this](const AirUtil::AddressInfo& aInfo) { return isIPInRange(aInfo.ip, remoteIP, aInfo.prefix, v6); });
						if (p != addresses.end()) {
							localIp = p->ip;
						}
					}
				}
			}
		}

		url = urls.controlURL;
		service = data.first.servicetype;

#ifdef _WIN32
		device = data.CIF.friendlyName;
#else
		// Doesn't work on Linux
		device = "Generic";
#endif
	}

	if(ret) {
		FreeUPNPUrls(&urls);
		freeUPNPDevlist(devices);
	}

	return ok;
}

void Mapper_MiniUPnPc::uninit() {
}

bool Mapper_MiniUPnPc::add(const string& port, const Protocol protocol, const string& description) {
#ifdef HAVE_OLD_MINIUPNPC
	return UPNP_AddPortMapping(url.c_str(), service.c_str(), port.c_str(), port.c_str(),
		localIp.c_str(), description.c_str(), protocols[protocol], 0) == UPNPCOMMAND_SUCCESS;
#else
	return UPNP_AddPortMapping(url.c_str(), service.c_str(), port.c_str(), port.c_str(),
		localIp.c_str(), description.c_str(), protocols[protocol], 0, 0) == UPNPCOMMAND_SUCCESS;
#endif
}

bool Mapper_MiniUPnPc::remove(const string& port, const Protocol protocol) {
	return UPNP_DeletePortMapping(url.c_str(), service.c_str(), port.c_str(), protocols[protocol], 0) == UPNPCOMMAND_SUCCESS;
}

string Mapper_MiniUPnPc::getDeviceName() {
	return device;
}

string Mapper_MiniUPnPc::getExternalIP() {
	char buf[16] = { 0 };
	if(UPNP_GetExternalIPAddress(url.c_str(), service.c_str(), buf) == UPNPCOMMAND_SUCCESS)
		return buf;
	return Util::emptyString;
}

} // dcpp namespace
