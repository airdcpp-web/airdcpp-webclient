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

#ifndef DCPLUSPLUS_DCPP_ADDRESSINFO_H
#define DCPLUSPLUS_DCPP_ADDRESSINFO_H

#include <string>

namespace dcpp {

using std::string;

class AddressInfo {
public:
	enum Type {
		TYPE_V4,
		TYPE_V6,
		TYPE_URL,
		TYPE_DUAL
	};

	AddressInfo(const string& aIP, Type aType) : type(aType) {
		ip[aType] = aIP;
	}

	AddressInfo(const string& aV4, const string& aV6) {
		ip[TYPE_V4] = aV4;
		ip[TYPE_V6] = aV6;
		type = TYPE_DUAL;
	}

	bool hasV6CompatibleAddress() const noexcept {
		return type != TYPE_V4;
	}

	bool hasV4CompatibleAddress() const noexcept {
		return type != TYPE_V6;
	}

	string getV6CompatibleAddress() const noexcept {
		if (type == TYPE_DUAL)
			return ip[TYPE_V6];

		return ip[type];
	}

	string getV4CompatibleAddress() const noexcept {
		if (type == TYPE_DUAL)
			return ip[TYPE_V4];

		return ip[type];
	}

	Type getType() const noexcept {
		return type;
	}
private:
	Type type;
	string ip[TYPE_DUAL];
};

enum class NatRole {
	NONE,
	CLIENT,
	SERVER
};

struct SocketConnectOptions {
	SocketConnectOptions(const string& aPort, bool aSecure, NatRole aNatRole = NatRole::NONE) : port(aPort), natRole(aNatRole), secure(aSecure) {}

	string port;
	NatRole natRole;
	bool secure;
};


} // namespace dcpp

#endif // !defined(SOCKET_H)
