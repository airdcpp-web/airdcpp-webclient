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
#include <airdcpp/connection/socket/Socket.h>

#include <airdcpp/connectivity/ConnectivityManager.h>
#include <airdcpp/core/header/format.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/core/timer/TimerManager.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/util/SystemUtil.h>

/// @todo remove when MinGW has this
#ifdef __MINGW32__
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL WSAEADDRNOTAVAIL
#endif
#endif

#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0
#endif

#ifndef IPV6_V6ONLY
#ifdef _WIN32 // Mingw seems to lack this...
#define IPV6_V6ONLY 27
#endif
#endif

namespace dcpp {

namespace {

#ifdef _WIN32

template<typename F>
inline auto check(F f, bool blockOk = false) -> decltype(f()) {
	for(;;) {
		auto ret = f();
		if(ret != static_cast<decltype(ret)>(SOCKET_ERROR)) {
			return ret;
		}

		auto error = Socket::getLastError();
		if(blockOk && error == WSAEWOULDBLOCK) {
			return static_cast<decltype(ret)>(-1);
		}

		if(error != EINTR) {
			throw SocketException(error);
		}
	}
}

inline void setBlocking2(socket_t sock, bool block) noexcept {
	u_long b = block ? 0 : 1;
	ioctlsocket(sock, FIONBIO, &b);
}

#else

template<typename F>
inline auto check(F f, bool blockOk = false) -> decltype(f()) {
	for(;;) {
		auto ret = f();
		if(ret != static_cast<decltype(ret)>(-1)) {
			return ret;
		}

		auto error = Socket::getLastError();
		if(blockOk && (error == EWOULDBLOCK || error == ENOBUFS || error == EINPROGRESS || error == EAGAIN)) {
			return -1;
		}

		if(error != EINTR) {
			throw SocketException(error);
		}
	}
}

inline void setBlocking2(socket_t sock, bool block) noexcept {
	int flags = fcntl(sock, F_GETFL, 0);
	if(block) {
		fcntl(sock, F_SETFL, flags & (~O_NONBLOCK));
	} else {
		fcntl(sock, F_SETFL, flags | O_NONBLOCK);
	}
}

#endif

inline int getSocketOptInt2(socket_t sock, int option) {
	int val;
	socklen_t len = sizeof(val);
	check([&] { return ::getsockopt(sock, SOL_SOCKET, option, (char*)&val, &len); });
	return val;
}

inline int setSocketOpt2(socket_t sock, int level, int option, int val) {
	int len = sizeof(val);
	return ::setsockopt(sock, level, option, (char*)&val, len);
}

inline bool isConnected(socket_t sock) {
	fd_set wfd;
	struct timeval tv = { 0 };

	FD_ZERO(&wfd);
	FD_SET(sock, &wfd);

    if(::select(static_cast<int>(sock) + 1, NULL, &wfd, NULL, &tv) == 1) {
        if (getSocketOptInt2(sock, SO_ERROR) == 0) {
            return true;
        }
    }

	return false;
}

inline socket_t readable(socket_t sock0, socket_t sock1) {
	// FD_SET for an invalid socket can cause a buffer overflow (at least on Linux)
	if (sock0 == INVALID_SOCKET) {
		return sock1;
	} else if (sock1 == INVALID_SOCKET) {
		return sock0;
	}

	fd_set rfd;
	struct timeval tv = { 0 };

	FD_ZERO(&rfd);
	FD_SET(sock0, &rfd);
	FD_SET(sock1, &rfd);

    if(::select(static_cast<int>(std::max(sock0, sock1)) + 1, &rfd, NULL, NULL, &tv) > 0) {
        return static_cast<int>(FD_ISSET(sock0, &rfd) ? sock0 : sock1);
    }

	return sock0;
}

}

Socket::addr Socket::udpAddr;
socklen_t Socket::udpAddrLen;

#ifdef _DEBUG

SocketException::SocketException(int aError) noexcept {
	errorString = "SocketException: " + errorToString(aError);
	dcdebug("Thrown: %s\n", errorString.c_str());
}

#else // _DEBUG

SocketException::SocketException(int aError) noexcept : Exception(errorToString(aError), aError) { }

#endif

#ifdef _WIN32

void SocketHandle::reset(socket_t s) {
	if(valid()) {
		::closesocket(sock);
	}

	sock = s;
}

int Socket::getLastError() { return ::WSAGetLastError(); }

#else

void SocketHandle::reset(socket_t s) {
	if(valid()) {
		::close(sock);
	}

	sock = s;
}

int Socket::getLastError() { return errno; }

#endif

Socket::Stats Socket::stats = { 0, 0 };

static const uint32_t SOCKS_TIMEOUT = 30000;

string SocketException::errorToString(int aError) noexcept {
	string msg = SystemUtil::translateError(aError);
	if(msg.empty()) {
		msg = str(boost::format("Unknown error: 0x%1$x") % aError);
	}

	return msg;
}

socket_t Socket::setSock(socket_t s, int af) {
	setBlocking2(s, false);
	setSocketOpt2(s, SOL_SOCKET, SO_REUSEADDR, 1);

#ifdef SO_REUSEPORT
	// Required on Linux/BSD to allow binding the same port from separate sockets (e.g. v4 and v6)
	setSocketOpt2(s, SOL_SOCKET, SO_REUSEPORT, 1);
#endif

	if(af == AF_INET) {
		dcassert(sock4 == INVALID_SOCKET);
		sock4 = s;
	} else if(af == AF_INET6) {
		dcassert(sock6 == INVALID_SOCKET);
		setSocketOpt2(s, IPPROTO_IPV6, IPV6_V6ONLY, 1);
		sock6 = s;
	} else {
		throw SocketException(str(boost::format("Unknown protocol %d") % af));
	}

	return s;
}

socket_t Socket::getSock() const {
	if(sock6.valid()) {
		if(sock4.valid()) {
			if(isConnected(sock6)) {
				dcdebug("Closing IPv4, IPv6 connected");
				sock4.reset();
			} else if(isConnected(sock4)) {
				dcdebug("Closing IPv6, IPv4 connected");
				sock6.reset();
				return sock4;
			}

			dcdebug("Both v4 & v6 sockets valid and unconnected, returning v6...\n");
			// TODO Neither connected - but this will become a race if the IPv4 socket connects while
			// we're still using the IPv6 one...
		}

		return sock6;
	}

	return sock4;
}

void Socket::setBlocking(bool block) noexcept {
	if(sock4.valid()) setBlocking2(sock4, block);
	if(sock6.valid()) setBlocking2(sock6, block);
}

socket_t Socket::create(const addrinfo& ai) {
	return setSock(check([&] { return ::socket(ai.ai_family, ai.ai_socktype, ai.ai_protocol); }), ai.ai_family);
}

const string& Socket::getIp() const noexcept {
	return sock6.valid() ? ip6 : ip4;
}

bool Socket::isV6Valid() const noexcept {
	return sock6.valid();
}

uint16_t Socket::accept(const Socket& listeningSocket) {
	disconnect();

	addr sock_addr = { { 0 } };
	socklen_t sz = sizeof(sock_addr);

	auto sock = check([&listeningSocket, &sock_addr, &sz] { 
		return ::accept(readable(listeningSocket.sock4, listeningSocket.sock6), &sock_addr.sa, &sz); 
	});
	setSock(sock, sock_addr.sa.sa_family);

#ifdef _WIN32
	// Make sure we disable any inherited windows message things for this socket.
	::WSAEventSelect(sock, NULL, 0);
#endif
	auto remoteIP = resolveName(&sock_addr.sa, sz);

	// return the remote port and set IP
	if(sock_addr.sa.sa_family == AF_INET) {
		setIp4(remoteIP);
		return ntohs(sock_addr.sai.sin_port);
	}
	if(sock_addr.sa.sa_family == AF_INET6) {
		setIp6(remoteIP);
		return ntohs(sock_addr.sai6.sin6_port);
	}
	return 0;
}

string Socket::listen(const string& port) {
	disconnect();

	// For server sockets we create both ipv4 and ipv6 if possible
	// We use the same port for both sockets to deal with the fact that
	// there's no way in ADC to have different ports for v4 and v6 TCP sockets

	uint16_t ret = 0;

	addrinfo_p ai(nullptr, nullptr);

	if(!v4only) {
		try { ai = resolveAddr(localIp6, port, AF_INET6, AI_PASSIVE | AI_ADDRCONFIG); }
		catch(const SocketException&) { ai.reset(); }
		for(auto a = ai.get(); a && !sock6.valid(); a = a->ai_next) {
			try {
				create(*a);
				if(ret != 0) {
					((sockaddr_in6*)a->ai_addr)->sin6_port = ret;
				}

				check([this, a] { return ::bind(sock6, a->ai_addr, static_cast<int>(a->ai_addrlen)); });
				check([this, a] { return ::getsockname(sock6, a->ai_addr, (socklen_t*)&a->ai_addrlen); });
				ret = ((sockaddr_in6*)a->ai_addr)->sin6_port;

				if(type == TYPE_TCP) {
					check([this] { return ::listen(sock6, 20); });
				}
			} catch(const SocketException&) { }
		}
	}

	try { ai = resolveAddr(localIp4, port, AF_INET, AI_PASSIVE | AI_ADDRCONFIG); }
	catch(const SocketException&) { ai.reset(); }
	for(auto a = ai.get(); a && !sock4.valid(); a = a->ai_next) {
		try {
			create(*a);
			if(ret != 0) {
				((sockaddr_in*)a->ai_addr)->sin_port = ret;
			}

			check([this, &a] { return ::bind(sock4, a->ai_addr, static_cast<int>(a->ai_addrlen)); });
			check([this, &a] { return ::getsockname(sock4, a->ai_addr, (socklen_t*)&a->ai_addrlen); });
			ret = ((sockaddr_in*)a->ai_addr)->sin_port;

			if(type == TYPE_TCP) {
				check([this] { return ::listen(sock4, 20); });
			}
		} catch (const SocketException& e) { 
			dcdebug("Socket::listen for port %s caught an error: %s\n", port.c_str(), e.getError().c_str());
		}
	}

	if(ret == 0) {
		throw SocketException("Could not open port for listening");
	}
	return Util::toString(ntohs(ret));
}

void Socket::connect(const AddressInfo& aAddr, const string& aPort, const string& aLocalPort) {
	disconnect();

	// We try to connect to both IPv4 and IPv6 if available
	string lastError;

	if (aAddr.getType() == AddressInfo::TYPE_URL) {
		connect(aAddr.getV6CompatibleAddress(), aPort, aLocalPort, AF_UNSPEC, lastError);
	} else {
		if (aAddr.hasV6CompatibleAddress()) {
			connect(aAddr.getV6CompatibleAddress(), aPort, aLocalPort, AF_INET6, lastError);
		}

		if (aAddr.hasV4CompatibleAddress()) {
			connect(aAddr.getV4CompatibleAddress(), aPort, aLocalPort, AF_INET, lastError);
		}
	}

	// IP should be set if at least one connection attempt succeed
	if (ip4.empty() && ip6.empty())
		throw SocketException(lastError);
}

void Socket::connect(const string& aAddr, const string& aPort, const string& aLocalPort, int aFamily, string& lastError_) {
	auto addr = resolveAddr(aAddr, aPort, aFamily);
	for (auto ai = addr.get(); ai; ai = ai->ai_next) {
		if ((ai->ai_family == AF_INET && !sock4.valid()) ||
			(ai->ai_family == AF_INET6 && !sock6.valid())) {

			if (ai->ai_family == AF_INET6 && v4only) {
				lastError_ = STRING(CONNECTION_IPV6_UNSUPPORTED);
				continue;
			}

			try {
				auto sock = create(*ai);
				auto &localIp = ai->ai_family == AF_INET ? getLocalIp4() : getLocalIp6();

				if (!aLocalPort.empty() || !localIp.empty()) {
					auto local = resolveAddr(localIp, aLocalPort, ai->ai_family);
					check([&local, &sock] { return ::bind(sock, local->ai_addr, static_cast<int>(local->ai_addrlen)); });
				}

				check([&sock, &ai] { return ::connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)); }, true);

				auto ip = resolveName(ai->ai_addr, static_cast<int>(ai->ai_addrlen));
				ai->ai_family == AF_INET ? setIp4(ip) : setIp6(ip);
			} catch (const SocketException& e) {
				ai->ai_family == AF_INET ? sock4.reset() : sock6.reset();
				lastError_ = e.getError();
			}
		}
	}
}

