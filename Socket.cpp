/* 
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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
#include "Socket.h"

#include "SettingsManager.h"
#include "ResourceManager.h"
#include "TimerManager.h"
#include "LogManager.h"

#include <IPHlpApi.h>
#pragma comment(lib, "iphlpapi.lib")

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

Socket::addr Socket::udpAddr;
socklen_t Socket::udpAddrLen;
uint16_t Socket::family = AF_INET6;	// IPv6 as default, unsupported OS will fallback to IPv4

#ifdef _DEBUG

SocketException::SocketException(int aError) noexcept {
	error = "SocketException: " + errorToString(aError);
	dcdebug("Thrown: %s\n", error.c_str());
}

#else // _DEBUG

SocketException::SocketException(int aError) noexcept : Exception(errorToString(aError)) { }

#endif

Socket::Stats Socket::stats = { 0, 0 };

static const uint32_t SOCKS_TIMEOUT = 30000;

string SocketException::errorToString(int aError) noexcept {
	string msg = Util::translateError(aError);
	if(msg.empty())
	{
		char tmp[64];
		snprintf(tmp, sizeof(tmp), CSTRING(UNKNOWN_ERROR), aError);
		msg = tmp;
	}

	return msg;
}

void Socket::create(uint8_t aType /* = TYPE_TCP */) {
	if(sock != INVALID_SOCKET)
		disconnect();

	switch(aType) {
	case TYPE_TCP:
		sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
		break;
	case TYPE_UDP:
		sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
		break;
	default:
		dcassert(0);
	}
	
	if(sock == SOCKET_ERROR) {
		int err = getLastError();
		if((err == WSAEAFNOSUPPORT) && (family == AF_INET6)) {	// FIXME: WSAEAFNOSUPPORT on non-Windows
			// IPv6 unsupported, fallback to IPv4
			family = AF_INET;
			create(aType);
		} else
			throw SocketException(err); 
	}
		

	if(family == AF_INET6) {
		// enable hybrid dual stack (IPv4 and IPv6 via one socket)
		int val = 0;
		if(::setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&val, sizeof(val)) == SOCKET_ERROR) {
			// hybrid socket unsupported, fallback to IPv4
			family = AF_INET;
			create(aType);
		}
	}

	type = aType;
	setBlocking(false);
	setSocketOpt(SO_REUSEADDR, 1);
}

void Socket::accept(const Socket& listeningSocket) {
	if(sock != INVALID_SOCKET) {
		disconnect();
	}
	addr sock_addr = { { 0 } };
	socklen_t sz = sizeof(sock_addr);

	do {
		sock = ::accept(listeningSocket.sock, &sock_addr.sa, &sz);
	} while (sock == SOCKET_ERROR && getLastError() == EINTR);
	check(sock);

#ifdef _WIN32
	// Make sure we disable any inherited windows message things for this socket.
	::WSAAsyncSelect(sock, NULL, 0, 0);
#endif

	type = TYPE_TCP;

	setIp(resolveName(sock_addr));

	connected = true;
	setBlocking(false);
}


uint16_t Socket::bind(uint16_t aPort, const string& aIp /* = 0.0.0.0 */){
	addrinfo_p res = resolveAddr(aIp, aPort, AI_PASSIVE | AI_ADDRCONFIG);

	if(::bind(sock, res->ai_addr, res->ai_addrlen) == SOCKET_ERROR) {
		dcdebug("Bind failed, retrying with INADDR_ANY: %s\n", SocketException(getLastError()).getError().c_str());	
		if(res->ai_family == AF_INET6)
			((sockaddr_in6*)res->ai_addr)->sin6_addr = in6addr_any;
		else
			((sockaddr_in*)res->ai_addr)->sin_addr.s_addr = htonl(INADDR_ANY);

		check(::bind(sock, res->ai_addr, res->ai_addrlen));
	}
	socklen_t size = res->ai_addrlen;
	getsockname(sock, res->ai_addr, &size);

	return ntohs(res->ai_family == AF_INET6 ? ((sockaddr_in6*)res->ai_addr)->sin6_port : ((sockaddr_in*)res->ai_addr)->sin_port);
}

void Socket::listen() {
	check(::listen(sock, 20));
	connected = true;
}

