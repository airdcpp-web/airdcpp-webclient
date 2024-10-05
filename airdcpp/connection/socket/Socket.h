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

#ifndef DCPLUSPLUS_DCPP_SOCKET_H
#define DCPLUSPLUS_DCPP_SOCKET_H

#ifdef _WIN32

#include <airdcpp/core/header/w.h>

typedef int socklen_t;
typedef SOCKET socket_t;

#else

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

typedef int socket_t;
const int INVALID_SOCKET = -1;
#define SOCKET_ERROR -1
#endif

#include <airdcpp/connection/socket/AddressInfo.h>
#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/util/Util.h>
#include <airdcpp/core/classes/Exception.h>

#include <boost/noncopyable.hpp>
#include <memory>

namespace dcpp {

class SocketException : public Exception {
public:
#ifdef _DEBUG
	explicit SocketException(const string& aError) noexcept : Exception("SocketException: " + aError) { }
#else //_DEBUG
	SocketException(const string& aError) noexcept : Exception(aError) { }
#endif // _DEBUG
	explicit SocketException(int aError) noexcept;
	~SocketException() noexcept override = default;
private:
	static string errorToString(int aError) noexcept;
};

/** RAII socket handle */
class SocketHandle : public boost::noncopyable {
public:
	SocketHandle() : sock(INVALID_SOCKET) { }
	explicit SocketHandle(socket_t sock) : sock(sock) { }
	~SocketHandle() { reset(); }

	operator socket_t() const { return get(); }
	SocketHandle& operator=(socket_t s) { reset(s); return *this; }

	socket_t get() const { return sock; }
	bool valid() const { return sock != INVALID_SOCKET; }
	void reset(socket_t s = INVALID_SOCKET);
private:
	socket_t sock;
};

class Socket : public boost::noncopyable
{
public:
	enum SocketType {
		TYPE_TCP = IPPROTO_TCP,
		TYPE_UDP = IPPROTO_UDP
	};

	using addrinfo_p = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>;
	using AddrinfoList = vector<addrinfo_p>;

	explicit Socket(SocketType type) : type(type) { }

	virtual ~Socket() = default;

	/**
	 * Connects a socket to an address/ip, closing any other connections made with
	 * this instance.
	 * @param aAddr Server address, in dns or xxx.xxx.xxx.xxx format.
	 * @param aPort Server port.
	 * @throw SocketException If any connection error occurs.
	 */
	virtual void connect(const AddressInfo& aAddr, const string& aPort, const string& aLocalPort = Util::emptyString);
	void connect(const AddressInfo& aAddr, uint16_t aPort, uint16_t localPort = 0) { connect(aAddr, aPort == 0 ? Util::emptyString : Util::toString(aPort), localPort == 0 ? Util::emptyString : Util::toString(localPort)); }

	/**
	 * Same as connect(), but through the SOCKS5 server
	 */
	void socksConnect(const AddressInfo& aIp, const string& aPort, uint64_t timeout = 0);

	virtual int write(const void* aBuffer, size_t aLen);
	int write(const string_view& aData) { return write(aData.data(), aData.length()); }
	virtual void writeTo(const string& aIp, const string& aPort, const void* aBuffer, size_t aLen);
	void writeTo(const string& aIp, const string& aPort, const string_view& aData) { writeTo(aIp, aPort, aData.data(), aData.length()); }
	virtual void shutdown() noexcept;
	virtual void close() noexcept;
	void disconnect() noexcept;

	virtual bool waitConnected(uint64_t millis);
	virtual bool waitAccepted(uint64_t millis);

	/**
	 * Reads zero to aBufLen characters from this socket,
	 * @param aBuffer A buffer to store the data in.
	 * @param aBufLen Size of the buffer.
	 * @return Number of bytes read, 0 if disconnected and -1 if the call would block.
	 * @throw SocketException On any failure.
	 */
	virtual int read(void* aBuffer, size_t aBufLen);
	/**
	 * Reads zero to aBufLen characters from this socket,
	 * @param aBuffer A buffer to store the data in.
	 * @param aBufLen Size of the buffer.
	 * @param aIP Remote IP address
	 * @return Number of bytes read, 0 if disconnected and -1 if the call would block.
	 * @throw SocketException On any failure.
	 */
	virtual int read(void* aBuffer, size_t aBufLen, string &aIP);