namespace {
	inline uint64_t timeLeft(uint64_t start, uint64_t timeout) {
		if(timeout == 0) {
			return 0;
		}
		uint64_t now = GET_TICK();
		if(start + timeout < now)
			throw SocketException(STRING(CONNECTION_TIMEOUT));
		return start + timeout - now;
	}
}
void Socket::appendSocksAddress(const string& aAddr, const string& aPort, ByteVector& connStr_) const {
	if (SETTING(SOCKS_RESOLVE)) {
		connStr_.push_back(SocksAddrType::TYPE_DOMAIN);
		connStr_.push_back((uint8_t)aAddr.size());
		connStr_.insert(connStr_.end(), aAddr.begin(), aAddr.end());
	} else {
		auto ai = resolveAddr(aAddr, aPort);
		if (ai->ai_family == AF_INET) {
			connStr_.push_back(SocksAddrType::TYPE_V4);
			auto paddr = (uint8_t*)&((sockaddr_in*)ai->ai_addr)->sin_addr;
			connStr_.insert(connStr_.end(), paddr, paddr + 4);
		} else if (ai->ai_family == AF_INET6) {
			connStr_.push_back(SocksAddrType::TYPE_V6);
			auto paddr = (uint8_t*)&((sockaddr_in6*)ai->ai_addr)->sin6_addr;
			connStr_.insert(connStr_.end(), paddr, paddr + 16);
		}
	}

	uint16_t port = htons(static_cast<uint16_t>(Util::toInt(aPort)));
	auto pport = (uint8_t*)&port;
	connStr_.push_back(pport[0]);
	connStr_.push_back(pport[1]);
}

