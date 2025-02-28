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

#include <airdcpp/hub/ClientManagerListener.h>
#include <airdcpp/connection/ConnectionManagerListener.h>
#include <airdcpp/core/timer/TimerManagerListener.h>

#include <airdcpp/protocol/AdcSupports.h>
#include <airdcpp/connection/ConnectionType.h>
#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/core/classes/FloodCounter.h>
#include <airdcpp/user/HintedUser.h>
#include <airdcpp/queue/QueueDownloadInfo.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/connection/UserConnection.h>

namespace dcpp {

class SocketException;

class TokenManager {
public:
	~TokenManager();

	string createToken(ConnectionType aConnType) noexcept;
	bool addToken(const string& aToken, ConnectionType aConnType) noexcept;
	void removeToken(const string& aToken) noexcept;
	bool hasToken(const string& aToken, ConnectionType aConnType = CONNECTION_TYPE_LAST) const noexcept;
private:
	unordered_map<string, ConnectionType> tokens;
	static FastCriticalSection cs;
};

class ConnectionQueueItem : public boost::noncopyable, public Flags {
public:
	using Ptr = ConnectionQueueItem *;
	using List = vector<Ptr>;
	
	enum class State {
		CONNECTING,					// Recently sent request to connect
		WAITING,					// Waiting to send request to connect
		ACTIVE,						// In one up/downmanager
	};

	enum Flags {
		FLAG_MCN				= 0x02,
		FLAG_RUNNING			= 0x04,
	};

	ConnectionQueueItem(const HintedUser& aUser, ConnectionType aConntype, const string& aToken);
	
	GETSET(string, token, Token);
	IGETSET(QueueDownloadType, downloadType, DownloadType, QueueDownloadType::ANY);
	GETSET(string, lastBundle, LastBundle);
	IGETSET(uint64_t, lastAttempt, LastAttempt, 0);
	IGETSET(int, errors, Errors, 0); // Number of connection errors, or -1 after a protocol error
	IGETSET(State, state, State, State::WAITING);
	IGETSET(uint8_t, maxRemoteConns, MaxRemoteConns, 0);
	GETSET(ConnectionType, connType, ConnType);

	const string& getHubUrl() const noexcept { return user.hint; }
	void setHubUrl(const string& aHubUrl) noexcept { user.hint = aHubUrl; }
	const HintedUser& getUser() const noexcept { return user; }
	bool allowNewConnections(int running) const noexcept;

	bool isSmallSlot() const noexcept;
	bool isActive() const noexcept;
	bool isRunning() const noexcept;
	bool isMcn() const noexcept;

	bool allowConnect(int aAttempts, int aAttemptLimit, uint64_t aTick) const noexcept;
	bool isTimeout(uint64_t aTick) const noexcept;