	virtual std::pair<bool, bool> wait(uint64_t millis, bool checkRead, bool checkWrite);

	static string resolve(const string& aDns, int af = AF_UNSPEC) noexcept;
	addrinfo_p resolveAddr(const string& name, const string& port, int family = AF_UNSPEC, int flags = 0) const;

	static uint64_t getTotalDown() { return stats.totalDown; }
	static uint64_t getTotalUp() { return stats.totalUp; }

	void setBlocking(bool block) noexcept;

	string getLocalIp() const noexcept;
	uint16_t getLocalPort() const noexcept;

	/** Binds a socket to a certain local port and possibly IP. */
	virtual string listen(const string& port);
	/** Accept a socket.
	@return remote port */
	virtual uint16_t accept(const Socket& listeningSocket);

	int getSocketOptInt(int option);
	void setSocketOpt(int option, int value);

	virtual bool isSecure() const noexcept { return false; }
	virtual bool isTrusted() const noexcept { return false; }
	virtual bool isKeyprintMatch() const noexcept { return true; }
	virtual std::string getEncryptionInfo() const noexcept { return Util::emptyString; }
	virtual ByteVector getKeyprint() const noexcept{ return ByteVector(); }
	virtual bool verifyKeyprint(const string&, bool) noexcept{ return true; };

	/** When socks settings are updated, this has to be called... */
	static void socksUpdated();
	static bool socksUdpInitialized() noexcept;

	static int getLastError();

	GETSET(string, ip4, Ip4);
	GETSET(string, ip6, Ip6);
	GETSET(string, localIp4, LocalIp4);
	GETSET(string, localIp6, LocalIp6);
	IGETSET(bool, v4only, V4only, false);

	const string& getIp() const noexcept;
	bool isV6Valid() const noexcept;
	static string resolveName(const sockaddr* sa, socklen_t sa_len, int flags = NI_NUMERICHOST);
protected:
	using addr = union {
		sockaddr sa;
		sockaddr_in sai;
		sockaddr_in6 sai6;
		sockaddr_storage sas;
	};

	socket_t getSock() const;
	bool hasSocket() const noexcept;

	mutable SocketHandle sock4;
	mutable SocketHandle sock6;

	SocketType type;

	class Stats {
	public:
		uint64_t totalDown;
		uint64_t totalUp;
	};
	static Stats stats;
private:
	enum SocksAddrType {
		TYPE_V4 = 1,
		TYPE_DOMAIN = 3,
		TYPE_V6 = 4,
	};

	void connect(const string& aAddr, const string& aPort, const string& localPort, int aFamily, string& lastError_);

	socket_t setSock(socket_t s, int af);

	// Low level interface
	socket_t create(const addrinfo& ai);


	// SOCKS5
	static addr udpAddr;
	static socklen_t udpAddrLen;

	static void socksParseResponseAddress(const ByteVector& aData, size_t aDataLength, addr& addr_);

	using SocksConstructConnF = std::function<void(ByteVector& connStr_)>;
	void socksConnect(addr& addr_, const SocksConstructConnF& aConstructConnStr, uint64_t aTimeout);
	void appendSocksAddress(const string& aName, const string& aPort, ByteVector& connStr_) const;

	/**
	 * Sends data, will block until all data has been sent or an exception occurs
	 * @param aBuffer Buffer with data
	 * @param aLen Data length
	 * @throw SocketExcpetion Send failed.
	 */
	void socksWrite(const void* aBuffer, size_t aLen, uint64_t timeout = 0);

	using SocksCompleteF = std::function<bool(const ByteVector&, int)>;
	/**
	 * Reads data until aIsComplete returns true.
	 * If the socket is closed, or the timeout is reached, the number of bytes read
	 * actually read is returned.
	 * On exception, an unspecified amount of bytes might have already been read.
	 */
	int socksRead(ByteVector& aBuffer, size_t aBufLen, const SocksCompleteF& aIsComplete, uint64_t aTimeout = 0);

	/**
	 * Reads data until aBufLen bytes have been read or an error occurs.
	 * If the socket is closed, or the timeout is reached, the number of bytes read
	 * actually read is returned.
	 * On exception, an unspecified amount of bytes might have already been read.
	 */
	int socksRead(ByteVector& aBuffer, size_t aBufLen, uint64_t timeout = 0);

	void socksAuth(uint64_t timeout);
};

} // namespace dcpp

#endif // !defined(SOCKET_H)