void Socket::socksConnect(addr& addr_, const SocksConstructConnF& aConstructConnStr, uint64_t aTimeout) {
	if (SETTING(SOCKS_SERVER).empty() || SETTING(SOCKS_PORT) == 0) {
		throw SocketException(STRING(SOCKS_FAILED));
	}

	ByteVector connStr;
	uint64_t start = GET_TICK();

	// Auth

	// Not pretty, but IPv6 should always be allowed with SOCKS server...
	auto prevV4Only = v4only;
	setV4only(false);
	Socket::connect(AddressInfo(SETTING(SOCKS_SERVER), AddressInfo::TYPE_URL), Util::toString(SETTING(SOCKS_PORT)));
	setV4only(prevV4Only);

	if (!Socket::waitConnected(timeLeft(start, aTimeout))) {
		throw SocketException(STRING(SOCKS_FAILED));
	}

	socksAuth(timeLeft(start, aTimeout));

	aConstructConnStr(connStr);

	// Send data
	socksWrite(&connStr[0], connStr.size(), timeLeft(start, aTimeout));

	// Read response
	connStr.resize(22); // Make it fit the IPv6 response
	auto len = socksRead(
		connStr,
		22,
		[](const ByteVector& aBuffer, int aLen) {
			if (aLen < 10) {
				return false;
			}

			auto expectedDataLength = aBuffer[3] == SocksAddrType::TYPE_V6 ? 22 : 10;
			return aLen >= expectedDataLength;
		},
		timeLeft(start, aTimeout)
	);

	connStr.resize(len);
	socksParseResponseAddress(connStr, len, addr_);
}

