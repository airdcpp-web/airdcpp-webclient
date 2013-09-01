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


#include "stdinc.h"

#include "ConnectivityManager.h"
#include "ClientManager.h"
#include "DebugManager.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "SearchManager.h"
#include "SettingsManager.h"
#include "UDPServer.h"
#include "UploadManager.h"


namespace dcpp {

void UDPServer::listen() {
	disconnect();

	try {
		socket.reset(new Socket(Socket::TYPE_UDP));
		socket->setLocalIp4(CONNSETTING(BIND_ADDRESS));
		socket->setLocalIp6(CONNSETTING(BIND_ADDRESS6));
		socket->setV4only(false);
		port = socket->listen(Util::toString(CONNSETTING(UDP_PORT)));
		start();
	} catch(...) {
		socket.reset();
		throw;
	}
}

void UDPServer::disconnect() {
	if(socket.get()) {
		stop = true;
		socket->disconnect();
		port.clear();

		join();

		socket.reset();

		stop = false;
	}
}

UDPServer::UDPServer() : stop(false) { }
UDPServer::~UDPServer() {
	/*if(socket.get()) {
		stop = true;
		socket->disconnect();
#ifdef _WIN32
		join();
#endif
	}*/
}

#define BUFSIZE 8192
int UDPServer::run() {
	uint8_t* buf = nullptr;
	int len;
	string remoteAddr;

	while(!stop) {
		try {
			if(!socket->wait(400, true, false).first) {
				continue;
			}

			buf = new uint8_t[BUFSIZE];
			if((len = socket->read(buf, BUFSIZE, remoteAddr)) > 0) {
				pp.queue.push(new PacketProcessor::PacketTask(buf, len, remoteAddr));
				pp.s.signal();
				continue;
			}

			delete buf;
		} catch(const SocketException& e) {
			dcdebug("SearchManager::run Error: %s\n", e.getError().c_str());
		}

		bool failed = false;
		while(!stop) {
			try {
				socket->disconnect();
				port = socket->listen(Util::toString(CONNSETTING(UDP_PORT)));
				if(failed) {
					LogManager::getInstance()->message("Search enabled again", LogManager::LOG_INFO);
					failed = false;
				}
				break;
			} catch(const SocketException& e) {
				dcdebug("SearchManager::run Stopped listening: %s\n", e.getError().c_str());

				if(!failed) {
					LogManager::getInstance()->message(STRING_F(SEARCH_DISABLED_X, e.getError()), LogManager::LOG_ERROR);
					failed = true;
				}

				// Spin for 60 seconds
				for(auto i = 0; i < 60 && !stop; ++i) {
					Thread::sleep(1000);
				}
			}
		}
	}

	return 0;
}

UDPServer::PacketProcessor::PacketProcessor() : stop(false), queue(1024) {
	start();
}

UDPServer::PacketProcessor::~PacketProcessor() {
	stop = true;
	s.signal();
	join();
}

int UDPServer::PacketProcessor::run() {
	while (true) {
		s.wait();
		if (stop)
			break;

		unique_ptr<PacketTask> t;
		if(!queue.pop(t)) {
			continue;
		}

		string x((char*)t->buf, t->len);
		string remoteIp = move(t->remoteIp);

		//check if this packet has been encrypted
		if (SETTING(ENABLE_SUDP) && t->len >= 32 && ((t->len & 15) == 0)) {
			SearchManager::getInstance()->decryptPacket(x, t->len, t->buf, BUFSIZE);
		}
			
		delete t->buf;
		if (x.empty())
			continue;

		COMMAND_DEBUG(x, DebugManager::TYPE_CLIENT_UDP, DebugManager::INCOMING, remoteIp);

		if(x.compare(0, 4, "$SR ") == 0) {
			SearchManager::getInstance()->onSR(x, remoteIp);
		} else if(x.compare(1, 4, "RES ") == 0 && x[x.length() - 1] == 0x0a) {
			AdcCommand c(x.substr(0, x.length()-1));
			if(c.getParameters().empty())
				continue;
			string cid = c.getParam(0);
			if(cid.size() != 39)
				continue;

			UserPtr user = ClientManager::getInstance()->findUser(CID(cid));
			if(!user)
				continue;

			// This should be handled by AdcCommand really...
			c.getParameters().erase(c.getParameters().begin());

			SearchManager::getInstance()->onRES(c, user, remoteIp);
		} else if (x.compare(1, 4, "PSR ") == 0 && x[x.length() - 1] == 0x0a) {
			AdcCommand c(x.substr(0, x.length()-1));
			if(c.getParameters().empty())
				continue;
			string cid = c.getParam(0);
			if(cid.size() != 39)
				continue;

			UserPtr user = ClientManager::getInstance()->findUser(CID(cid));
			// when user == NULL then it is probably NMDC user, check it later
			
			c.getParameters().erase(c.getParameters().begin());			
			
			SearchManager::getInstance()->onPSR(c, user, remoteIp);
		
		} else if (x.compare(1, 4, "PBD ") == 0 && x[x.length() - 1] == 0x0a) {
			if (!SETTING(USE_PARTIAL_SHARING)) {
				continue;
			}
			//LogManager::getInstance()->message("GOT PBD UDP: " + x);
			AdcCommand c(x.substr(0, x.length()-1));
			if(c.getParameters().empty())
				continue;
			string cid = c.getParam(0);
			if(cid.size() != 39)
				continue;

			UserPtr user = ClientManager::getInstance()->findUser(CID(cid));
			
			c.getParameters().erase(c.getParameters().begin());			
			
			if (user)
				SearchManager::getInstance()->onPBD(c, user);
		
		} else if ((x.compare(1, 4, "UBD ") == 0 || x.compare(1, 4, "UBN ") == 0) && x[x.length() - 1] == 0x0a) {
			AdcCommand c(x.substr(0, x.length()-1));
			if(c.getParameters().empty())
				continue;
			
			c.getParameters().erase(c.getParameters().begin());			
			
			if (x.compare(1, 4, "UBN ") == 0) {
				//LogManager::getInstance()->message("GOT UBN UDP: " + x);
				UploadManager::getInstance()->onUBN(c);
			} else {
				//LogManager::getInstance()->message("GOT UBD UDP: " + x);
				UploadManager::getInstance()->onUBD(c);
			}
		}
	}

	return 0;

	/*else if(x.compare(1, 4, "SCH ") == 0 && x[x.length() - 1] == 0x0a) {
		try {
			respond(AdcCommand(x.substr(0, x.length()-1)));
		} catch(ParseException& ) {
		}
	}*/ // Needs further DoS investigation
}

}