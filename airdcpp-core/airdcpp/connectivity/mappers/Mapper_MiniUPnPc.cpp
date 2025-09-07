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

#include "stdinc.h"

#include "Mapper_MiniUPnPc.h"

#include <airdcpp/util/LinkUtil.h>
#include <airdcpp/util/NetworkUtil.h>

#include <airdcpp/util/Util.h>
#include <airdcpp/connection/socket/Socket.h>

extern "C" {
#ifndef MINIUPNP_STATICLIB
#define MINIUPNP_STATICLIB
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
#ifndef _WIN32
	// not implemented
	return true;
#else
	if (!v6) { 
		uint32_t mmask = (~0u) << (32-mask);
		uint32_t result1 = IPToUInt(aIP1) & mmask;
		uint32_t result2 = IPToUInt(aIP2) & mmask;

		return result1 == result2;
	} else {
		if (mask & 16)
			return false;

		in6_addr addr1, addr2;

		auto p = aIP1.find('%');
		inet_pton(AF_INET6, (p != string::npos ? aIP1.substr(0, p) : aIP1).c_str(), &addr1);

		p = aIP2.find('%');
		inet_pton(AF_INET6, (p != string::npos ? aIP2.substr(0, p) : aIP2).c_str(), &addr2);

		//reset the non-common bytes
		int resetPos = 16-((128-mask) / 16);
		for (int i = resetPos; i < 16; ++i) {
			addr1.u.Byte[i] = 0;
			addr2.u.Byte[i] = 0;
		}

		return memcmp(addr1.u.Byte, addr2.u.Byte, 16) == 0;
	}
#endif
}

bool Mapper_MiniUPnPc::init() {
	if(!url.empty())
		return true;

#if MINIUPNPC_API_VERSION < 14
	UPNPDev* devices = upnpDiscover(2000, localIp.empty() ? nullptr : localIp.c_str(), 0, 0, v6, 0);
#else
	UPNPDev* devices = upnpDiscover(2000, localIp.empty() ? nullptr : localIp.c_str(), 0, 0, v6, 2, 0);
#endif
	if(!devices)
		return false;

	UPNPUrls urls;
	IGDdatas data;

#if (MINIUPNPC_API_VERSION >= 18)
	auto ret = UPNP_GetValidIGD(devices, &urls, &data, 0, 0, 0, 0);
#else
	auto ret = UPNP_GetValidIGD(devices, &urls, &data, 0, 0);
#endif


	bool ok = ret == 1;
	if(ok) {
		if (localIp.empty()) {
			// We have no bind address configured in settings
			// Try to avoid choosing a random adapter for port mapping

			// Parse router IP from the control URL address
			auto controlUrl = string(string(data.urlbase).empty() ? urls.controlURL : data.urlbase);
			updateLocalIp(controlUrl);
		}

		url = urls.controlURL;
		service = data.first.servicetype;
		device = localIp.empty() ? "Generic" : localIp;
	}

	freeUPNPDevlist(devices);

	if (ret) {
		FreeUPNPUrls(&urls);
	}

	return ok;
}

void Mapper_MiniUPnPc::updateLocalIp(const string& aControlUrl) noexcept {
	string routerIp, portTmp, protoTmp, pathTmp, queryTmp, fragmentTmp;
	LinkUtil::decodeUrl(aControlUrl, protoTmp, routerIp, portTmp, pathTmp, queryTmp, fragmentTmp);

	routerIp = Socket::resolve(routerIp, v6 ? AF_INET6 : AF_INET);
	if (!routerIp.empty()) {
		auto adapters = NetworkUtil::getNetworkAdapters(v6);

		// Find a local IP that is within the same subnet
		auto p = ranges::find_if(adapters, [&routerIp, this](const AdapterInfo& aInfo) { 
			return isIPInRange(aInfo.ip, routerIp, aInfo.prefix, v6); 
		});
		if (p != adapters.end()) {
			localIp = p->ip;
		}
	}
}

void Mapper_MiniUPnPc::uninit() {
}

bool Mapper_MiniUPnPc::add(const string& port, const Protocol protocol, const string& description) {
	return UPNP_AddPortMapping(url.c_str(), service.c_str(), port.c_str(), port.c_str(),
		localIp.c_str(), description.c_str(), protocols[protocol], 0, 0) == UPNPCOMMAND_SUCCESS;
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
