/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_CONNECTION_MANAGER_H
#define DCPLUSPLUS_DCPP_CONNECTION_MANAGER_H

#include "TimerManager.h"
#include "ClientManagerListener.h"
#include "ConnectionManagerListener.h"

#include "ConnectionType.h"
#include "CriticalSection.h"
#include "HintedUser.h"
#include "Singleton.h"
#include "UserConnection.h"

namespace dcpp {

class SocketException;

class TokenManager {
public:
	string makeToken() const noexcept;
	string getToken(ConnectionType aConnType) noexcept;
	bool addToken(const string& aToken, ConnectionType aConnType) noexcept;
	void removeToken(const string& aToken) noexcept;
	bool hasToken(const string& aToken, ConnectionType aConnType) noexcept;
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

	ConnectionQueueItem(const HintedUser& aUser, ConnectionType aConntype, const string& aToken) : token(aToken), 
		downloadType(TYPE_ANY), connType(aConntype),
		lastAttempt(0), errors(0), state(WAITING), maxConns(0), hubUrl(aUser.hint), user(aUser.user) {
	}
	
	GETSET(string, token, Token);
	GETSET(DownloadType, downloadType, DownloadType);
	GETSET(string, lastBundle, LastBundle);
	GETSET(uint64_t, lastAttempt, LastAttempt);
	GETSET(int, errors, Errors); // Number of connection errors, or -1 after a protocol error
	GETSET(State, state, State);
	GETSET(uint8_t, maxConns, MaxConns);
	GETSET(string, hubUrl, HubUrl);
	GETSET(ConnectionType, connType, ConnType);

	const UserPtr& getUser() const { return user; }
	//UserPtr& getUser() { return user; }
	const HintedUser getHintedUser() const { return HintedUser(user, hubUrl); }
	bool allowNewConnections(int running) const;
private:
	UserPtr user;
};

class ExpectedMap {
public:
	void add(const string& aKey, const string& aMyNick, const string& aHubUrl) {
		Lock l(cs);
		expectedConnections.emplace(aKey, make_pair(aMyNick, aHubUrl));
	}

	StringPair remove(const string& aKey) {
		Lock l(cs);
		auto i = expectedConnections.find(aKey);
		if(i == expectedConnections.end()) 
			return make_pair(Util::emptyString, Util::emptyString);

		StringPair tmp = move(i->second);
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
inline bool operator==(ConnectionQueueItem::Ptr ptr, const UserPtr& aUser) { return ptr->getUser() == aUser; }
// With a token
inline bool operator==(ConnectionQueueItem::Ptr ptr, const string& aToken) { return compare(ptr->getToken(), aToken) == 0; }

class ConnectionManager : public Speaker<ConnectionManagerListener>, public ClientManagerListener,
	public UserConnectionListener, TimerManagerListener, 
	public Singleton<ConnectionManager>
{
public:
	TokenManager tokens;
	void nmdcExpect(const string& aNick, const string& aMyNick, const string& aHubUrl) {
		expectedConnections.add(aNick, aMyNick, aHubUrl);
	}
	//expecting to get connection from a passive user
	void adcExpect(const string& aToken, const CID& aCID, const string& aHubUrl) {
		expectedConnections.add(aToken, aCID.toBase32(), aHubUrl);
	}

	void nmdcConnect(const string& aServer, const string& aPort, const string& aMyNick, const string& hubUrl, const string& encoding, bool stealth, bool secure);
	void nmdcConnect(const string& aServer, const string& aPort, const string& localPort, BufferedSocket::NatRoles natRole, const string& aNick, const string& hubUrl, const string& encoding, bool stealth, bool secure);
	void adcConnect(const OnlineUser& aUser, const string& aPort, const string& aToken, bool secure);
	void adcConnect(const OnlineUser& aUser, const string& aPort, const string& localPort, BufferedSocket::NatRoles natRole, const string& aToken, bool secure);

	void getDownloadConnection(const HintedUser& aUser, bool smallSlot=false);
	void force(const string& token);
	
	void disconnect(const UserPtr& aUser); // disconnect dall connections to the user
	void disconnect(const UserPtr& aUser, ConnectionType aConnType);
	void disconnect(const string& token);
	bool setBundle(const string& token, const string& bundleToken);

	void shutdown(function<void (float)> progressF);
	bool isShuttingDown() const { return shuttingDown; }

	/** Find a suitable port to listen on, and start doing it */
	void listen();
	void disconnect() noexcept;

	const string& getPort() const;
	const string& getSecurePort() const;

	void addRunningMCN(const UserConnection *aSource) noexcept;

	// set fatalError to true if the client shouldn't try to reconnect automatically
	void failDownload(const string& aToken, const string& aError, bool fatalError);

	SharedMutex& getCS() { return cs; }
	const ConnectionQueueItem::List& getTransferConnections(bool aDownloads) const {
		return aDownloads ? cqis[CONNECTION_TYPE_DOWNLOAD] : cqis[CONNECTION_TYPE_UPLOAD];
	}
private:
	bool allowNewMCN(const ConnectionQueueItem* aCQI);
	void createNewMCN(const HintedUser& aUser);

	class Server : public Thread {
	public:
		Server(bool secure, const string& port_, const string& ipv4, const string& ipv6);
		virtual ~Server() { die = true; join(); }

		const string& getPort() const { return port; }

	private:
		virtual int run() noexcept;

		Socket sock;
		string port;
		bool secure;
		bool die;
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

	uint32_t floodCounter;

	unique_ptr<Server> server;
	unique_ptr<Server> secureServer;

	bool shuttingDown;

	friend class Singleton<ConnectionManager>;
	ConnectionManager();

	~ConnectionManager() { }
	
	UserConnection* getConnection(bool aNmdc, bool secure) noexcept;
	void putConnection(UserConnection* aConn);

	void addUploadConnection(UserConnection* uc);
	void addDownloadConnection(UserConnection* uc);
	void addPMConnection(UserConnection* uc);

	void checkWaitingMCN() noexcept;

	ConnectionQueueItem* getCQI(const HintedUser& aUser, ConnectionType aConnType, const string& aToken = Util::emptyString);
	void putCQI(ConnectionQueueItem* cqi);

	void accept(const Socket& sock, bool secure) noexcept;

	bool checkKeyprint(UserConnection *aSource);

	void failed(UserConnection* aSource, const string& aError, bool protocolError);
	
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

	void onUserUpdated(const UserPtr& aUser);
	void attemptDownloads(uint64_t aTick, StringList& removedTokens);
};

} // namespace dcpp

#endif // !defined(CONNECTION_MANAGER_H)