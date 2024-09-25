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

#ifndef DCPLUSPLUS_DCPP_UDP_SERVER_H
#define DCPLUSPLUS_DCPP_UDP_SERVER_H

#include <airdcpp/AdcCommand.h>
#include <airdcpp/DispatcherQueue.h>

namespace dcpp {

class UDPServer : public Thread, public CommandHandler<UDPServer> {
public:
	UDPServer();
	~UDPServer() override;

	const string& getPort() const { return port; }
	void disconnect();
	void listen();


	void addTask(Callback&& aTask) noexcept;
private:
	friend class CommandHandler<UDPServer>;

	int run() override;

	std::unique_ptr<Socket> socket;
	string port;
	bool stop;

	DispatcherQueue pp;
	void handlePacket(const ByteVector& aBuf, size_t aLen, const string& aRemoteIp);

	// Search results
	void handle(AdcCommand::RES, AdcCommand& c, const string& aRemoteIp) noexcept;

	// Ignore any other ADC commands for now
	template<typename T> void handle(T, AdcCommand&, const string&) { }
};

}

#endif // !defined(DCPLUSPLUS_DCPP_UDP_SERVER_H)