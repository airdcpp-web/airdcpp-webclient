/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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
#include "Mapper_NATPMP.h"

#include "Util.h"

extern "C" {
#ifndef STATICLIB
#define STATICLIB
#endif
#include <../natpmp/getgateway.h>
#include <../natpmp/natpmp.h>
}

///@todo should bind to the local IP

namespace dcpp {

const string Mapper_NATPMP::name = "NAT-PMP";

static natpmp_t nat;

Mapper_NATPMP::Mapper_NATPMP(string&& localIp) :
Mapper(move(localIp)),
lifetime(0)
{
}

bool Mapper_NATPMP::init() {
	// the lib normally handles this but we call it manually to store the result (IP of the router).
	in_addr addr;
	if(getdefaultgateway(reinterpret_cast<in_addr_t*>(&addr.s_addr)) < 0)
		return false;
	gateway = inet_ntoa(addr);

	return initnatpmp(&nat, 1, addr.s_addr) >= 0;
}

void Mapper_NATPMP::uninit() {
	closenatpmp(&nat);
}

namespace {

int reqType(Mapper::Protocol protocol) {
	if (protocol == Mapper::PROTOCOL_TCP) {
		return NATPMP_PROTOCOL_TCP;
	} else {
		dcassert(protocol == Mapper::PROTOCOL_UDP);
		return NATPMP_PROTOCOL_UDP;
	}
}

int respType(Mapper::Protocol protocol) {
	if (protocol == Mapper::PROTOCOL_TCP) {
		return NATPMP_RESPTYPE_TCPPORTMAPPING;
	} else {
		dcassert(protocol == Mapper::PROTOCOL_UDP);
		return  NATPMP_RESPTYPE_UDPPORTMAPPING;
	}
}

bool sendRequest(uint16_t port, Mapper::Protocol protocol, uint32_t lifetime) {
	return sendnewportmappingrequest(&nat, reqType(protocol), port, port, lifetime) >= 0;
}

bool read(natpmpresp_t& response) {
	int res;
	do {
		// wait for the previous request to complete.
		timeval timeout;
		if(getnatpmprequesttimeout(&nat, &timeout) >= 0) {
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(nat.s, &fds);
			select(FD_SETSIZE, &fds, 0, 0, &timeout);
		}

		res = readnatpmpresponseorretry(&nat, &response);
	} while(res == NATPMP_TRYAGAIN && nat.try_number <= 5); // don't wait for 9 failures as that takes too long.
	return res >= 0;
}

} // unnamed namespace

bool Mapper_NATPMP::add(const string& port, const Protocol protocol, const string&) {
	auto port_ = Util::toInt(port);
	if(sendRequest(port_, protocol, 3600)) {
		natpmpresp_t response;
		if(read(response) && response.type == respType(protocol) && response.pnu.newportmapping.mappedpublicport == port_) {
			lifetime = std::min(3600u, response.pnu.newportmapping.lifetime) / 60;
			return true;
		}
	}
	return false;
}

bool Mapper_NATPMP::remove(const string& port, const Protocol protocol) {
	auto port_ = Util::toInt(port);
	if(sendRequest(port_, protocol, 0)) {
		natpmpresp_t response;
		return read(response) && response.type == respType(protocol) && response.pnu.newportmapping.mappedpublicport == port_;
	}
	return false;
}

string Mapper_NATPMP::getDeviceName() {
	return gateway; // in lack of the router's name, give its IP.
}

string Mapper_NATPMP::getExternalIP() {
	if(sendpublicaddressrequest(&nat) >= 0) {
		natpmpresp_t response;
		if(read(response) && response.type == NATPMP_RESPTYPE_PUBLICADDRESS) {
			return inet_ntoa(response.pnu.publicaddress.addr);
		}
	}
	return Util::emptyString;
}

} // dcpp namespace
