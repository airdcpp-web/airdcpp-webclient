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

#ifndef DCPLUSPLUS_DCPP_CONNECTION_MANAGER_H
#define DCPLUSPLUS_DCPP_CONNECTION_MANAGER_H

#include "ClientManagerListener.h"
#include "ConnectionManagerListener.h"
#include "TimerManagerListener.h"

#include "ConnectionType.h"
#include "CriticalSection.h"
#include "FloodCounter.h"
#include "HintedUser.h"
#include "Singleton.h"
#include "UserConnection.h"

namespace dcpp {

class SocketException;

class TokenManager {
public:
	string createToken(ConnectionType aConnType) noexcept;
	bool addToken(const string& aToken, ConnectionType aConnType) noexcept;
	void removeToken(const string& aToken) noexcept;
	bool hasToken(const string& aToken, ConnectionType aConnType) const noexcept;
private:
	unordered_map<string, ConnectionType> tokens;
	static FastCriticalSection cs;
};

class ConnectionQueueItem : boost::noncopyable, public Flags {
public:
	typedef ConnectionQueueItem* Ptr;
	typedef vector<Ptr> List;
	typedef List::const_iterator Iter;
	
	enum State {
		CONNECTING,					// Recently sent request to connect
		WAITING,					// Waiting to send request to connect
		ACTIVE,						// In one up/downmanager
		RUNNING						// Running/idle
	};

	enum Flags {
		FLAG_MCN1				= 0x01,
		FLAG_REMOVE				= 0x08
	};

	enum DownloadType {
		TYPE_ANY,
		TYPE_SMALL,
		TYPE_SMALL_CONF,
		TYPE_MCN_NORMAL
	};

	ConnectionQueueItem(const HintedUser& aUser, ConnectionType aConntype, const string& aToken);
	
	GETSET(string, token, Token);
	IGETSET(DownloadType, downloadType, DownloadType, TYPE_ANY);
	GETSET(string, lastBundle, LastBundle);
	IGETSET(uint64_t, lastAttempt, LastAttempt, 0);
	IGETSET(int, errors, Errors, 0); // Number of connection errors, or -1 after a protocol error
	IGETSET(State, state, State, WAITING);
	IGETSET(uint8_t, maxConns, MaxConns, 0);
	GETSET(ConnectionType, connType, ConnType);

	const string& getHubUrl() const noexcept { return user.hint; }
	void setHubUrl(const string& aHubUrl) noexcept { user.hint = aHubUrl; }
	const HintedUser& getUser() const noexcept { return user; }
	bool allowNewConnections(int running) const noexcept;
private:
	HintedUser user;
};

class ExpectedMap {
public:
	void add(const string& aKey, const string& aMyNick, const string& aHubUrl) noexcept {
		Lock l(cs);
		expectedConnections.emplace(aKey, make_pair(aMyNick, aHubUrl));
	}

	StringPair remove(const string& aKey) {
		Lock l(cs);
		auto i = expectedConnections.find(aKey);
		if(i == expectedConnections.end()) 
			return make_pair(Util::emptyString, Util::emptyString);

		StringPair tmp = std::move(i->second);
		expectedConnections.erase(i);
		
		return tmp;
	}

private:
	/** Nick -> myNick, hubUrl for expected NMDC incoming connections */
	/** Token, hubUrl for expected ADC incoming connections */
	typedef unordered_map<string, StringPair> ExpectMap;
	ExpectMap expectedConnections;

