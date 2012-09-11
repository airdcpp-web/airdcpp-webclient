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

#ifndef DCPLUSPLUS_DCPP_CLIENT_H
#define DCPLUSPLUS_DCPP_CLIENT_H

#include "compiler.h"

#include "atomic.h"

#include "forward.h"

#include "User.h"
#include "Speaker.h"
#include "BufferedSocketListener.h"
#include "TimerManager.h"
#include "ClientListener.h"
#include "DebugManager.h"
#include "SearchQueue.h"
#include "OnlineUser.h"
#include "ShareProfile.h"

namespace dcpp {

class ClientBase
{
public:
	
	ClientBase() { }
	
	virtual const string& getHubUrl() const = 0;
	virtual string getHubName() const = 0;
	virtual bool isOp() const = 0;
	virtual void connect(const OnlineUser& user, const string& token) = 0;
	virtual void privateMessage(const OnlineUserPtr& user, const string& aMessage, bool thirdPerson = false) = 0;
	virtual void directSearch(const OnlineUser& user, int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList, const string& aDir) = 0;
};

/** Yes, this should probably be called a Hub */
class Client : public ClientBase, public Speaker<ClientListener>, public BufferedSocketListener, protected TimerManagerListener, private boost::noncopyable {
public:
	typedef unordered_map<string*, Client*, noCaseStringHash, noCaseStringEq> List;
	typedef List::const_iterator Iter;

	virtual void connect();
	virtual void disconnect(bool graceless);

	virtual void connect(const OnlineUser& user, const string& token) = 0;
	virtual void hubMessage(const string& aMessage, bool thirdPerson = false) = 0;
	virtual void privateMessage(const OnlineUserPtr& user, const string& aMessage, bool thirdPerson = false) = 0;
	virtual void sendUserCmd(const UserCommand& command, const ParamMap& params) = 0;

	uint64_t search(int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList, Search::searchType sType, void* owner);
	void cancelSearch(void* aOwner) { searchQueue.cancelSearch(aOwner); }
	
	virtual void password(const string& pwd) = 0;
	virtual void info(bool force) = 0;

	virtual size_t getUserCount() const = 0;
	int64_t getAvailable() const { return availableBytes; };
	
	virtual void send(const AdcCommand& command) = 0;

	bool isConnected() const { return state != STATE_DISCONNECTED; }
	bool isReady() const { return state != STATE_CONNECTING && state != STATE_DISCONNECTED; }
	bool isSecure() const;
	bool isTrusted() const;
	std::string getCipherName() const;
	vector<uint8_t> getKeyprint() const;

	bool isOp() const { return getMyIdentity().isOp(); }

	virtual void refreshUserList(bool) = 0;
	virtual void getUserList(OnlineUserList& list) const = 0;
	virtual OnlineUserPtr findUser(const string& aNick) const = 0;
	
	const string& getPort() const { return port; }
	const string& getAddress() const { return address; }

	const string& getIp() const { return ip; }
	string getIpPort() const { return getIp() + ':' + port; }
	string getLocalIp() const;

	void updated(const OnlineUserPtr& aUser) { fire(ClientListener::UserUpdated(), this, aUser); }

	static int getTotalCounts() {
		return counts[COUNT_NORMAL] + counts[COUNT_REGISTERED] + counts[COUNT_OP];
	}

	static string getCounts();
	
	void setSearchInterval(uint32_t aInterval) {
		// min interval is 5 seconds
		searchQueue.minInterval = max(aInterval, (uint32_t)(5 * 1000));
	}

	uint32_t getSearchInterval() const {
		return searchQueue.minInterval;
	}	
	
	void reconnect();
	void shutdown();
	bool isActive() const;

	void send(const string& aMessage) { send(aMessage.c_str(), aMessage.length()); }
	void send(const char* aMessage, size_t aLen);

	string getMyNick() const { return getMyIdentity().getNick(); }
	string getHubName() const { return getHubIdentity().getNick().empty() ? getHubUrl() : getHubIdentity().getNick(); }
	string getHubDescription() const { return getHubIdentity().getDescription(); }
	
	void Message(const string& msg) {
		fire(ClientListener::AddLine(), this, msg);
	}
	mutable CriticalSection cs;

	Identity& getHubIdentity() { return hubIdentity; }

	const string& getHubUrl() const { return hubUrl; }

	GETSET(Identity, myIdentity, MyIdentity);
	GETSET(Identity, hubIdentity, HubIdentity);

	GETSET(string, defpassword, Password);
	
	GETSET(string, currentNick, CurrentNick);
	GETSET(string, currentDescription, CurrentDescription);
	GETSET(string, favIp, FavIp);
	GETSET(bool, favnoPM, FavNoPM);

	GETSET(uint64_t, lastActivity, LastActivity);
	GETSET(uint32_t, reconnDelay, ReconnDelay);
	
	GETSET(string, encoding, Encoding);	
	
	GETSET(bool, registered, Registered);
	GETSET(bool, autoReconnect, AutoReconnect);
	GETSET(bool, stealth, Stealth);
	GETSET(bool, hubShowJoins, HubShowJoins); // Show joins
	GETSET(bool, hubLogMainchat, HubLogMainchat);
	GETSET(bool, chatNotify, ChatNotify);
	GETSET(ProfileToken, shareProfile, ShareProfile);
	GETSET(ProfileToken, favToken, FavToken);

protected:
	friend class ClientManager;
	Client(const string& hubURL, char separator, bool secure_);
	virtual ~Client();

	enum CountType {
		COUNT_NORMAL,
		COUNT_REGISTERED,
		COUNT_OP,
		COUNT_UNCOUNTED
	};

	static atomic<long> counts[COUNT_UNCOUNTED];

	enum States {
		STATE_CONNECTING,	///< Waiting for socket to connect
		STATE_PROTOCOL,		///< Protocol setup
		STATE_IDENTIFY,		///< Nick setup
		STATE_VERIFY,		///< Checking password
		STATE_NORMAL,		///< Running
		STATE_DISCONNECTED,	///< Nothing in particular
	} state;

	SearchQueue searchQueue;
	BufferedSocket* sock;

	int64_t availableBytes;

	bool updateCounts(bool aRemove, bool updateIcons);
	void updateActivity() { lastActivity = GET_TICK(); }

	/** Reload details from favmanager or settings */
	void reloadSettings(bool updateNick);

	virtual string checkNick(const string& nick) = 0;
	virtual void search(int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList) = 0;

	// TimerManagerListener
	virtual void on(Second, uint64_t aTick) noexcept;
	// BufferedSocketListener
	virtual void on(Connecting) noexcept { fire(ClientListener::Connecting(), this); }
	virtual void on(Connected) noexcept;
	virtual void on(Line, const string& aLine) noexcept;
	virtual void on(Failed, const string&) noexcept;

	virtual bool v4only() const = 0;
private:

	Client(const Client&);
	Client& operator=(const Client&);

	string hubUrl;
	string address;
	string ip;
	string localIp;
	string keyprint;

	int seticons;

	string port;
	char separator;
	bool secure;
	CountType countType;
};

} // namespace dcpp

#endif // !defined(CLIENT_H)

/**
 * @file
 * $Id: Client.h 551 2010-12-18 12:14:16Z bigmuscle $
 */
