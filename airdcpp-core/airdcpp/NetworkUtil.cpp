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

#include <airdcpp/NetworkUtil.h>

#include <airdcpp/ConnectivityManager.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/Util.h>

#ifdef _WIN32

#include <IPHlpApi.h>
#pragma comment(lib, "iphlpapi.lib")

#endif


#ifndef _WIN32
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#include <net/if.h>
#endif

#endif


namespace dcpp {

bool NetworkUtil::isLocalIp(const string& ip, bool v6) noexcept {
	if (v6) {
		return (ip.length() > 4 && ip.starts_with("fe80")) || ip == "::1";
	}

	return (ip.length() > 3 && strncmp(ip.c_str(), "169", 3) == 0) || ip == "127.0.0.1";
}

bool NetworkUtil::isPrivateIp(const string& ip, bool v6) noexcept {
	if (v6) {
		// https://en.wikipedia.org/wiki/Unique_local_address
		return ip.length() > 2 && ip.starts_with("fd");
	} else {
		dcassert(ip.length() <= INET_ADDRSTRLEN);
		struct in_addr addr;

		inet_pton(AF_INET, ip.c_str(), &addr.s_addr);

		if (addr.s_addr  != INADDR_NONE) {
			unsigned long haddr = ntohl(addr.s_addr);
			auto result = ((haddr & 0xff000000) == 0x0a000000 || // 10.0.0.0/8
					(haddr & 0xfff00000) == 0xac100000 || // 172.16.0.0/12
					(haddr & 0xffff0000) == 0xc0a80000);  // 192.168.0.0/16
			return result;
		}
	}
	return false;
}

bool NetworkUtil::isPublicIp(const string& ip, bool v6) noexcept {
	return !ip.empty() && !isLocalIp(ip, v6) && !isPrivateIp(ip, v6);
}

AdapterInfoList NetworkUtil::getCoreBindAdapters(bool v6) {
	// Get the addresses and sort them
	auto bindAddresses = getNetworkAdapters(v6);
	sort(bindAddresses.begin(), bindAddresses.end(), adapterSort);

	// "Any" adapter
	bindAddresses.emplace(bindAddresses.begin(), STRING(ANY), v6 ? "::" : "0.0.0.0", static_cast<uint8_t>(0));

	// Current address not listed?
	const auto& setting = v6 ? SETTING(BIND_ADDRESS6) : SETTING(BIND_ADDRESS);
	ensureBindAddress(bindAddresses, setting);

	return bindAddresses;
}

int NetworkUtil::adapterSort(const AdapterInfo& lhs, const AdapterInfo& rhs) noexcept {
	if (lhs.adapterName.empty() && rhs.adapterName.empty()) {
		return Util::stricmp(lhs.ip, rhs.ip) < 0;
	}

	return Util::stricmp(lhs.adapterName, rhs.adapterName) < 0;
}

void NetworkUtil::ensureBindAddress(AdapterInfoList& adapters_, const string& aBindAddress) noexcept {
	auto cur = ranges::find_if(adapters_, [&aBindAddress](const AdapterInfo& aInfo) { return aInfo.ip == aBindAddress; });
	if (cur == adapters_.end()) {
		adapters_.emplace_back(STRING(UNKNOWN), aBindAddress, static_cast<uint8_t>(0));
		cur = adapters_.end() - 1;
	}
}

AdapterInfoList NetworkUtil::getNetworkAdapters(bool v6) {
	AdapterInfoList adapterInfos;

#ifdef _WIN32
	ULONG len = 15360; //"The recommended method of calling the GetAdaptersAddresses function is to pre-allocate a 15KB working buffer pointed to by the AdapterAddresses parameter"
	for (int i = 0; i < 3; ++i)
	{
		PIP_ADAPTER_ADDRESSES adapterInfo = (PIP_ADAPTER_ADDRESSES)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
		ULONG ret = GetAdaptersAddresses(v6 ? AF_INET6 : AF_INET, GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, adapterInfo, &len);
		bool freeObject = true;

		if (ret == ERROR_SUCCESS)
		{
			for (PIP_ADAPTER_ADDRESSES pAdapterInfo = adapterInfo; pAdapterInfo != NULL; pAdapterInfo = pAdapterInfo->Next)
			{
				// we want only enabled ethernet interfaces
				if (pAdapterInfo->OperStatus == IfOperStatusUp && (pAdapterInfo->IfType == IF_TYPE_ETHERNET_CSMACD || pAdapterInfo->IfType == IF_TYPE_IEEE80211))
				{
					PIP_ADAPTER_UNICAST_ADDRESS ua;
					for (ua = pAdapterInfo->FirstUnicastAddress; ua != NULL; ua = ua->Next) {
						//get the name of the adapter
						char buf[BUFSIZ];
						memset(buf, 0, BUFSIZ);
						getnameinfo(ua->Address.lpSockaddr, ua->Address.iSockaddrLength, buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);

						//is it a local address?
						/*SOCKADDR_IN6* pAddr = (SOCKADDR_IN6*) ua->Address.lpSockaddr;
						BYTE prefix[8] = { 0xFE, 0x80 };
						auto fLinkLocal = (memcmp(pAddr->sin6_addr.u.Byte, prefix, sizeof(prefix)) == 0);*/

						adapterInfos.emplace_back(Text::fromT(tstring(pAdapterInfo->FriendlyName)), buf, ua->OnLinkPrefixLength);
					}
					freeObject = false;
				}
			}
		}

		if (freeObject)
			HeapFree(GetProcessHeap(), 0, adapterInfo);

		if (ret != ERROR_BUFFER_OVERFLOW)
			break;
	}
#else

#ifdef HAVE_IFADDRS_H
	struct ifaddrs* ifap;

	if (getifaddrs(&ifap) == 0) {
		for (struct ifaddrs* i = ifap; i != NULL; i = i->ifa_next) {
			struct sockaddr* sa = i->ifa_addr;

			// If the interface is up, is not a loopback and it has an address
			if ((i->ifa_flags & IFF_UP) && !(i->ifa_flags & IFF_LOOPBACK) && sa != NULL) {
				void* src = nullptr;
				socklen_t len;

				if (!v6 && sa->sa_family == AF_INET) {
					// IPv4 address
					struct sockaddr_in* sai = (struct sockaddr_in*)sa;
					src = (void*)&(sai->sin_addr);
					len = INET_ADDRSTRLEN;
				} else if (v6 && sa->sa_family == AF_INET6) {
					// IPv6 address
					struct sockaddr_in6* sai6 = (struct sockaddr_in6*)sa;
					src = (void*)&(sai6->sin6_addr);
					len = INET6_ADDRSTRLEN;
				}

				// Convert the binary address to a string and add it to the output list
				if (src) {
					char address[len];
					inet_ntop(sa->sa_family, src, address, len);
					// TODO: get the prefix
					adapterInfos.emplace_back("Unknown", (string)address, 0);
				}
			}
		}
		freeifaddrs(ifap);
	}
#endif

#endif

	return adapterInfos;
}

string NetworkUtil::getLocalIp(bool v6) noexcept {
	const auto& bindAddr = v6 ? CONNSETTING(BIND_ADDRESS6) : CONNSETTING(BIND_ADDRESS);
	if (!bindAddr.empty() && bindAddr != SettingsManager::getInstance()->getDefault(v6 ? SettingsManager::BIND_ADDRESS6 : SettingsManager::BIND_ADDRESS)) {
		return bindAddr;
	}

	// No bind address configured, try to find a public address
	auto adapters = getNetworkAdapters(v6);
	if (adapters.empty()) {
		return Util::emptyString;
	}

	auto p = ranges::find_if(adapters, [v6](const AdapterInfo& aAdapterInfo) { return isPublicIp(aAdapterInfo.ip, v6); });
	if (p != adapters.end()) {
		return p->ip;
	}

	return adapters.front().ip;
}

} // namespace dcpp