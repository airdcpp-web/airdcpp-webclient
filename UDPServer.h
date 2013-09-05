/*
 * Copyright (C) 2011-2013 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_UDP_SERVER_H
#define DCPLUSPLUS_DCPP_UDP_SERVER_H

#include "Socket.h"
#include "Thread.h"
#include "Semaphore.h"

#include <boost/lockfree/queue.hpp>

namespace dcpp {

class UDPServer : public Thread {
public:
	UDPServer();
	virtual ~UDPServer();

	const string& getPort() const { return port; }
	void disconnect();
	void listen();
private:
	virtual int run();

	std::unique_ptr<Socket> socket;
	string port;
	bool stop;


	class PacketProcessor : public Thread {
	public:
		struct PacketTask {
			PacketTask() : buf(nullptr) { }
			PacketTask(uint8_t* aBuf, size_t aLen, const string& aRemoteIp) : buf(aBuf), len(aLen), remoteIp(move(aRemoteIp)) { }
			//~PacketTask() { if (buf) delete buf; }

			uint8_t* buf;
			size_t len;
			string remoteIp;
		};

		PacketProcessor();
		virtual ~PacketProcessor();
		virtual int run();

		Semaphore s;
		boost::lockfree::queue<PacketTask*> queue;
	private:
		bool stop;
	};

	PacketProcessor pp;
};

}

#endif // !defined(DCPLUSPLUS_DCPP_UDP_SERVER_H)