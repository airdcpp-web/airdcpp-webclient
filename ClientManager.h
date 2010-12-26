/*
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_CLIENT_MANAGER_H
#define DCPLUSPLUS_DCPP_CLIENT_MANAGER_H

#include "TimerManager.h"

#include "Client.h"
#include "Singleton.h"
#include "SettingsManager.h"
#include "User.h"
#include "Socket.h"
#include "DirectoryListing.h"
#include "FavoriteManager.h"

#include "ClientManagerListener.h"

namespace dcpp {

class UserCommand;

class ClientManager : public Speaker<ClientManagerListener>, 
	private ClientListener, public Singleton<ClientManager>, 
	private TimerManagerListener
{
public:
	Client* getClient(const string& aHubURL);
	void putClient(Client* aClient);

	StringList getHubs(const CID& cid, const string& hintUrl) const;
	StringList getHubNames(const CID& cid, const string& hintUrl) const;
	StringList getNicks(const CID& cid, const string& hintUrl) const;

	StringList getHubs(const CID& cid, const string& hintUrl, bool priv) const;
	StringList getHubNames(const CID& cid, const string& hintUrl, bool priv) const;
	StringList getNicks(const CID& cid, const string& hintUrl, bool priv) const;

	StringList getNicks(const HintedUser& user) const { return getNicks(user.user->getCID(), user.hint); }
	StringList getHubNames(const HintedUser& user) const { return getHubNames(user.user->getCID(), user.hint); }

	string getConnection(const CID& cid) const;
	uint8_t getSlots(const CID& cid) const;

	bool isConnected(const string& aUrl) const;
	
	void search(int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, void* aOwner = 0);
	uint64_t search(StringList& who, int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList, void* aOwner = 0);
	
	void cancelSearch(void* aOwner);
		
	void infoUpdated();

	UserPtr getUser(const string& aNick, const string& aHubUrl) throw();
	UserPtr getUser(const CID& cid) throw();

	string findHub(const string& ipPort) const;
	const string& findHubEncoding(const string& aUrl) const;

	/**
	* @param priv discard any user that doesn't match the hint.
	* @return OnlineUser* found by CID and hint; might be only by CID if priv is false.
	*/
	OnlineUser* findOnlineUser(const CID& cid, const string& hintUrl, bool priv) const throw();

	UserPtr findUser(const string& aNick, const string& aHubUrl) const throw() { return findUser(makeCid(aNick, aHubUrl)); }
	UserPtr findUser(const CID& cid) const throw();
	UserPtr findLegacyUser(const string& aNick) const throw();
	
	void updateNick(const UserPtr& user, const string& nick) throw();
	string getMyNick(const string& hubUrl) const;
	
	void setIPUser(const UserPtr& user, const string& IP, uint16_t udpPort = 0) {
		if(IP.empty())
			return;
			
		Lock l(cs);
		OnlinePairC p = onlineUsers.equal_range(const_cast<CID*>(&user->getCID()));
		for (OnlineIterC i = p.first; i != p.second; i++) {
			i->second->getIdentity().setIp(IP);
			if(udpPort > 0)
				i->second->getIdentity().setUdpPort(Util::toString(udpPort));
		}
	}
	
	bool isSharingHub(const HintedUser& p) {
		Lock l(cs);
		OnlineUser* u = findOnlineUser_hint(p.user->getCID(), p.hint);
		if(u)
			return (!u->getClient().getHideShare());
		return true;
	}

	bool isOp(const UserPtr& aUser, const string& aHubUrl) const;
	bool isStealth(const string& aHubUrl) const;

	/** Constructs a synthetic, hopefully unique CID */
	CID makeCid(const string& nick, const string& hubUrl) const throw();

	void putOnline(OnlineUser* ou) throw();
	void putOffline(OnlineUser* ou, bool disconnect = false) throw();

	UserPtr& getMe();
	
	void send(AdcCommand& c, const CID& to);
	void connect(const HintedUser& user, const string& token);
	void privateMessage(const HintedUser& user, const string& msg, bool thirdPerson);
	void userCommand(const HintedUser& user, const UserCommand& uc, StringMap& params, bool compatibility);

	int getMode(const string& aHubUrl) const;
	bool isActive(const string& aHubUrl = Util::emptyString) const { return getMode(aHubUrl) != SettingsManager::INCOMING_FIREWALL_PASSIVE; }

	void lock() throw() { cs.lock(); }
	void unlock() throw() { cs.unlock(); }

	const Client::List& getClients() const { return clients; }

	CID getMyCID();
	const CID& getMyPID();

	// NMDC functions only!!!
//	void setPkLock(const UserPtr& p, const string& aPk, const string& aLock);
//	void setSupports(const UserPtr& p, const string& aSupports);
	
//	void setGenerator(const UserPtr& p, const string& aGenerator);
//	void setUnknownCommand(const UserPtr& p, const string& aUnknownCommand);

private:

	typedef unordered_map<CID*, UserPtr> UserMap;
	typedef UserMap::iterator UserIter;

	typedef unordered_map<CID*, std::string> NickMap;

	typedef unordered_multimap<CID*, OnlineUser*> OnlineMap;
	typedef OnlineMap::iterator OnlineIter;
	typedef OnlineMap::const_iterator OnlineIterC;
	typedef pair<OnlineIter, OnlineIter> OnlinePair;
	typedef pair<OnlineIterC, OnlineIterC> OnlinePairC;
	
	Client::List clients;
	mutable CriticalSection cs;
	
	UserMap users;
	OnlineMap onlineUsers;	
	NickMap nicks;

	UserPtr me;
	
	CID pid;	

	friend class Singleton<ClientManager>;

	ClientManager() {
		TimerManager::getInstance()->addListener(this); 
	}

	~ClientManager() throw() {
		TimerManager::getInstance()->removeListener(this); 
	}

	void updateNick(const OnlineUser& user) throw();
		
	/// @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	OnlineUser* findOnlineUser_hint(const CID& cid, const string& hintUrl) const throw() {
		OnlinePairC p;
		return findOnlineUser_hint(cid, hintUrl, p);
	}
	/**
	* @param p OnlinePair of all the users found by CID, even those who don't match the hint.
	* @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	*/
	OnlineUser* findOnlineUser_hint(const CID& cid, const string& hintUrl, OnlinePairC& p) const throw();

	// ClientListener
	void on(Connected, const Client* c) throw();
	void on(UserUpdated, const Client*, const OnlineUserPtr& user) throw();
	void on(UsersUpdated, const Client* c, const OnlineUserList&) throw();
	void on(Failed, const Client*, const string&) throw();
	void on(HubUpdated, const Client* c) throw();
	void on(HubUserCommand, const Client*, int, int, const string&, const string&) throw();
	void on(NmdcSearch, Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize,
		int aFileType, const string& aString, bool) throw();
	void on(AdcSearch, const Client* c, const AdcCommand& adc, const CID& from) throw();
	// TimerManagerListener
	void on(TimerManagerListener::Minute, uint64_t aTick) throw();
};

} // namespace dcpp

#endif // !defined(CLIENT_MANAGER_H)

/**
 * @file
 * $Id: ClientManager.h 551 2010-12-18 12:14:16Z bigmuscle $
 */