void Socket::socksConnect(const AddressInfo& aAddr, const string& aPort, uint64_t aTimeout) {
	addr sock_addr;
	socksConnect(
		sock_addr,
		[this, &aAddr, &aPort](ByteVector& connStr_) {
			connStr_.push_back(5);			// SOCKSv5
			connStr_.push_back(1);			// Connect
			connStr_.push_back(0);			// Reserved
			appendSocksAddress(aAddr.hasV6CompatibleAddress() ? aAddr.getV6CompatibleAddress() : aAddr.getV4CompatibleAddress(), aPort, connStr_);
		},
		aTimeout
	);

	auto isV6 = sock_addr.sa.sa_family == AF_INET6;
	const auto ip = resolveName(&sock_addr.sa, isV6 ? sizeof(sock_addr.sai6) : sizeof(sock_addr.sai));
	if (isV6) {
		setIp6(ip);
	} else {
		setIp4(ip);
	}

	dcdebug("SOCKS5: resolved address %s:%d (v6: %s)\n", ip.c_str(), isV6 ? sock_addr.sai6.sin6_port : sock_addr.sai.sin_port, isV6 ? "true" : "false");
}

void Socket::socksAuth(uint64_t timeout) {
	vector<uint8_t> connStr;

	uint64_t start = GET_TICK();

	if(SETTING(SOCKS_USER).empty() && SETTING(SOCKS_PASSWORD).empty()) {
		// No username and pw, easier...=)
		connStr.push_back(5);			// SOCKSv5
		connStr.push_back(1);			// 1 method
		connStr.push_back(0);			// Method 0: No auth...

		socksWrite(&connStr[0], 3, timeLeft(start, timeout));

		if (socksRead(connStr, 2, timeLeft(start, timeout)) != 2) {
			throw SocketException(STRING(SOCKS_FAILED));
		}

		if (connStr[1] != 0) {
			throw SocketException(STRING(SOCKS_NEEDS_AUTH));
		}
	} else {
		// We try the username and password auth type (no, we don't support gssapi)

		connStr.push_back(5);			// SOCKSv5
		connStr.push_back(1);			// 1 method
		connStr.push_back(2);			// Method 2: Name/Password...
		socksWrite(&connStr[0], 3, timeLeft(start, timeout));

		if (socksRead(connStr, 2, timeLeft(start, timeout)) != 2) {
			throw SocketException(STRING(SOCKS_FAILED));
		}
		if (connStr[1] != 2) {
			throw SocketException(STRING(SOCKS_AUTH_UNSUPPORTED));
		}

		connStr.clear();
		// Now we send the username / pw...
		connStr.push_back(1);
		connStr.push_back((uint8_t)SETTING(SOCKS_USER).length());
		connStr.insert(connStr.end(), SETTING(SOCKS_USER).begin(), SETTING(SOCKS_USER).end());
		connStr.push_back((uint8_t)SETTING(SOCKS_PASSWORD).length());
		connStr.insert(connStr.end(), SETTING(SOCKS_PASSWORD).begin(), SETTING(SOCKS_PASSWORD).end());

		socksWrite(&connStr[0], connStr.size(), timeLeft(start, timeout));

		if (socksRead(connStr, 2, timeLeft(start, timeout)) != 2) {
			throw SocketException(STRING(SOCKS_AUTH_FAILED));
		}

		if(connStr[1] != 0) {
			throw SocketException(STRING(SOCKS_AUTH_FAILED));
		}
	}
}

