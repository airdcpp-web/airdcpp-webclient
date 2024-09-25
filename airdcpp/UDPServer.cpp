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


#include "stdinc.h"

#include <airdcpp/ConnectivityManager.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/ProtocolCommandManager.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/PartialSharingManager.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/Socket.h>
#include <airdcpp/UDPServer.h>
#include <airdcpp/UploadBundleManager.h>


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

UDPServer::UDPServer() : stop(false), pp(true) { }
UDPServer::~UDPServer() { }


void UDPServer::addTask(Callback&& aTask) noexcept {
	pp.addTask(std::move(aTask));
}

constexpr auto BUFSIZE = 8192;
int UDPServer::run() {
	int len;
	string remoteAddr;

	while(!stop) {
		try {
			if(!socket->wait(400, true, false).first) {
				continue;
			}

			auto buffer = vector<uint8_t>(BUFSIZE);
			if((len = socket->read(buffer.data(), BUFSIZE, remoteAddr)) > 0) {
				pp.addTask([buf = std::move(buffer), len, remoteAddr, this] { handlePacket(buf, len, remoteAddr); });
				continue;
			}
		} catch(const SocketException& e) {
			dcdebug("SearchManager::run Error: %s\n", e.getError().c_str());
		}

		bool failed = false;
		while(!stop) {
			try {
				socket->disconnect();
				port = socket->listen(Util::toString(CONNSETTING(UDP_PORT)));
				if(failed) {
					LogManager::getInstance()->message("Search enabled again", LogMessage::SEV_INFO, STRING(CONNECTIVITY));
					failed = false;
				}
				break;
			} catch(const SocketException& e) {
				dcdebug("SearchManager::run Stopped listening: %s\n", e.getError().c_str());

				if(!failed) {
					LogManager::getInstance()->message(STRING_F(SEARCH_DISABLED_X, e.getError()), LogMessage::SEV_ERROR, STRING(CONNECTIVITY));
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

void UDPServer::handlePacket(const ByteVector& aBuf, size_t aLen, const string& aRemoteIp) {
	string x(aBuf.begin(), aBuf.begin() + aLen);

	//check if this packet has been encrypted
	if (SETTING(ENABLE_SUDP) && aLen >= 32 && ((aLen & 15) == 0)) {
		SearchManager::getInstance()->decryptPacket(x, aLen, aBuf);
	}

	if (x.empty())
		return;

	COMMAND_DEBUG(x, ProtocolCommandManager::TYPE_CLIENT_UDP, ProtocolCommandManager::INCOMING, aRemoteIp);

	if (x.compare(0, 1, "$") == 0) {
		// NMDC commands
		if (x.compare(1, 3, "SR ") == 0) {
			SearchManager::getInstance()->onSR(x, aRemoteIp);
		} else {
			dcdebug("Unknown NMDC command received via UDP: %s\n", x.c_str());
		}
		
		return;
	}

	// ADC commands

	// ADC commands must end with \n
	if (x[x.length() - 1] != 0x0a) {
		dcdebug("Invalid UDP data received: %s (no newline)\n", x.c_str());
		return;
	}

	if (!Text::validateUtf8(x)) {
		dcdebug("UTF-8 validation failed for received UDP data: %s\n", x.c_str());
		return;
	}

	// Dispatch without newline
	dispatch(x.substr(0, x.length() - 1), false, [&aRemoteIp](const AdcCommand& aCmd) {
		ProtocolCommandManager::getInstance()->fire(ProtocolCommandManagerListener::IncomingUDPCommand(), aCmd, aRemoteIp);
	}, aRemoteIp);
}

void UDPServer::handle(AdcCommand::RES, AdcCommand& c, const string& aRemoteIp) noexcept {
	if (c.getParameters().empty())
		return;

	string cid = c.getParam(0);
	if (cid.size() != 39)
		return;

	UserPtr user = ClientManager::getInstance()->findUser(CID(cid));
	if (!user)
		return;

	// Remove the CID
	// This should be handled by AdcCommand really...
	c.getParameters().erase(c.getParameters().begin());

	SearchManager::getInstance()->onRES(c, user, aRemoteIp);
}

}