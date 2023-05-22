/*
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_BUFFERED_SOCKET_H
#define DCPLUSPLUS_DCPP_BUFFERED_SOCKET_H

#include <deque>
#include <memory>

#include "typedefs.h"

#include "AddressInfo.h"
#include "BufferedSocketListener.h"
#include "GetSet.h"
#include "Semaphore.h"
#include "Thread.h"
#include "Socket.h"
#include "Speaker.h"

namespace dcpp {

using std::deque;
using std::function;
using std::pair;
using std::unique_ptr;

class BufferedSocket : public Speaker<BufferedSocketListener>, public Thread {
public:
	enum Modes {
		MODE_LINE,
		MODE_ZPIPE,
		MODE_DATA
	};

	enum NatRoles {
		NAT_NONE,
		NAT_CLIENT,
		NAT_SERVER
	};

	/**
	 * BufferedSocket factory, each BufferedSocket may only be used to create one connection
	 * @param sep Line separator
	 * @return An unconnected socket
	 */
	static BufferedSocket* getSocket(char sep, bool v4only = false) {
		return new BufferedSocket(sep, v4only);
	}

	static void putSocket(BufferedSocket* aSock, function<void ()> f = nullptr) {
		if(aSock) {
			aSock->removeListeners();
			aSock->shutdown(f);
		}
	}

	static void waitShutdown() noexcept {
		while(sockets > 0)
			Thread::sleep(100);
	}

	typedef std::function<bool(const string& aIP)> SocketAcceptFloodF;
	void accept(const Socket& srv, bool aSecure, bool aAllowUntrusted, const SocketAcceptFloodF& aFloodCheckF);
	void connect(const AddressInfo& aAddress, const string& aPort, bool aSecure, bool aAllowUntrusted, bool aProxy, const string& expKP = Util::emptyString);
	void connect(const AddressInfo& aAddress, const string& aPort, const string& aLocalPort, NatRoles aNatRole, bool aSecure, bool aAllowUntrusted, bool aProxy, const string& expKP = Util::emptyString);

	/** Sets data mode for aBytes bytes. Must be called within onLine. */
	void setDataMode(int64_t aBytes = -1) { mode = MODE_DATA; dataBytes = aBytes; }
	/**
	 * Rollback is an ugly hack to solve problems with compressed transfers where not all data received
	 * should be treated as data.
	 * Must be called from within onData.
	 */
	void setLineMode(size_t aRollback) { setMode (MODE_LINE, aRollback);}
	void setMode(Modes mode, size_t aRollback = 0);
	Modes getMode() const { return mode; }

	const string& getIp() const { return sock ? sock->getIp() : Util::emptyString; }
	bool isSecure() const { return sock ? sock->isSecure() : false; }
	bool isTrusted() const { return sock ? sock->isTrusted() : false; }
	bool isKeyprintMatch() const { return sock ? sock->isKeyprintMatch() : false; }
	std::string getEncryptionInfo() const { return sock ? sock->getEncryptionInfo() : Util::emptyString; }
	ByteVector getKeyprint() const { return sock ? sock->getKeyprint() : ByteVector(); }
	bool verifyKeyprint(const string& expKeyp, bool allowUntrusted) noexcept { return sock ? sock->verifyKeyprint(expKeyp, allowUntrusted) : false; };
	string getLocalIp() const { return sock ? sock->getLocalIp() : Util::emptyString; }
	uint16_t getLocalPort() const { return sock ? sock->getLocalPort() : 0; }
	bool isV6Valid() const { return sock ? sock->isV6Valid() : false; }

	void write(const string& aData) { write(aData.data(), aData.length()); }
	void write(const char* aBuf, size_t aLen) noexcept;
	/** Send the file f over this socket. */
	void transmitFile(InputStream* f) { Lock l(cs); addTask(SEND_FILE, new SendFileInfo(f)); }

	/** Call a function from the socket's thread. */
	void callAsync(function<void ()> f) { Lock l(cs); addTask(ASYNC_CALL, new CallData(f)); }

	void disconnect(bool graceless = false) noexcept { Lock l(cs); if(graceless) disconnecting = true; addTask(DISCONNECT, 0); }
	bool isDisconnecting() const noexcept { return disconnecting; }

	GETSET(char, separator, Separator);
	IGETSET(bool, useLimiter, UseLimiter, false);
private:
	enum Tasks {
		CONNECT,
		DISCONNECT,
		SEND_DATA,
		SEND_FILE,
		SHUTDOWN,
		ACCEPTED,
		ASYNC_CALL
	};

	enum State {
		STARTING, // Waiting for CONNECT/ACCEPTED/SHUTDOWN
		RUNNING,
		FAILED
	};

	struct TaskData {
		virtual ~TaskData() { }
	};
	struct ConnectInfo : public TaskData {
		ConnectInfo(AddressInfo addr_, string port_, string localPort_, NatRoles natRole_, bool proxy_) : addr(addr_), port(port_), localPort(localPort_), natRole(natRole_), proxy(proxy_) {}
		AddressInfo addr;
		string port;
		string localPort;
		NatRoles natRole;
		bool proxy;
	};
	struct SendFileInfo : public TaskData {
		SendFileInfo(InputStream* stream_) : stream(stream_) { }
		InputStream* stream;
	};
	struct CallData : public TaskData {
		CallData(function<void ()> f) : f(f) { }
		function<void ()> f;
	};

	BufferedSocket(char aSeparator, bool v4only);

	virtual ~BufferedSocket();

	CriticalSection cs;

	Semaphore taskSem;
	deque<pair<Tasks, unique_ptr<TaskData> > > tasks;

	Modes mode = MODE_LINE;
	std::unique_ptr<UnZFilter> filterIn;
	int64_t dataBytes = 0;
	size_t rollback = 0;
	string line;
	ByteVector inbuf;
	ByteVector writeBuf;
	ByteVector sendBuf;

	std::unique_ptr<Socket> sock;
	State state = STARTING;
	bool disconnecting = false;
	bool v4only;

	virtual int run();

	void threadConnect(const AddressInfo& aAddr, const string& aPort, const string& localPort, NatRoles natRole, bool proxy);
	void threadAccept();
	void threadRead();
	void threadSendFile(InputStream* is);
	void threadSendData();

	void fail(const string& aError);
	static atomic<long> sockets;

	bool checkEvents();
	void checkSocket();

	void setSocket(std::unique_ptr<Socket>&& s);
	void setOptions();
	void shutdown(function<void ()> f);
	void addTask(Tasks task, TaskData* data);
};

} // namespace dcpp

#endif // !defined(BUFFERED_SOCKET_H)
