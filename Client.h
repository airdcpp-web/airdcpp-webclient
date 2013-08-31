/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#include "atomic.h"
#include "compiler.h"
#include "forward.h"

#include "BufferedSocket.h"
#include "BufferedSocketListener.h"
#include "ClientListener.h"
#include "HubSettings.h"
#include "OnlineUser.h"
#include "Search.h"
#include "SearchQueue.h"
#include "Speaker.h"
#include "TimerManager.h"
#include "User.h"

#include <boost/noncopyable.hpp>

namespace dcpp {

class ClientBase
{
public:
	
	ClientBase() { }
	
	virtual const string& getHubUrl() const = 0;
	virtual string getHubName() const = 0;
	virtual bool isOp() const = 0;
	virtual int connect(const OnlineUser& user, const string& token, string& lastError_) = 0;
	virtual bool privateMessage(const OnlineUserPtr& user, const string& aMessage, string& error_, bool thirdPerson = false) = 0;
	virtual void directSearch(const OnlineUser& /*user*/, int /*aSizeMode*/, int64_t /*aSize*/, int /*aFileType*/, const string& /*aString*/, const string& /*aToken*/, const StringList& /*aExtList*/, const string& /*aDir*/, time_t /*aDate*/, int /*aDateMode*/) { 
		dcassert(0); 
	}
};

/** Yes, this should probably be called a Hub */
class Client : public ClientBase, public Speaker<ClientListener>, public BufferedSocketListener, protected TimerManagerListener, public HubSettings, private boost::noncopyable {
public:
	typedef unordered_map<string*, Client*, noCaseStringHash, noCaseStringEq> List;
	typedef List::const_iterator Iter;

	virtual void connect();
	virtual void disconnect(bool graceless);

	virtual int connect(const OnlineUser& user, const string& token, string& lastError_) = 0;
	virtual bool hubMessage(const string& aMessage, string& error_, bool thirdPerson = false) = 0;
	virtual bool privateMessage(const OnlineUserPtr& user, const string& aMessage, string& error_, bool thirdPerson = false) = 0;
	virtual void sendUserCmd(const UserCommand& command, const ParamMap& params) = 0;

	uint64_t queueSearch(SearchPtr aSearch);
	void cancelSearch(void* aOwner) { searchQueue.cancelSearch(aOwner); }
	
	virtual void password(const string& pwd) = 0;
	void info();

	virtual size_t getUserCount() const = 0;
	int64_t getAvailable() const { return availableBytes; };
	
	virtual bool send(const AdcCommand& command) = 0;

	template<typename F>
	void callAsync(F f) { if(sock) sock->callAsync(f); }

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

	void updated(const OnlineUserPtr& aUser);
	void updated(OnlineUserList& users);

	static int getTotalCounts() {
		return counts[COUNT_NORMAL] + counts[COUNT_REGISTERED] + counts[COUNT_OP];
	}

	static string getCounts();
	
	void setActive();
	void reconnect();
	virtual void shutdown();
	bool isActive() const;
	bool isActiveV4() const;
	bool isActiveV6() const;

	void send(const string& aMessage) { send(aMessage.c_str(), aMessage.length()); }
	void send(const char* aMessage, size_t aLen);

	string getMyNick() const { return getMyIdentity().getNick(); }
	string getHubName() const { return getHubIdentity().getNick().empty() ? getHubUrl() : getHubIdentity().getNick(); }
	string getHubDescription() const { return getHubIdentity().getDescription(); }
	
	void Message(const string& msg) {
		fire(ClientListener::AddLine(), this, msg);
	}

	Identity& getHubIdentity() { return hubIdentity; }

	const string& getHubUrl() const { return hubUrl; }

	GETSET(Identity, myIdentity, MyIdentity);
	GETSET(Identity, hubIdentity, HubIdentity);

	GETSET(string, defpassword, Password);
	GETSET(bool, favnoPM, FavNoPM);

	GETSET(uint64_t, lastActivity, LastActivity);
	GETSET(uint32_t, reconnDelay, ReconnDelay);
	
	GETSET(string, encoding, Encoding);	
	
	GETSET(bool, registered, Registered);
	GETSET(bool, autoReconnect, AutoReconnect);
	GETSET(bool, stealth, Stealth);
	GETSET(ProfileToken, shareProfile, ShareProfile);
	GETSET(ProfileToken, favToken, FavToken);
	GETSET(uint32_t, uniqueId, UniqueId);

	/* Set a hub setting and return the new value */
	bool changeBoolHubSetting(HubSettings::HubBoolSetting aSetting);

	enum CountType {
		COUNT_NORMAL = 0x00,
		COUNT_REGISTERED = 0x01,
		COUNT_OP = 0x04,
		COUNT_UNCOUNTED = 0x08
	};
protected:
	friend class ClientManager;
	Client(const string& hubURL, char separator);
	virtual ~Client();

	static atomic<long> counts[COUNT_UNCOUNTED];

	enum States {
		STATE_CONNECTING,	///< Waiting for socket to connect
		STATE_PROTOCOL,		///< Protocol setup
		STATE_IDENTIFY,		///< Nick setup
		STATE_VERIFY,		///< Checking password
		STATE_NORMAL,		///< Running
		STATE_DISCONNECTED	///< Nothing in particular
	} state;

	SearchQueue searchQueue;
	BufferedSocket* sock;

	int64_t availableBytes;

	bool updateCounts(bool aRemove, bool updateIcons);
	void updateActivity() { lastActivity = GET_TICK(); }

	/** Reload details from favmanager or settings */
	void reloadSettings(bool updateNick);
	/// Get the external IP the user has defined for this hub, if any.
	const string& getUserIp4() const;
	const string& getUserIp6() const;

	string getDescription() const;

	virtual string checkNick(const string& nick) = 0;
	virtual void search(const SearchPtr& aSearch) = 0;
	virtual void infoImpl() = 0;

	// TimerManagerListener
	virtual void on(Second, uint64_t aTick) noexcept;
	// BufferedSocketListener
	virtual void on(Connecting) noexcept { fire(ClientListener::Connecting(), this); }
	virtual void on(Connected) noexcept;
	virtual void on(Line, const string& aLine) noexcept;
	virtual void on(Failed, const string&) noexcept;

	virtual bool v4only() const = 0;
	void setHubUrl(const string& url);
	void onPassword();

	bool closing;
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