int Socket::getSocketOptInt(int option) {
	int val;
	socklen_t len = sizeof(val);
	check([&val, option, this, &len] { return ::getsockopt(getSock(), SOL_SOCKET, option, (char*)&val, &len); });
	return val;
}

void Socket::setSocketOpt(int option, int val) {
	int len = sizeof(val);
	if(sock4.valid()) {
		check([this, option, val, len] { 
			return ::setsockopt(sock4, SOL_SOCKET, option, (char*)&val, len); 
		});
	}

	if(sock6.valid()) {
		check([this, option, val, len] { 
			return ::setsockopt(sock6, SOL_SOCKET, option, (char*)&val, len); 
		});
	}
}

int Socket::read(void* aBuffer, size_t aBufLen) {
	auto len = check([&aBuffer, aBufLen, this] {
		return type == TYPE_TCP
			? ::recv(getSock(), (char*)aBuffer, static_cast<int>(aBufLen), 0)
			: ::recvfrom(readable(sock4, sock6), (char*)aBuffer, static_cast<int>(aBufLen), 0, NULL, NULL);
	}, true);

	if(len > 0) {
		stats.totalDown += len;
	}

	return len;
}

int Socket::read(void* aBuffer, size_t aBufLen, string &aIP) {
	dcassert(type == TYPE_UDP);

	addr remote_addr = { { 0 } };
	socklen_t addr_length = sizeof(remote_addr);

	auto len = check([&] {
		return ::recvfrom(readable(sock4, sock6), (char*)aBuffer, static_cast<int>(aBufLen), 0, &remote_addr.sa, &addr_length);
	}, true);

	if(len > 0) {
		aIP = resolveName(&remote_addr.sa, addr_length);
		stats.totalDown += len;
	} else {
		aIP.clear();
	}

	return len;
}

int Socket::socksRead(ByteVector& aBuffer, size_t aBufLen, const SocksCompleteF& aIsComplete, uint64_t aTimeout) {
	int i = 0;
	while (i <= 0 || !aIsComplete(aBuffer, i)) {
		int j = Socket::read(&aBuffer[i], aBufLen - i);
		if (j == 0) {
			return i;
		} else if(j == -1) {
			if(!Socket::wait(aTimeout, true, false).first) {
				return i;
			}
			continue;
		}

		i += j;
	}
	return i;
}

int Socket::socksRead(ByteVector& aBuffer, size_t aBufLen, uint64_t aTimeout) {
	return socksRead(
		aBuffer, 
		aBufLen, 
		[aBufLen](const ByteVector&, int aLen) { return static_cast<size_t>(aLen) == aBufLen; }, 
		aTimeout
	);
}

void Socket::socksWrite(const void* aBuffer, size_t aLen, uint64_t timeout) {
	auto buf = (const uint8_t*)aBuffer;
	size_t pos = 0;
	// No use sending more than this at a time...
	int sendSize = getSocketOptInt(SO_SNDBUF);

	while(pos < aLen) {
		int i = Socket::write(buf+pos, std::min(aLen - pos, static_cast<size_t>(sendSize)));
		if (i == -1) {
			Socket::wait(timeout, false, true);
		} else {
			pos+=i;
			stats.totalUp += i;
		}
	}
}

int Socket::write(const void* aBuffer, size_t aLen) {
	auto sent = check([&aBuffer, &aLen, this] { return ::send(getSock(), (const char*)aBuffer, static_cast<int>(aLen), 0); }, true);
	if(sent > 0) {
		stats.totalUp += sent;
	}
	return sent;
}

