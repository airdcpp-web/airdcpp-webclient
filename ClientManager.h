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

#ifndef DCPLUSPLUS_DCPP_CLIENT_MANAGER_H
#define DCPLUSPLUS_DCPP_CLIENT_MANAGER_H

#include "TimerManager.h"

#include "Client.h"
#include "Singleton.h"
#include "SettingsManager.h"
#include "OnlineUser.h"
#include "Socket.h"
#include "DirectoryListing.h"
#include "FavoriteManager.h"

#include "CID.h"
#include "ClientManagerListener.h"
#include "HintedUser.h"

namespace dcpp {

class UserCommand;

class ClientManager : public Speaker<ClientManagerListener>, 
	private ClientListener, public Singleton<ClientManager>, 
	private TimerManagerListener
{
public:
	Client* getClient(const string& aHubURL);
	void putClient(Client* aClient);

	size_t getUserCount() const;
	int64_t getAvailable() const;

	StringList getHubUrls(const CID& cid, const string& hintUrl = Util::emptyString);
	StringList getHubNames(const CID& cid, const string& hintUrl = Util::emptyString);
	StringList getNicks(const CID& cid, const string& hintUrl = Util::emptyString);
	string getField(const CID& cid, const string& hintUrl, const char* field) const;

	StringList getHubUrls(const CID& cid, const string& hintUrl, bool priv) const;
	StringList getHubNames(const CID& cid, const string& hintUrl, bool priv) const;
	StringList getNicks(const CID& cid, const string& hintUrl, bool priv) const;

	StringList getNicks(const HintedUser& user) { return getNicks(user.user->getCID(), user.hint); }
	StringList getHubNames(const HintedUser& user) { return getHubNames(user.user->getCID(), user.hint); }
	StringList getHubUrls(const HintedUser& user) { return getHubUrls(user.user->getCID(), user.hint); }

	StringPairList getHubs(const CID& cid, const string& hintUrl, bool priv);

	vector<Identity> getIdentities(const UserPtr &u) const;


	string getConnection(const CID& cid) const;
	string getDLSpeed(const CID& cid) const;
	uint8_t getSlots(const CID& cid) const;

	bool isConnected(const string& aUrl) const;
	
	uint64_t search(string& who, int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList, Search::searchType sType, void* aOwner = 0);
	
	void cancelSearch(void* aOwner);
		
	void infoUpdated();

	UserPtr getUser(const string& aNick, const string& aHubUrl) noexcept;
	UserPtr getUser(const CID& cid) noexcept;

	string findHub(const string& ipPort) const;
	const string& findHubEncoding(const string& aUrl) const;

	/**
	* @param priv discard any user that doesn't match the hint.
	* @return OnlineUser* found by CID and hint; might be only by CID if priv is false.
	*/
	OnlineUser* findOnlineUser(const HintedUser& user, bool priv) const;
	OnlineUser* findOnlineUser(const CID& cid, const string& hintUrl, bool priv) const;

	UserPtr findUser(const string& aNick, const string& aHubUrl) const noexcept { return findUser(makeCid(aNick, aHubUrl)); }
	UserPtr findUser(const CID& cid) const noexcept;
	UserPtr findLegacyUser(const string& aNick) const noexcept;

	UserPtr findUserByNick(const string& aNick, const string& aHubUrl) const noexcept;
	
	void updateNick(const UserPtr& user, const string& nick) noexcept;
	string getMyNick(const string& hubUrl) const;
	
	void setIPUser(const UserPtr& user, const string& IP, const string& udpPort = Util::emptyString);
	
	const string& findProfile(const HintedUser& p, const string& userSID);
	void listProfiles(const UserPtr& aUser, StringSet& profiles);

	string findMySID(const HintedUser& p);

	bool isOp(const UserPtr& aUser, const string& aHubUrl) const;
	bool isStealth(const string& aHubUrl) const;

	/** Constructs a synthetic, hopefully unique CID */
	CID makeCid(const string& nick, const string& hubUrl) const noexcept;

	void putOnline(OnlineUser* ou) noexcept;
	void putOffline(OnlineUser* ou, bool disconnect = false, bool priv = false) noexcept;

	UserPtr& getMe();
	
	bool send(AdcCommand& c, const CID& to, bool noCID=false, bool noPassive=false);

	void connect(const HintedUser& user, const string& token);
	void privateMessage(const HintedUser& user, const string& msg, bool thirdPerson);
	void userCommand(const HintedUser& user, const UserCommand& uc, ParamMap& params, bool compatibility);

	int getMode(const string& aHubUrl) const;
	bool isActive(const string& aHubUrl = Util::emptyString) const { return getMode(aHubUrl) != SettingsManager::INCOMING_FIREWALL_PASSIVE; }

	void lockRead() noexcept { cs.lock_shared(); }
	void unlockRead() noexcept { cs.unlock_shared(); }

	const Client::List& getClients() const { return clients; }
	void getOnlineClients(StringList& onlineClients);

	CID getMyCID();
	const CID& getMyPID();

	void resetProfiles(const StringList& aProfiles, ShareProfilePtr aDefaultProfile);
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
	mutable SharedMutex cs;
	
	UserMap users;
	OnlineMap onlineUsers;	
	NickMap nicks;

	UserPtr me;

	Socket udp;
	
	CID pid;	

	friend class Singleton<ClientManager>;

	ClientManager();

	virtual ~ClientManager();

	void updateUser(const OnlineUser& user) noexcept;
		
	/// @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	OnlineUser* findOnlineUserHint(const CID& cid, const string& hintUrl) const {
		OnlinePairC p;
		return findOnlineUserHint(cid, hintUrl, p);
	}
	/**
	* @param p OnlinePair of all the users found by CID, even those who don't match the hint.
	* @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	*/
	OnlineUser* findOnlineUserHint(const CID& cid, const string& hintUrl, OnlinePairC& p) const;

	// ClientListener
	void on(Connected, const Client* c) noexcept;
	void on(UserUpdated, const Client*, const OnlineUserPtr& user) noexcept;
	void on(UsersUpdated, const Client* c, const OnlineUserList&) noexcept;
	void on(Failed, const Client*, const string&) noexcept;
	void on(HubUpdated, const Client* c) noexcept;
	void on(HubUserCommand, const Client*, int, int, const string&, const string&) noexcept;
	void on(NmdcSearch, Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize,
		int aFileType, const string& aString, bool) noexcept;
	void on(AdcSearch, const Client* c, const AdcCommand& adc, const CID& from) noexcept;
	// TimerManagerListener
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
};

} // namespace dcpp

#endif // !defined(CLIENT_MANAGER_H)

/**
 * @file
 * $Id: ClientManager.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