void Socket::connect(const string& aAddr, uint16_t aPort) {
	if(sock == INVALID_SOCKET) {
		create(TYPE_TCP);
	}

	addrinfo_p res = resolveAddr(aAddr, aPort, AI_NUMERICSERV);

	// resolveAddr can return more addresses (e.g. IPv4 and IPv6) and we are supposed to connect to all of them
	// since we have experimental IPv6 now, connect only to last address in list (which is IPv4 if it exists)
	auto ai = res.get();
	while(ai->ai_next) ai = ai->ai_next;

	int result;
	do {
		result = ::connect(sock, ai->ai_addr, ai->ai_addrlen);
	} while (result < 0 && getLastError() == EINTR);

	check(result, true);

	connected = true;

	setIp(resolveName((addr&)*ai->ai_addr));
	setPort(aPort);
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

void Socket::socksConnect(const string& aAddr, uint16_t aPort, uint32_t timeout) {
	if(SETTING(SOCKS_SERVER).empty() || SETTING(SOCKS_PORT) == 0) {
		throw SocketException(STRING(SOCKS_FAILED));
	}

	uint64_t start = GET_TICK();

	connect(SETTING(SOCKS_SERVER), static_cast<uint16_t>(SETTING(SOCKS_PORT)));

	if(wait(timeLeft(start, timeout), WAIT_CONNECT) != WAIT_CONNECT) {
		throw SocketException(STRING(SOCKS_FAILED));
	}

	socksAuth(timeLeft(start, timeout));

	ByteVector connStr;

	// Authenticated, let's get on with it...
	connStr.push_back(5);			// SOCKSv5
	connStr.push_back(1);			// Connect
	connStr.push_back(0);			// Reserved

	if(BOOLSETTING(SOCKS_RESOLVE)) {
		connStr.push_back(3);		// Address type: domain name
		connStr.push_back((uint8_t)aAddr.size());
		connStr.insert(connStr.end(), aAddr.begin(), aAddr.end());
	} else {
		auto ai = resolveAddr(aAddr, aPort);

		if(ai->ai_family == AF_INET) {
			connStr.push_back(1);		// Address type: IPv4
			uint8_t* paddr = (uint8_t*)&((sockaddr_in*)ai->ai_addr)->sin_addr;
			connStr.insert(connStr.end(), paddr, paddr+4);
		} else if(ai->ai_family == AF_INET6) {
			connStr.push_back(4);		// Address type: IPv6
			uint8_t* paddr = (uint8_t*)&((sockaddr_in6*)ai->ai_addr)->sin6_addr;
			connStr.insert(connStr.end(), paddr, paddr+16);
		}
	}

	uint16_t port = htons(aPort);
	uint8_t* pport = (uint8_t*)&port;
	connStr.push_back(pport[0]);
	connStr.push_back(pport[1]);

	writeAll(&connStr[0], connStr.size(), timeLeft(start, timeout));

	// We assume we'll get a ipv4 address back...therefore, 10 bytes...
	/// @todo add support for ipv6
	if(readAll(&connStr[0], 10, timeLeft(start, timeout)) != 10) {
		throw SocketException(STRING(SOCKS_FAILED));
	}

	if(connStr[0] != 5 || connStr[1] != 0) {
		throw SocketException(STRING(SOCKS_FAILED));
	}

	in_addr sock_addr;

	memzero(&sock_addr, sizeof(sock_addr));
	sock_addr.s_addr = *((unsigned long*)&connStr[4]);
	setIp(inet_ntoa(sock_addr));
}

void Socket::socksAuth(uint64_t timeout) {
	vector<uint8_t> connStr;

	uint64_t start = GET_TICK();

	if(SETTING(SOCKS_USER).empty() && SETTING(SOCKS_PASSWORD).empty()) {
		// No username and pw, easier...=)
		connStr.push_back(5);			// SOCKSv5
		connStr.push_back(1);			// 1 method
		connStr.push_back(0);			// Method 0: No auth...

		writeAll(&connStr[0], 3, timeLeft(start, timeout));

		if(readAll(&connStr[0], 2, timeLeft(start, timeout)) != 2) {
			throw SocketException(STRING(SOCKS_FAILED));
		}

		if(connStr[1] != 0) {
			throw SocketException(STRING(SOCKS_NEEDS_AUTH));
		}				
	} else {
		// We try the username and password auth type (no, we don't support gssapi)

		connStr.push_back(5);			// SOCKSv5
		connStr.push_back(1);			// 1 method
		connStr.push_back(2);			// Method 2: Name/Password...
		writeAll(&connStr[0], 3, timeLeft(start, timeout));

		if(readAll(&connStr[0], 2, timeLeft(start, timeout)) != 2) {
			throw SocketException(STRING(SOCKS_FAILED));
		}
		if(connStr[1] != 2) {
			throw SocketException(STRING(SOCKS_AUTH_UNSUPPORTED));
		}

		connStr.clear();
		// Now we send the username / pw...
		connStr.push_back(1);
		connStr.push_back((uint8_t)SETTING(SOCKS_USER).length());
		connStr.insert(connStr.end(), SETTING(SOCKS_USER).begin(), SETTING(SOCKS_USER).end());
		connStr.push_back((uint8_t)SETTING(SOCKS_PASSWORD).length());
		connStr.insert(connStr.end(), SETTING(SOCKS_PASSWORD).begin(), SETTING(SOCKS_PASSWORD).end());

		writeAll(&connStr[0], connStr.size(), timeLeft(start, timeout));

		if(readAll(&connStr[0], 2, timeLeft(start, timeout)) != 2) {
			throw SocketException(STRING(SOCKS_AUTH_FAILED));
		}

		if(connStr[1] != 0) {
			throw SocketException(STRING(SOCKS_AUTH_FAILED));
		}
	}
}

int Socket::getSocketOptInt(int option) const {
	int val;
	socklen_t len = sizeof(val);
	check(::getsockopt(sock, SOL_SOCKET, option, (char*)&val, &len));
	return val;
}

void Socket::setSocketOpt(int option, int val) {
	int len = sizeof(val);
	check(::setsockopt(sock, SOL_SOCKET, option, (char*)&val, len));
}

int Socket::read(void* aBuffer, int aBufLen) {
	int len = 0;

	dcassert(type == TYPE_TCP || type == TYPE_UDP);
	do {
		if(type == TYPE_TCP) {
			len = ::recv(sock, (char*)aBuffer, aBufLen, 0);
		} else {
			len = ::recvfrom(sock, (char*)aBuffer, aBufLen, 0, NULL, NULL);
		}
	} while (len < 0 && getLastError() == EINTR);
	check(len, true);

	if(len > 0) {
		stats.totalDown += len;
	}

	return len;
}

int Socket::read(void* aBuffer, int aBufLen, addr &remote) {
	dcassert(type == TYPE_UDP);

	addr remote_addr = { { 0 } };
	socklen_t addr_length = sizeof(remote_addr);

	int len;
	do {
		len = ::recvfrom(sock, (char*)aBuffer, aBufLen, 0, &remote_addr.sa, &addr_length);
	} while (len < 0 && getLastError() == EINTR);

	check(len, true);
	if(len > 0) {
		stats.totalDown += len;
	}
	remote = remote_addr;

	return len;
}

int Socket::readAll(void* aBuffer, int aBufLen, uint64_t timeout) {
	uint8_t* buf = (uint8_t*)aBuffer;
	int i = 0;
	while(i < aBufLen) {
		int j = read(buf + i, aBufLen - i);
		if(j == 0) {
			return i;
		} else if(j == -1) {
			if(wait(timeout, WAIT_READ) != WAIT_READ) {
				return i;
			}
			continue;
		}

		i += j;
	}
	return i;
}

void Socket::writeAll(const void* aBuffer, int aLen, uint64_t timeout) {
	const uint8_t* buf = (const uint8_t*)aBuffer;
	int pos = 0;
	// No use sending more than this at a time...
	int sendSize = getSocketOptInt(SO_SNDBUF);

	while(pos < aLen) {
		int i = write(buf+pos, (int)min(aLen-pos, sendSize));
		if(i == -1) {
			wait(timeout, WAIT_WRITE);
		} else {
			pos+=i;
			stats.totalUp += i;
		}
	}
}

int Socket::write(const void* aBuffer, int aLen) {
	int sent;
	do {
		sent = ::send(sock, (const char*)aBuffer, aLen, 0);
	} while (sent < 0 && getLastError() == EINTR);

	check(sent, true);
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
void Socket::writeTo(const string& aAddr, uint16_t aPort, const void* aBuffer, int aLen, bool proxy) {
	if(aLen <= 0) 
		return;
		
	// Temporary fix to avoid spamming
	if(aPort == 80 || aPort == 2501) {
		LogManager::getInstance()->message("Someone is trying to use your client to spam " + aAddr + ", please urge hub owner to fix this");
		return;
	}
		
	if(aAddr.empty() || aPort == 0) {
		throw SocketException(EADDRNOTAVAIL);
	}

	auto buf = (const uint8_t*)aBuffer;
	if(sock == INVALID_SOCKET) {
		create(TYPE_UDP);
		setSocketOpt(SO_SNDTIMEO, 250);
	}

	dcassert(type == TYPE_UDP);

	int sent;
	if(SETTING(OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5 && proxy) {
		if(udpAddr.sa.sa_family == 0) {
			throw SocketException(STRING(SOCKS_SETUP_ERROR));
		}

		vector<uint8_t> connStr;

		connStr.reserve(aLen + 24);

		connStr.push_back(0);		// Reserved
		connStr.push_back(0);		// Reserved
		connStr.push_back(0);		// Fragment number, always 0 in our case...
		
		if(BOOLSETTING(SOCKS_RESOLVE)) {
			connStr.push_back(3);
			connStr.push_back((uint8_t)aAddr.size());
			connStr.insert(connStr.end(), aAddr.begin(), aAddr.end());
		} else {
			auto ai = resolveAddr(aAddr, aPort);

			if(ai->ai_family == AF_INET) {
				connStr.push_back(1);		// Address type: IPv4
				uint8_t* paddr = (uint8_t*)&((sockaddr_in*)ai->ai_addr)->sin_addr;
				connStr.insert(connStr.end(), paddr, paddr+4);
			} else if(ai->ai_family == AF_INET6) {
				connStr.push_back(4);		// Address type: IPv6
				uint8_t* paddr = (uint8_t*)&((sockaddr_in6*)ai->ai_addr)->sin6_addr;
				connStr.insert(connStr.end(), paddr, paddr+16);
			}
		}

		connStr.insert(connStr.end(), buf, buf + aLen);

		do {
			sent = ::sendto(sock, (const char*)&connStr[0], connStr.size(), 0, &udpAddr.sa, udpAddrLen);
		} while (sent < 0 && getLastError() == EINTR);
	} else {
		addrinfo_p res = resolveAddr(aAddr, aPort);
		do {
			sent = ::sendto(sock, (const char*)aBuffer, (int)aLen, 0, res->ai_addr, res->ai_addrlen);
		} while (sent < 0 && getLastError() == EINTR);
	}
		
	check(sent);
	stats.totalUp += sent;
}

/**
 * Blocks until timeout is reached one of the specified conditions have been fulfilled
 * @param millis Max milliseconds to block.
 * @param waitFor WAIT_*** flags that set what we're waiting for, set to the combination of flags that
 *				  triggered the wait stop on return (==WAIT_NONE on timeout)
 * @return WAIT_*** ored together of the current state.
 * @throw SocketException Select or the connection attempt failed.
 */
int Socket::wait(uint64_t millis, int waitFor) {
	timeval tv;
	fd_set rfd, wfd, efd;
	fd_set *rfdp = NULL, *wfdp = NULL;
	tv.tv_sec = static_cast<uint32_t>(millis/1000);
	tv.tv_usec = static_cast<uint32_t>((millis%1000)*1000); 

	if(waitFor & WAIT_CONNECT) {
		dcassert(!(waitFor & WAIT_READ) && !(waitFor & WAIT_WRITE));

		int result;
		do {
			FD_ZERO(&wfd);
			FD_ZERO(&efd);
	
			FD_SET(sock, &wfd);
			FD_SET(sock, &efd);
			result = select((int)(sock+1), 0, &wfd, &efd, &tv);
		} while (result < 0 && getLastError() == EINTR);
		check(result);

		if(FD_ISSET(sock, &wfd)) {
			return WAIT_CONNECT;
		}

		if(FD_ISSET(sock, &efd)) {
			int y = 0;
			socklen_t z = sizeof(y);
			check(getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&y, &z));

			if(y != 0)
				throw SocketException(y);
			// No errors! We're connected (?)...
			return WAIT_CONNECT;
		}
		return 0;
	}

	int result;
	do {
		if(waitFor & WAIT_READ) {
			dcassert(!(waitFor & WAIT_CONNECT));
			rfdp = &rfd;
			FD_ZERO(rfdp);
			FD_SET(sock, rfdp);
		}
		if(waitFor & WAIT_WRITE) {
			dcassert(!(waitFor & WAIT_CONNECT));
			wfdp = &wfd;
			FD_ZERO(wfdp);
			FD_SET(sock, wfdp);
		}

		result = select((int)(sock+1), rfdp, wfdp, NULL, &tv);
	} while (result < 0 && getLastError() == EINTR);
	check(result);

	waitFor = WAIT_NONE;

	if(rfdp && FD_ISSET(sock, rfdp)) {
		waitFor |= WAIT_READ;
	}
	if(wfdp && FD_ISSET(sock, wfdp)) {
		waitFor |= WAIT_WRITE;
	}

	return waitFor;
}

bool Socket::waitConnected(uint64_t millis) {
	return wait(millis, Socket::WAIT_CONNECT) == WAIT_CONNECT;
}

bool Socket::waitAccepted(uint64_t /*millis*/) {
	// Normal sockets are always connected after a call to accept
	return true;
}

string Socket::resolve(const string& aDns)
{
	addrinfo_p result = resolveAddr(aDns, 0);
	return resolveName((addr&)*result->ai_addr);
}

Socket::addrinfo_p Socket::resolveAddr(const string& aDns, uint16_t port, int flags) {
	addrinfo hints = { 0 };
	addrinfo* result;
	hints.ai_family = family;
	hints.ai_flags = flags | (family == AF_INET6 ? (AI_ALL | AI_V4MAPPED) : 0);

	string dns = aDns;

	// zone ID should be removed from IP
	string::size_type suffix = aDns.find('%');
	if(suffix != string::npos)
		dns = dns.substr(0, suffix);
	
	// getaddrinfo isn't able to map 0.0.0.0 to IPv6 format
	if(family == AF_INET6 && dns == "0.0.0.0")
		dns = "::";

	int ret = ::getaddrinfo(dns.c_str(), port == 0 ? NULL : Util::toString(port).c_str(), &hints, &result);
	if(ret != 0)
		throw SocketException(ret);

	dcdebug("Resolved %s:%d to %s, next is %p\n", aDns.c_str(), port,
		resolveName((addr&)*result->ai_addr, NULL).c_str(), result->ai_next);

	return addrinfo_p(result, &freeaddrinfo);
}

string Socket::resolveName(const addr& serv_addr, uint16_t* port) {
	char buf[NI_MAXHOST];
	check(::getnameinfo((sockaddr*)&serv_addr, (serv_addr.sas.ss_family == AF_INET6) ? sizeof(serv_addr.sai6) : sizeof(serv_addr.sai), buf, sizeof(buf), NULL, 0, NI_NUMERICHOST));
	string ip(buf);

	switch(serv_addr.sas.ss_family) {
        case AF_INET:
			if(port != NULL) *port = serv_addr.sai.sin_port;
            break;

        case AF_INET6:
			if(port != NULL) *port = serv_addr.sai6.sin6_port;

			// if it is IPv4 mapped address then convert to IPv4
			if(IN6_IS_ADDR_V4MAPPED(&serv_addr.sai6.sin6_addr))
				ip = ip.substr(7);
            break;

        default:
            dcassert(0);
    }

	return ip;
}

string Socket::getLocalIp() const noexcept {
	if(sock == INVALID_SOCKET)
		return Util::emptyString;

	addr sock_addr;
	socklen_t len = sizeof(sock_addr);
	if(getsockname(sock, &sock_addr.sa, &len) == 0) {
		return resolveName(sock_addr);
	}

	return Util::emptyString;
}

uint16_t Socket::getLocalPort() noexcept {
	if(sock == INVALID_SOCKET)
		return 0;

	addr sock_addr;
	socklen_t len = sizeof(sock_addr);
	if(getsockname(sock, &sock_addr.sa, &len) == 0) {
		if(sock_addr.sa.sa_family == AF_INET) {
			return ntohs(sock_addr.sai.sin_port);
		} else if(sock_addr.sa.sa_family == AF_INET6) {
			return ntohs(sock_addr.sai6.sin6_port);
		}
	}
	return 0;
}

void Socket::socksUpdated() {
	memset(&udpAddr, 0, sizeof(udpAddr));
	udpAddrLen = sizeof(udpAddr);
	
	if(SETTING(OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5) {
		try {
			Socket s;
			s.setBlocking(false);
			s.connect(SETTING(SOCKS_SERVER), static_cast<uint16_t>(SETTING(SOCKS_PORT)));
			s.socksAuth(SOCKS_TIMEOUT);

			char connStr[10];
			connStr[0] = 5;			// SOCKSv5
			connStr[1] = 3;			// UDP Associate
			connStr[2] = 0;			// Reserved
			connStr[3] = 1;			// Address type: IPv4;
			*((long*)(&connStr[4])) = 0;		// No specific outgoing UDP address
			*((uint16_t*)(&connStr[8])) = 0;	// No specific port...
			
			s.writeAll(connStr, 10, SOCKS_TIMEOUT);
			
			// We assume we'll get a ipv4 address back...therefore, 10 bytes...if not, things
			// will break, but hey...noone's perfect (and I'm tired...)...
			if(s.readAll(connStr, 10, SOCKS_TIMEOUT) != 10) {
				return;
			}

			if(connStr[0] != 5 || connStr[1] != 0) {
				return;
			}

			udpAddr.sa.sa_family = AF_INET;	// TODO: what about AF_INET6?
			udpAddr.sai.sin_port = *((uint16_t*)(&connStr[8]));
			udpAddr.sai.sin_addr.S_un.S_addr = *((long*)(&connStr[4]));
			udpAddrLen = sizeof(udpAddr.sai);
		} catch(const SocketException&) {
			dcdebug("Socket: Failed to register with socks server\n");
		}
	}
}

void Socket::shutdown() noexcept {
	if(sock != INVALID_SOCKET)
		::shutdown(sock, 2);
}

void Socket::close() noexcept {
	if(sock != INVALID_SOCKET) {
#ifdef _WIN32
		::closesocket(sock);
#else
		::close(sock);
#endif
		connected = false;
		sock = INVALID_SOCKET;
	}
}

void Socket::disconnect() noexcept {
	shutdown();
	close();
}

string Socket::getRemoteHost(const string& aIp) {
	if(aIp.empty())
		return Util::emptyString;
	hostent *h = NULL;
	unsigned int addr;
	addr = inet_addr(aIp.c_str());

	h = gethostbyaddr(reinterpret_cast<char *>(&addr), 4, AF_UNSPEC);
	if (h == NULL) {
		return Util::emptyString;
	} else {
		return h->h_name;
	}
}

#define UNSPEC_IP	(family == AF_INET6 ? "::" : "0.0.0.0")
string Socket::getBindAddress() {
	if(SettingsManager::getInstance()->isDefault(SettingsManager::BIND_INTERFACE))
		return UNSPEC_IP;

	// care about win32 only now (see wx build for *nix version)
	ULONG len =	8192; // begin with 8 kB, it should be enough in most of cases
	for(int i = 0; i < 3; ++i)
	{
		PIP_ADAPTER_ADDRESSES adapterInfo = (PIP_ADAPTER_ADDRESSES)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
		ULONG ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, adapterInfo, &len);

		if(ret == ERROR_SUCCESS)
		{
			for(PIP_ADAPTER_ADDRESSES pAdapterInfo = adapterInfo; pAdapterInfo != NULL; pAdapterInfo = pAdapterInfo->Next)
			{
				if(SETTING(BIND_INTERFACE) != pAdapterInfo->AdapterName)
					continue;

				// we want only enabled ethernet interfaces
				if(pAdapterInfo->FirstUnicastAddress && pAdapterInfo->OperStatus == IfOperStatusUp && (pAdapterInfo->IfType == IF_TYPE_ETHERNET_CSMACD || pAdapterInfo->IfType == IF_TYPE_IEEE80211))
				{
					string ip = resolveName((addr&)*pAdapterInfo->FirstUnicastAddress->Address.lpSockaddr);
					HeapFree(GetProcessHeap(), 0, adapterInfo);
					return ip;
				}

				break;
			}
		}

		HeapFree(GetProcessHeap(), 0, adapterInfo);

		if(ret != ERROR_BUFFER_OVERFLOW)
			break;
	}

	return UNSPEC_IP;	// no interface found, return unspecified address
}

} // namespace dcpp

/**
 * @file
 * $Id: Socket.cpp 576 2011-08-29 17:50:49Z bigmuscle $
 */
