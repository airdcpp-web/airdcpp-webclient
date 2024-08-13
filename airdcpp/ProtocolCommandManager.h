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

#ifndef DCPLUSPLUS_DCPP_DEBUGMANAGER_H
#define DCPLUSPLUS_DCPP_DEBUGMANAGER_H

#include "Speaker.h"
#include "Singleton.h"

namespace dcpp {

class ProtocolCommandManagerListener {
public:
template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> DebugCommand;

	typedef X<1> IncomingHubCommand;
	typedef X<2> IncomingUDPCommand;
	typedef X<3> IncomingTCPCommand;

	typedef X<4> OutgoingHubCommand;
	typedef X<5> OutgoingUDPCommand;
	typedef X<6> OutgoingTCPCommand;

	virtual void on(DebugCommand, const string&, uint8_t, uint8_t, const string&) noexcept { }

	virtual void on(IncomingHubCommand, const AdcCommand&, const Client&) noexcept { }
	virtual void on(IncomingUDPCommand, const AdcCommand&, const string&) noexcept { }
	virtual void on(IncomingTCPCommand, const AdcCommand&, const string&, const UserPtr&) noexcept { }

	virtual void on(OutgoingHubCommand, const AdcCommand&, const Client&) noexcept { }
	virtual void on(OutgoingUDPCommand, const AdcCommand&, const string&, const OnlineUserPtr&) noexcept { }
	virtual void on(OutgoingTCPCommand, const AdcCommand&, const string&, const UserPtr&) noexcept { }
};

class ProtocolCommandManager : public Singleton<ProtocolCommandManager>, public Speaker<ProtocolCommandManagerListener>
{
	friend class Singleton<ProtocolCommandManager>;
	ProtocolCommandManager() { };
public:
	void SendCommandMessage(const string& aMess, uint8_t aType, uint8_t aDirection, const string& aIP) {
		fire(ProtocolCommandManagerListener::DebugCommand(), aMess, aType, aDirection, aIP);
	}

	~ProtocolCommandManager() { };
	enum Type {
		TYPE_HUB, TYPE_CLIENT, TYPE_CLIENT_UDP
	};

	enum Direction {
		INCOMING, OUTGOING
	};
};
#define COMMAND_DEBUG(a,b,c,d) ProtocolCommandManager::getInstance()->SendCommandMessage(a,b,c,d);

} // namespace dcpp

#endif
