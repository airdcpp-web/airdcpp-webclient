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

#ifndef DCPLUSPLUS_DCPP_CLIENT_MANAGER_H
#define DCPLUSPLUS_DCPP_CLIENT_MANAGER_H

#include "TimerManager.h"
#include "ClientManagerListener.h"

#include "CID.h"
#include "Client.h"
#include "CriticalSection.h"
#include "HintedUser.h"
#include "OnlineUser.h"
#include "Search.h"
#include "SettingsManager.h"
#include "Singleton.h"
#include "Socket.h"
#include "ShareProfile.h"
#include "OfflineUser.h"

namespace dcpp {

class UserCommand;

class ClientManager : public Speaker<ClientManagerListener>, 
	private ClientListener, public Singleton<ClientManager>, 
	private TimerManagerListener
{
	typedef unordered_map<CID*, UserPtr> UserMap;
	typedef UserMap::iterator UserIter;

public:
	Client* createClient(const RecentHubEntryPtr& aEntry, ProfileToken aProfile) noexcept;
	Client* getClient(const string& aHubURL) noexcept;
	void putClient(Client* aClient) noexcept;
	void setClientUrl(const string& aOldUrl, const string& aNewUrl) noexcept;

	size_t getUserCount() const noexcept;
	int64_t getAvailable() const noexcept;

	string getField(const CID& cid, const string& hintUrl, const char* field) const noexcept;

	OrderedStringSet getHubSet(const CID& cid) const noexcept;
	StringList getHubUrls(const CID& cid) const noexcept;
	StringList getHubNames(const CID& cid) const noexcept;
	StringList getNicks(const CID& cid, bool allowCID = true) const noexcept;
	pair<int64_t, int> getShareInfo(const HintedUser& user) const noexcept;
	void getUserInfoList(const UserPtr& user, User::UserInfoList& aList_) const noexcept;

	StringList getNicks(const HintedUser& user) const noexcept { return getNicks(user.user->getCID()); }
	StringList getHubNames(const HintedUser& user) const noexcept { return getHubNames(user.user->getCID()); }
	StringList getHubUrls(const HintedUser& user) const noexcept { return getHubUrls(user.user->getCID()); }

	StringPair getNickHubPair(const UserPtr& user, string& hint) const noexcept;

	template<class NameOperator>
	string formatUserList(const HintedUser& user, bool removeDuplicates) const noexcept {
		OnlineUserList ouList;

		RLock l(cs);
		auto hinted = getUsers(user, ouList);

		if (removeDuplicates) {
			ouList.erase(unique(ouList.begin(), ouList.end(), [](const OnlineUserPtr& a, const OnlineUserPtr& b) { return compare(NameOperator()(a), NameOperator()(b)) == 0; }), ouList.end());
			if (hinted) {
				//erase users with the hinted nick
				auto p = equal_range(ouList.begin(), ouList.end(), hinted, OnlineUser::NickSort());
				ouList.erase(p.first, p.second);
			}
		}

		string ret = hinted ? NameOperator()(hinted) + " " : Util::emptyString;
		if (!ouList.empty())
			ret += Util::listToStringT<OnlineUserList, NameOperator>(ouList, hinted ? true : false, hinted ? false : true);
		return ret;
	}

	string getFormatedNicks(const HintedUser& user) const noexcept;
	string getFormatedHubNames(const HintedUser& user) const noexcept;

	StringPairList getHubs(const CID& cid) const noexcept;

	map<string, Identity> getIdentities(const UserPtr &u) const noexcept;
	
	string getNick(const UserPtr& u, const string& hintUrl, bool allowFallback = true) const noexcept;
	StringPairList getNickHubPair(const CID& cid, string& hint) const noexcept;

	string getDLSpeed(const CID& cid) const noexcept;
	uint8_t getSlots(const CID& cid) const noexcept;

	bool isConnected(const string& aUrl) const noexcept;
	
	uint64_t search(string& who, SearchPtr aSearch) noexcept;

	void directSearch(const HintedUser& user, int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, 
		const StringList& aExtList, const string& aDir, time_t aDate, int aDateMode) noexcept;
	
	void cancelSearch(void* aOwner) noexcept;
		
	void infoUpdated() noexcept;

	UserPtr getUser(const string& aNick, const string& aHubUrl) noexcept;
	UserPtr getUser(const CID& cid) noexcept;

	// usage needs to be locked!
	const UserMap& getUsers() const { return users; }

	string findHub(const string& ipPort, bool nmdc) const noexcept;
	const string& findHubEncoding(const string& aUrl) const noexcept;

	/**
	* @param priv discard any user that doesn't match the hint.
	* @return OnlineUser* found by CID and hint; might be only by CID if priv is false.
	*/
	OnlineUser* findOnlineUser(const HintedUser& user) const noexcept;
	OnlineUser* findOnlineUser(const CID& cid, const string& hintUrl) const noexcept;

	UserPtr findUser(const string& aNick, const string& aHubUrl) const noexcept { return findUser(makeCid(aNick, aHubUrl)); }
	UserPtr findUser(const CID& cid) const noexcept;
	HintedUser findLegacyUser(const string& nick) const noexcept;

	UserPtr findUserByNick(const string& aNick, const string& aHubUrl) const noexcept;
	
	//Note; Lock usage
	void addOfflineUser(const UserPtr& user, const string& nick, const string& url, uint32_t lastSeen = GET_TIME()) noexcept;

	string getMyNick(const string& hubUrl) const noexcept;
	optional<OfflineUser> getOfflineUser(const CID& cid);
	
	void setIPUser(const UserPtr& user, const string& IP, const string& udpPort = Util::emptyString) noexcept;
	
	optional<ProfileToken> findProfile(UserConnection& p, const string& userSID) const noexcept;
	void listProfiles(const UserPtr& aUser, ProfileTokenSet& profiles) const noexcept;

	string findMySID(const UserPtr& aUser, string& aHubUrl, bool allowFallback) const noexcept;

	bool isOp(const UserPtr& aUser, const string& aHubUrl) const noexcept;
	bool isStealth(const string& aHubUrl) const noexcept;

	/** Constructs a synthetic, hopefully unique CID */
	CID makeCid(const string& nick, const string& hubUrl) const noexcept;

	void putOnline(OnlineUser* ou) noexcept;
	void putOffline(OnlineUser* ou, bool disconnect = false) noexcept;

	UserPtr& getMe() noexcept;
	string getClientStats() const noexcept;
	
	bool sendUDP(AdcCommand& c, const CID& to, bool noCID = false, bool noPassive = false, const string& encryptionKey = Util::emptyString, const string& aHubUrl = Util::emptyString) noexcept;

	bool connect(const UserPtr& aUser, const string& aToken, bool allowUrlChange, string& lastError_, string& hubHint_, bool& isProtocolError) noexcept;
	bool privateMessage(const HintedUser& user, const string& msg, string& error_, bool thirdPerson) noexcept;
	void userCommand(const HintedUser& user, const UserCommand& uc, ParamMap& params, bool compatibility) noexcept;

	bool isActive() const noexcept;
	bool isActive(const string& aHubUrl) const noexcept;

	SharedMutex& getCS() { return cs; }

	const Client::List& getClients() const noexcept { return clients; }
	void getOnlineClients(StringList& onlineClients) const noexcept;

	CID getMyCID() noexcept;
	const CID& getMyPID() noexcept;

	void resetProfile(ProfileToken oldProfile, ProfileToken newProfile, bool nmdcOnly) noexcept;
	void resetProfiles(const ShareProfileInfo::List& aProfiles, ProfileToken aDefaultProfile) noexcept;

	bool connectADCSearchResult(const CID& aCID, string& token_, string& hubUrl_, string& connection_, uint8_t& slots_) const noexcept;
	bool connectNMDCSearchResult(const string& userIP, const string& hubIpPort, HintedUser& user, string& nick, string& connection_, string& file, string& hubName) noexcept;
	bool hasAdcHubs() const noexcept;

	//return users supporting the ASCH extension (and total users)
	pair<size_t, size_t> countAschSupport(const OrderedStringSet& aHubs) const noexcept;
private:

	typedef unordered_map<CID*, OfflineUser> OfflineUserMap;

	typedef unordered_multimap<CID*, OnlineUser*> OnlineMap;
	typedef OnlineMap::iterator OnlineIter;
	typedef OnlineMap::const_iterator OnlineIterC;
	typedef pair<OnlineIter, OnlineIter> OnlinePair;
	typedef pair<OnlineIterC, OnlineIterC> OnlinePairC;
	
	Client::List clients;
	mutable SharedMutex cs;
	
	UserMap users;
	OnlineMap onlineUsers;

	OfflineUserMap offlineUsers;

	UserPtr me;

	Socket udp;
	
	CID pid;
	uint64_t lastOfflineUserCleanup;

	friend class Singleton<ClientManager>;

	ClientManager();

	virtual ~ClientManager();

	//Note; Lock usage
	void updateUser(const OnlineUser& user, bool wentOffline) noexcept;

	OnlineUserPtr getUsers(const HintedUser& aUser, OnlineUserList& users) const noexcept;
		
	/// @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	OnlineUser* findOnlineUserHint(const CID& cid, const string& hintUrl) const noexcept {
		OnlinePairC p;
		return findOnlineUserHint(cid, hintUrl, p);
	}
	/**
	* @param p OnlinePair of all the users found by CID, even those who don't match the hint.
	* @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	*/
	OnlineUser* findOnlineUserHint(const CID& cid, const string& hintUrl, OnlinePairC& p) const noexcept;

	void onSearch(const Client* c, const AdcCommand& adc, OnlineUser& from) noexcept;

	// ClientListener
	void on(Connected, const Client* c) noexcept;
	void on(UserUpdated, const Client*, const OnlineUserPtr& user) noexcept;
	void on(UsersUpdated, const Client* c, const OnlineUserList&) noexcept;
	void on(Failed, const string&, const string&) noexcept;
	void on(HubUpdated, const Client* c) noexcept;
	void on(HubUserCommand, const Client*, int, int, const string&, const string&) noexcept;
	void on(NmdcSearch, Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize,
		int aFileType, const string& aString, bool) noexcept;
	// TimerManagerListener
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
};

} // namespace dcpp

#endif // !defined(CLIENT_MANAGER_H)