	void resetFatalError() noexcept;
private:
	HintedUser user;
};

class ExpectedMap {
public:
	void add(const string& aKey, const string& aMyNick, const string& aHubUrl) noexcept {
		Lock l(cs);
		expectedConnections.try_emplace(aKey, aMyNick, aHubUrl);
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
	using ExpectMap = unordered_map<string, StringPair>;
	ExpectMap expectedConnections;

	CriticalSection cs;
};

// Comparing with a user...
inline bool operator==(ConnectionQueueItem::Ptr ptr, const UserPtr& aUser) noexcept { return ptr->getUser() == aUser; }
// With a token
inline bool operator==(ConnectionQueueItem::Ptr ptr, const string& aToken) noexcept { return compare(ptr->getToken(), aToken) == 0; }

class ConnectionManager : public Speaker<ConnectionManagerListener>, public ClientManagerListener,
	public UserConnectionListener, public TimerManagerListener, 
	public Singleton<ConnectionManager>
{
public:
	AdcSupports userConnectionSupports;

	TokenManager tokens;
	void nmdcExpect(const string& aNick, const string& aMyNick, const string& aHubUrl) noexcept {
		expectedConnections.add(aNick, aMyNick, aHubUrl);
	}
	//expecting to get connection from a passive user
	void adcExpect(const string& aToken, const CID& aCID, const string& aHubUrl) noexcept {
		expectedConnections.add(aToken, aCID.toBase32(), aHubUrl);
	}

	void nmdcConnect(const string& aServer, const SocketConnectOptions& aOptions, const string& aMyNick, const string& aHubUrl, const string& aEncoding) noexcept;
	void nmdcConnect(const string& aServer, const SocketConnectOptions& aOptions, const string& aLocalPort, const string& aNick, const string& aHubUrl, const string& aEncoding) noexcept;
	void adcConnect(const OnlineUser& aUser, const SocketConnectOptions& aOptions, const string& aToken) noexcept;
	void adcConnect(const OnlineUser& aUser, const SocketConnectOptions& aOptions, const string& aLocalPort, const string& aToken) noexcept;

	void getDownloadConnection(const HintedUser& aUser, bool aSmallSlot = false) noexcept;
	void force(const string& aToken) noexcept;
	
	void disconnect(const UserPtr& aUser) noexcept; // disconnect all connections to the user
	void disconnect(const string& aToken) const noexcept;

	void shutdown(const ProgressFunction& progressF) noexcept;
	bool isShuttingDown() const noexcept { return shuttingDown; }

	/** Find a suitable port to listen on, and start doing it, throws in case of errors (e.g. port taken) */
	void listen();
	void disconnect() noexcept;

	const string& getPort() const noexcept;
	const string& getSecurePort() const noexcept;

	// set fatalError to true if the client shouldn't try to reconnect automatically
	void failDownload(const string& aToken, const string& aError, bool aFatalError) noexcept;

	SharedMutex& getCS() noexcept { return cs; }

	// Unsafe
	const ConnectionQueueItem::List& getTransferConnections(bool aDownloads) const {
		return aDownloads ? cqis[CONNECTION_TYPE_DOWNLOAD] : cqis[CONNECTION_TYPE_UPLOAD];
	}

	bool isMCNUser(const UserPtr& aUser) const noexcept;


	using UserConnectionCallback = std::function<void (UserConnection *)>;
	bool findUserConnection(const string& aConnectToken, const UserConnectionCallback& aCallback) const noexcept;
	bool findUserConnection(UserConnectionToken aToken, const UserConnectionCallback& aCallback) const noexcept;
private:
	FloodCounter floodCounter;

	using ConnectionQueueItemCallback = std::function<void (ConnectionQueueItem *)>;

	// Can we create a new regular MCN connection?
	bool allowNewMCNUnsafe(const UserPtr& aUser, bool aSmallSlot, const ConnectionQueueItemCallback& aWaitingCallback = nullptr) const noexcept;

	// Create a new regular MCN connection
	void createNewMCN(const HintedUser& aUser) noexcept;

	// Remove an extra waiting MCN connection (we should keep only one waiting connection)
	void removeExtraMCNUnsafe(const ConnectionQueueItem* aFailedCQI) noexcept;

	void onDownloadRunning(const UserConnection* aSource) noexcept;

	class Server : public Thread {
	public:
		Server(bool secure, const string& port_, const string& ipv4, const string& ipv6);
		~Server() override { die = true; join(); }

		const string& getPort() const { return port; }

	private:
		int run() noexcept override;

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
	using DelayMap = unordered_map<string, uint64_t>;

	// Keep track own our own downloads if they are removed before the handshake is finished
	// (unknown tokens would be shown as uploads)
	DelayMap removedDownloadTokens;

	unique_ptr<Server> server;
	unique_ptr<Server> secureServer;

	bool shuttingDown = false;

	friend class Singleton<ConnectionManager>;
	ConnectionManager();

	~ConnectionManager() override = default;
	
	UserConnection* getConnection(bool aNmdc) noexcept;
	void putConnection(UserConnection* aConn) noexcept;

	void addUploadConnection(UserConnection* uc) noexcept;
	void addDownloadConnection(UserConnection* uc) noexcept;
	void addPMConnection(UserConnection* uc) noexcept;

	ConnectionQueueItem* getCQIUnsafe(const HintedUser& aUser, ConnectionType aConnType, const string& aToken = Util::emptyString) noexcept;
	void putCQIUnsafe(ConnectionQueueItem* cqi) noexcept;
	void putCQI(UserConnection* aSource) noexcept;

	void accept(const Socket& sock, bool aSecure) noexcept;

	FloodCounter::FloodLimits getIncomingConnectionLimits(const string& aIP) const noexcept;

	static bool checkKeyprint(UserConnection *aSource) noexcept;

	void failed(UserConnection* aSource, const string& aError, bool aProtocolError) noexcept;
	
	// UserConnectionListener
	void on(UserConnectionListener::Connected, UserConnection*) noexcept override;
	void on(UserConnectionListener::Failed, UserConnection*, const string&) noexcept override;
	void on(UserConnectionListener::ProtocolError, UserConnection*, const string&) noexcept override;
	void on(UserConnectionListener::CLock, UserConnection*, const string&) noexcept override;
	void on(UserConnectionListener::Key, UserConnection*, const string&) noexcept override;
	void on(UserConnectionListener::Direction, UserConnection*, const string&, const string&) noexcept override;
	void on(UserConnectionListener::MyNick, UserConnection*, const string&) noexcept override;
	void on(UserConnectionListener::Supports, UserConnection*, const StringList&) noexcept override;
	void on(UserConnectionListener::UserSet, UserConnection*) noexcept override;
	void on(UserConnectionListener::State, UserConnection*) noexcept override;

	void on(AdcCommand::SUP, UserConnection*, const AdcCommand&) noexcept override;
	void on(AdcCommand::INF, UserConnection*, const AdcCommand&) noexcept override;
	void on(AdcCommand::STA, UserConnection*, const AdcCommand&) noexcept override;

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool) noexcept override { onUserUpdated(aUser.getUser()); }
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool) noexcept override { onUserUpdated(aUser); }

	void onUserUpdated(const UserPtr& aUser) noexcept;
	void onIdle(const UserConnection* aSource) noexcept;
	void attemptDownloads(uint64_t aTick, StringList& removedTokens_) noexcept;

	bool attemptDownloadUnsafe(ConnectionQueueItem* cqi, StringList& removedTokens_) noexcept;
	bool connectUnsafe(ConnectionQueueItem* cqi, bool aAllowUrlChange) noexcept;

	ConnectionQueueItem* findDownloadUnsafe(const UserConnection* aSource) noexcept;

	StringList getAdcFeatures() const noexcept;
};

} // namespace dcpp

#endif // !defined(CONNECTION_MANAGER_H)