/**
 * Sends data, will block until all data has been sent or an exception occurs
 * @param aBuffer Buffer with data
 * @param aLen Data length
 * @throw SocketExcpetion Send failed.
 */
void Socket::writeTo(const string& aAddr, const string& aPort, const void* aBuffer, size_t aLen) {
	if(aLen <= 0)
		return;

	if(aAddr.empty() || aPort.empty()) {
		throw SocketException(EADDRNOTAVAIL);
	}

	auto buf = (const uint8_t*)aBuffer;

	int sent;
	if (CONNSETTING(OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5 && socksUdpInitialized()) {
		// Create connect string
		ByteVector connStr;

		connStr.reserve(aLen + 24);

		connStr.push_back(0);		// Reserved
		connStr.push_back(0);		// Reserved
		connStr.push_back(0);		// Fragment number, always 0 in our case...

		appendSocksAddress(aAddr, aPort, connStr); // Destination address

		connStr.insert(connStr.end(), buf, buf + aLen); // Data

		// Initialize socket
		if ((udpAddr.sa.sa_family == AF_INET && !sock4.valid()) || (udpAddr.sa.sa_family == AF_INET6 && !sock6.valid())) {
			addrinfo ai = { 0 };
			ai.ai_family = udpAddr.sa.sa_family;
			ai.ai_flags = 0;
			ai.ai_socktype = type == TYPE_TCP ? SOCK_STREAM : SOCK_DGRAM;
			ai.ai_protocol = type;

			create(ai);
		}

		// Send
		sent = check([&connStr, this] {
			return ::sendto(
				udpAddr.sa.sa_family == AF_INET ? sock4 : sock6,
				(const char*)&connStr[0], 
				(int)connStr.size(), 
				0, 
				&udpAddr.sa, 
				udpAddrLen
			); 
		});
	} else {
		auto ai = resolveAddr(aAddr, aPort);
		if((ai->ai_family == AF_INET && !sock4.valid()) || (ai->ai_family == AF_INET6 && !sock6.valid())) {
			create(*ai);
		}

		sent = check([this, &ai, &aBuffer, aLen] { 
			return ::sendto(
				ai->ai_family == AF_INET ? sock4 : sock6,
				(const char*)aBuffer, 
				static_cast<int>(aLen), 
				0, 
				ai->ai_addr, 
				static_cast<int>(ai->ai_addrlen)
			); 
		});
	}

	stats.totalUp += sent;
}

/**
 * Blocks until timeout is reached one of the specified conditions have been fulfilled
 * @param millis Max milliseconds to block.
 * @param checkRead Check for reading
 * @param checkWrite Check for writing
 * @return pair with read/write state respectively
 * @throw SocketException Select or the connection attempt failed.
 */
std::pair<bool, bool> Socket::wait(uint64_t millis, bool checkRead, bool checkWrite) {
	timeval tv;
	tv.tv_sec = static_cast<long>(millis / 1000);
	tv.tv_usec = (millis % 1000) * 1000;
	fd_set rfd, wfd;
	fd_set *rfdp = NULL, *wfdp = NULL;

	int nfds = -1;

	if(checkRead) {
		rfdp = &rfd;
		FD_ZERO(rfdp);
		if(sock4.valid()) {
			FD_SET(sock4, &rfd);
			nfds = std::max((int)sock4, nfds);
		}

		if(sock6.valid()) {
			FD_SET(sock6, &rfd);
			nfds = std::max((int)sock6, nfds);
		}
	}

	if(checkWrite) {
		wfdp = &wfd;
		FD_ZERO(wfdp);
		if(sock4.valid()) {
			FD_SET(sock4, &wfd);
			nfds = std::max((int)sock4, nfds);
		}

		if(sock6.valid()) {
			FD_SET(sock6, &wfd);
			nfds = std::max((int)sock6, nfds);
		}
	}

	check([&] { return ::select(nfds + 1, rfdp, wfdp, NULL, &tv); });

	return std::make_pair(
		rfdp && ((sock4.valid() && FD_ISSET(sock4, rfdp)) || (sock6.valid() && FD_ISSET(sock6, rfdp))),
		wfdp && ((sock4.valid() && FD_ISSET(sock4, wfdp)) || (sock6.valid() && FD_ISSET(sock6, wfdp))));
}

bool Socket::waitConnected(uint64_t millis) {
	timeval tv;
	tv.tv_sec = static_cast<long>(millis / 1000);
	tv.tv_usec = (millis % 1000) * 1000;
	fd_set fd;
	FD_ZERO(&fd);

	int nfds = -1;
	if(sock4.valid()) {
		FD_SET(sock4, &fd);
		nfds = static_cast<int>(sock4);
	}

	if(sock6.valid()) {
		FD_SET(sock6, &fd);
		nfds = std::max(static_cast<int>(sock6), nfds);
	}

	check([&] { return ::select(nfds + 1, NULL, &fd, NULL, &tv); });

	if(sock6.valid() && FD_ISSET(sock6, &fd)) {
		int err6 = getSocketOptInt2(sock6, SO_ERROR);
		if(err6 == 0) {
			sock4.reset(); // We won't be needing this any more...
			return true;
		}

		if(!sock4.valid()) {
			throw SocketException(err6);
		}

		sock6.reset();
	}

	if(sock4.valid() && FD_ISSET(sock4, &fd)) {
		int err4 = getSocketOptInt2(sock4, SO_ERROR);
		if(err4 == 0) {
			sock6.reset(); // We won't be needing this any more...
			return true;
		}

		if(!sock6.valid()) {
			throw SocketException(err4);
		}

		sock4.reset();
	}

	return false;
}

bool Socket::waitAccepted(uint64_t /*millis*/) {
	// Normal sockets are always connected after a call to accept
	return true;
}

string Socket::resolve(const string& aDns, int af) noexcept {
	addrinfo hints = { 0 };
	hints.ai_family = af;

	addrinfo *result = 0;

	string ret;

	if(!::getaddrinfo(aDns.c_str(), NULL, &hints, &result)) {
		try { ret = resolveName(result->ai_addr, static_cast<int>(result->ai_addrlen)); }
		catch(const SocketException&) { }

		::freeaddrinfo(result);
	}

	return ret;
}

Socket::addrinfo_p Socket::resolveAddr(const string& name, const string& port, int family, int flags) const {
	addrinfo hints = { 0 };
	hints.ai_family = family;
	hints.ai_flags = flags;
	hints.ai_socktype = type == TYPE_TCP ? SOCK_STREAM : SOCK_DGRAM;
	hints.ai_protocol = type;

	addrinfo *result = 0;

	if (auto err = ::getaddrinfo(name.c_str(), port.empty() ? NULL : port.c_str(), &hints, &result)) {
		throw SocketException(err);
	}

	//dcdebug("Resolved %s:%s to %s, next is %p\n", name.c_str(), port.c_str(),
		//resolveName(result->ai_addr, result->ai_addrlen).c_str(), result->ai_next);

	return addrinfo_p(result, &freeaddrinfo);
}

string Socket::resolveName(const sockaddr* sa, socklen_t sa_len, int flags) {
	char buf[1024];

	if (auto err = ::getnameinfo(sa, sa_len, buf, sizeof(buf), NULL, 0, flags)) {
		throw SocketException(err);
	}

	return string(buf);
}

bool Socket::hasSocket() const noexcept {
	try {
		return getSock() == INVALID_SOCKET;
	} catch (const SocketException&) {
		//...
	}

	return false;
}

string Socket::getLocalIp() const noexcept {
	if(!hasSocket())
		return Util::emptyString;

	addr sock_addr;
	if(socklen_t len = sizeof(sock_addr); ::getsockname(getSock(), &sock_addr.sa, &len) == 0) {
		try { return resolveName(&sock_addr.sa, len); }
		catch(const SocketException&) { }
	}

	return Util::emptyString;
}

uint16_t Socket::getLocalPort() const noexcept {
	if(!hasSocket())
		return 0;

	addr sock_addr;
	if(socklen_t len = sizeof(sock_addr); ::getsockname(getSock(), &sock_addr.sa, &len) == 0) {
		if(sock_addr.sa.sa_family == AF_INET) {
			return ntohs(sock_addr.sai.sin_port);
		} else if(sock_addr.sa.sa_family == AF_INET6) {
			return ntohs(sock_addr.sai6.sin6_port);
		}
	}
	return 0;
}


bool Socket::socksUdpInitialized() noexcept {
	return udpAddr.sa.sa_family != 0;
}

void Socket::socksUpdated() {
	memset(&udpAddr, 0, sizeof(udpAddr));
	udpAddrLen = sizeof(udpAddr);

	if(CONNSETTING(OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5) {
		// Set up a UDP relay with the server
		try {
			Socket s(TYPE_TCP);
			s.setBlocking(false);
			s.socksConnect(
				udpAddr,
				[&s](ByteVector& connStr_) {
					const bool v6 = s.isV6Valid();

					connStr_.push_back(5);			// SOCKSv5
					connStr_.push_back(3);			// UDP Associate
					connStr_.push_back(0);			// Reserved
					connStr_.push_back((unsigned char)(v6 ? SocksAddrType::TYPE_V6 : SocksAddrType::TYPE_V4));

					connStr_.insert(connStr_.end(), v6 ? 16 : 4, 0); // No specific outgoing UDP address
					connStr_.insert(connStr_.end(), 2, 0); // No specific port...
				},
				SOCKS_TIMEOUT
			);

		} catch (const SocketException& e) {
			dcdebug("Socket: Failed to register with socks server (%s)\n", e.getError().c_str());
			throw SocketException(STRING_F(SOCKS_SETUP_ERROR, e.getError()));
		}

		auto isV6 = udpAddr.sa.sa_family == AF_INET6;
		auto port = isV6 ? udpAddr.sai6.sin6_port : udpAddr.sai.sin_port;

		// We can't send any data without a valid port (IP could be validated as well...)
		if (port == 0) {
			dcdebug("SOCKS5: invalid port number was received\n");
			throw SocketException(STRING_F(SOCKS_SETUP_ERROR, STRING(SOCKS_UNSUPPORTED_RESPONSE)));
		}

		udpAddrLen = isV6 ? sizeof(udpAddr.sai6) : sizeof(udpAddr.sai);

		dcdebug("SOCKS5: UDP initialized with address %s:%d (v6: %s)\n", resolveName(&udpAddr.sa, udpAddrLen).c_str(), port, isV6 ? "true" : "false");
	}
}

void Socket::socksParseResponseAddress(const ByteVector& aData, size_t aDataLength, Socket::addr& addr_) {
	if (aDataLength < 10) {
		dcdebug("SOCKS5: not enough bytes in the response (" SIZET_FMT ")\n", aDataLength);
		throw SocketException(STRING(SOCKS_UNSUPPORTED_RESPONSE));
	}

	if (aData[0] != 5) {
		dcdebug("SOCKS5: invalid SOCKS version received (%d)\n", aData[0]);
		throw SocketException(STRING(SOCKS_UNSUPPORTED_RESPONSE));
	}

	if (aData[1] != 0) {
		// Connection failed
		dcdebug("SOCKS5: error received (%d)\n", aData[1]);
		throw SocketException(STRING(SOCKS_FAILED));
	}

	if (aData[3] == SocksAddrType::TYPE_V4) {
		addr_.sa.sa_family = AF_INET;
	} else if (aData[3] == SocksAddrType::TYPE_V6) {
		addr_.sa.sa_family = AF_INET6;
	} else {
		dcdebug("SOCKS5: unsupported protocol (%d)\n", aData[3]);
		throw SocketException(STRING(SOCKS_UNSUPPORTED_RESPONSE));
	}

	size_t expectedDataLength = addr_.sa.sa_family == AF_INET ? 10 : 22;
	if (aDataLength != expectedDataLength) {
		dcdebug("SOCKS5: received " SIZET_FMT " bytes while " SIZET_FMT " bytes were expected\n", aDataLength, expectedDataLength);
		throw SocketException(STRING(SOCKS_UNSUPPORTED_RESPONSE));
	}

	// Some server implementations may not return any IP/port for regular connect responses (those are required only for binding)
	// The caller should handle validation
	const auto port = *((uint16_t*)(&aData[aData.size() - 2])); // 2 bytes
	if (addr_.sa.sa_family == AF_INET6) {
		addr_.sai6.sin6_port = port;
	} else {
		addr_.sai.sin_port = port;
	}
#ifdef _WIN32
	if (addr_.sa.sa_family == AF_INET6) {
		memcpy(addr_.sai6.sin6_addr.u.Byte, &aData[4], 16);
	} else {
		addr_.sai.sin_addr.S_un.S_addr = *((long*)(&aData[4]));
	}
#else
	if (udpAddr.sa.sa_family == AF_INET6) {
		memcpy(addr_.sai6.sin6_addr.s6_addr, &aData[4], 16);
	} else {
		addr_.sai.sin_addr.s_addr = *((long*)(&aData[4]));
	}
#endif
}

void Socket::shutdown() noexcept {
	if(sock4.valid()) ::shutdown(sock4, 2);
	if(sock6.valid()) ::shutdown(sock6, 2);
}

void Socket::close() noexcept {
	sock4.reset();
	sock6.reset();
}

void Socket::disconnect() noexcept {
	shutdown();
	close();
}

} // namespace dcpp