	CriticalSection cs;
};

// Comparing with a user...
inline bool operator==(ConnectionQueueItem::Ptr ptr, const UserPtr& aUser) noexcept { return ptr->getUser() == aUser; }
// With a token
inline bool operator==(ConnectionQueueItem::Ptr ptr, const string& aToken) noexcept { return compare(ptr->getToken(), aToken) == 0; }

class ConnectionManager : public Speaker<ConnectionManagerListener>, public ClientManagerListener,
	public UserConnectionListener, TimerManagerListener, 
	public Singleton<ConnectionManager>
{
public:
	TokenManager tokens;
	void nmdcExpect(const string& aNick, const string& aMyNick, const string& aHubUrl) noexcept {
		expectedConnections.add(aNick, aMyNick, aHubUrl);
	}
	//expecting to get connection from a passive user
	void adcExpect(const string& aToken, const CID& aCID, const string& aHubUrl) noexcept {
		expectedConnections.add(aToken, aCID.toBase32(), aHubUrl);
	}

	void nmdcConnect(const string& aServer, const string& aPort, const string& aMyNick, const string& aHubUrl, const string& aEncoding, bool aSecure) noexcept;
	void nmdcConnect(const string& aServer, const string& aPort, const string& aLocalPort, BufferedSocket::NatRoles aNatRole, const string& aNick, const string& aHubUrl, const string& aEncoding, bool aSecure) noexcept;
	void adcConnect(const OnlineUser& aUser, const string& aPort, const string& aToken, bool aSecure) noexcept;
	void adcConnect(const OnlineUser& aUser, const string& aPort, const string& aLocalPort, BufferedSocket::NatRoles aNatRole, const string& aToken, bool aSecure) noexcept;

	void getDownloadConnection(const HintedUser& aUser, bool aSmallSlot = false) noexcept;
	void force(const string& aToken) noexcept;
	
	void disconnect(const UserPtr& aUser) noexcept; // disconnect all connections to the user
	void disconnect(const UserPtr& aUser, ConnectionType aConnType) noexcept;
	void disconnect(const string& aToken) noexcept;
	bool setBundle(const string& aToken, const string& aBundleToken) noexcept;

	void shutdown(function<void (float)> progressF) noexcept;
	bool isShuttingDown() const noexcept { return shuttingDown; }

	/** Find a suitable port to listen on, and start doing it, throws in case of errors (e.g. port taken) */
	void listen();
	void disconnect() noexcept;

	const string& getPort() const noexcept;
	const string& getSecurePort() const noexcept;

	void addRunningMCN(const UserConnection *aSource) noexcept;

	// set fatalError to true if the client shouldn't try to reconnect automatically
	void failDownload(const string& aToken, const string& aError, bool aFatalError) noexcept;

	SharedMutex& getCS() noexcept { return cs; }

	// Unsafe
	const ConnectionQueueItem::List& getTransferConnections(bool aDownloads) const {
		return aDownloads ? cqis[CONNECTION_TYPE_DOWNLOAD] : cqis[CONNECTION_TYPE_UPLOAD];
	}

	bool isMCNUser(const UserPtr& aUser) const noexcept;
private:
	FloodCounter floodCounter;

	bool allowNewMCN(const ConnectionQueueItem* aCQI) noexcept;
	void createNewMCN(const HintedUser& aUser) noexcept;

	class Server : public Thread {
	public:
		Server(bool secure, const string& port_, const string& ipv4, const string& ipv6);
		virtual ~Server() { die = true; join(); }

		const string& getPort() const { return port; }

	private:
		virtual int run() noexcept;

		Socket sock;
		string port;
		const bool secure;
		bool die = false;
	};

	friend class Server;

	mutable SharedMutex cs;

	/** All ConnectionQueueItems */
	ConnectionQueueItem::List cqis[CONNECTION_TYPE_LAST],
		&downloads; // shortcut

	/** All active connections */
	UserConnectionList userConnections;

	StringList features;
	StringList adcFeatures;

	ExpectedMap expectedConnections;
	typedef unordered_map<string, uint64_t> delayMap;
	typedef delayMap::iterator delayIter;
	delayMap delayedTokens;

	unique_ptr<Server> server;
	unique_ptr<Server> secureServer;

	bool shuttingDown = false;

	friend class Singleton<ConnectionManager>;
	ConnectionManager();

	~ConnectionManager() { }
	
	UserConnection* getConnection(bool aNmdc, bool aSecure) noexcept;
	void putConnection(UserConnection* aConn) noexcept;

	void addUploadConnection(UserConnection* uc) noexcept;
	void addDownloadConnection(UserConnection* uc) noexcept;
	void addPMConnection(UserConnection* uc) noexcept;

	ConnectionQueueItem* getCQI(const HintedUser& aUser, ConnectionType aConnType, const string& aToken = Util::emptyString) noexcept;
	void putCQI(ConnectionQueueItem* cqi) noexcept;

	void accept(const Socket& sock, bool aSecure) noexcept;

	FloodCounter::FloodLimits getIncomingConnectionLimits(const string& aIP) const noexcept;

	bool checkKeyprint(UserConnection *aSource) noexcept;

	void failed(UserConnection* aSource, const string& aError, bool aProtocolError) noexcept;
	
	// UserConnectionListener
	void on(Connected, UserConnection*) noexcept;
	void on(Failed, UserConnection*, const string&) noexcept;
	void on(ProtocolError, UserConnection*, const string&) noexcept;
	void on(CLock, UserConnection*, const string&) noexcept;
	void on(Key, UserConnection*, const string&) noexcept;
	void on(Direction, UserConnection*, const string&, const string&) noexcept;
	void on(MyNick, UserConnection*, const string&) noexcept;
	void on(Supports, UserConnection*, const StringList&) noexcept;

	void on(AdcCommand::SUP, UserConnection*, const AdcCommand&) noexcept;
	void on(AdcCommand::INF, UserConnection*, const AdcCommand&) noexcept;
	void on(AdcCommand::STA, UserConnection*, const AdcCommand&) noexcept;

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool) noexcept { onUserUpdated(aUser.getUser()); }
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool) noexcept { onUserUpdated(aUser); }

	void onUserUpdated(const UserPtr& aUser) noexcept;
	void attemptDownloads(uint64_t aTick, StringList& removedTokens_) noexcept;
};

} // namespace dcpp

#endif // !defined(CONNECTION_MANAGER_H)