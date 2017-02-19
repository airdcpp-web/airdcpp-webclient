/*
 * Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
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

#include "forward.h"

#include "ClientManagerListener.h"
#include "TimerManagerListener.h"

#include "ActionHook.h"
#include "ConnectionType.h"
#include "Client.h"
#include "CriticalSection.h"
#include "OfflineUser.h"
#include "Singleton.h"
#include "Socket.h"
#include "TimerManager.h"


namespace dcpp {

class UserCommand;

class ClientManager : public Speaker<ClientManagerListener>, 
	private ClientListener, public Singleton<ClientManager>, 
	private TimerManagerListener, private ShareManagerListener
{
	typedef unordered_map<CID*, UserPtr> UserMap;
	typedef UserMap::iterator UserIter;

public:
	ActionHook<const ChatMessagePtr> incomingHubMessageHook, incomingPrivateMessageHook;
	ActionHook<const string, const bool, const HintedUser, const bool> outgoingPrivateMessageHook;
	ActionHook<const string, const bool, const Client&> outgoingHubMessageHook;

	// Returns the new ClientPtr
	// NOTE: the main app should perform connecting to the new hub
	ClientPtr createClient(const string& aUrl) noexcept;
	ClientPtr getClient(const string& aHubURL) noexcept;
	ClientPtr getClient(ClientToken aClientId) noexcept;

	bool putClient(ClientToken aClientId) noexcept;
	bool putClient(const string& aHubURL) noexcept;
	bool putClient(ClientPtr& aClient) noexcept;
	void putClients() noexcept;

	// Returns the new ClientPtr
	// NOTE: the main app should perform connecting to the new hub
	ClientPtr redirect(const string& aHubUrl, const string& aNewUrl) noexcept;

	string getField(const CID& cid, const string& hintUrl, const char* field) const noexcept;

	OrderedStringSet getHubSet(const CID& cid) const noexcept;
	StringList getHubUrls(const CID& cid) const noexcept;
	StringList getHubNames(const CID& cid) const noexcept;
	string getHubName(const string& aHubUrl) const noexcept;
	StringList getNicks(const CID& cid, bool allowCID = true) const noexcept;

	struct ShareInfo {
		const int64_t size;
		const int fileCount;
	};
	optional<ShareInfo> getShareInfo(const HintedUser& user) const noexcept;

	void getUserInfoList(const UserPtr& user, User::UserInfoList& aList_) const noexcept;

	StringList getNicks(const HintedUser& user) const noexcept;
	StringList getHubNames(const HintedUser& user) const noexcept;
	StringList getHubUrls(const HintedUser& user) const noexcept;

	template<class NameOperator>
	string formatUserProperty(const HintedUser& user, bool removeDuplicates = true) const noexcept {
		OnlineUserList ouList;
		auto hinted = getOnlineUsers(user, ouList);

		return formatUserProperty<NameOperator>(hinted, ouList, removeDuplicates);
	}

	template<class NameOperator>
	string formatUserProperty(const OnlineUserPtr& aHintedUser, const OnlineUserList& aOtherUsers, bool aRemoveDuplicates = true) const noexcept {
		auto ouList = aOtherUsers;

		if (aRemoveDuplicates) {
			ouList.erase(unique(ouList.begin(), ouList.end(), [](const OnlineUserPtr& a, const OnlineUserPtr& b) { return compare(NameOperator()(a), NameOperator()(b)) == 0; }), ouList.end());
			if (aHintedUser) {
				//erase users with the hinted nick
				auto p = equal_range(ouList.begin(), ouList.end(), aHintedUser, OnlineUser::NickSort());
				ouList.erase(p.first, p.second);
			}
		}

		string ret = aHintedUser ? NameOperator()(aHintedUser) + " " : Util::emptyString;
		if (!ouList.empty())
			ret += Util::listToStringT<OnlineUserList, NameOperator>(ouList, aHintedUser ? true : false, aHintedUser ? false : true);
		return ret;
	}

	// Gets the user matching the hinted hubs + other instances of the user from other hubs
	// Returns null if the hinted user was not found
	OnlineUserPtr getOnlineUsers(const HintedUser& aUser, OnlineUserList& users) const noexcept;

	string getFormatedNicks(const HintedUser& user) const noexcept;
	string getFormatedHubNames(const HintedUser& user) const noexcept;

	StringPairList getHubs(const CID& cid) const noexcept;

	map<string, Identity> getIdentities(const UserPtr &u) const noexcept;
	
	string getNick(const UserPtr& u, const string& hintUrl, bool allowFallback = true) const noexcept;

	string getDLSpeed(const CID& cid) const noexcept;
	uint8_t getSlots(const CID& cid) const noexcept;

	bool hasClient(const string& aUrl) const noexcept;

	// Get users with nick matching the pattern. Uses relevancies for priorizing the results.
	OnlineUserList searchNicks(const string& aPattern, size_t aMaxResults, bool aIgnorePrefix, const StringList& aHubUrls) const noexcept;

	bool directSearch(const HintedUser& user, const SearchPtr& aSearch, string& error_) noexcept;
	
	optional<uint64_t> search(string& aHubUrl, const SearchPtr& aSearch, string& error_) noexcept;
	bool cancelSearch(const void* aOwner) noexcept;
	optional<uint64_t> getMaxSearchQueueTime(const void* aOwner) const noexcept;
	bool hasSearchQueueOverflow() const noexcept;
		
	void infoUpdated() noexcept;

	// Fire UserUpdated via each connected hub
	void userUpdated(const UserPtr& aUser) const noexcept;

	UserPtr getUser(const string& aNick, const string& aHubUrl) noexcept;
	UserPtr getUser(const CID& cid) noexcept;
	UserPtr loadUser(const string& aCid, const string& aUrl, const string& aNick, uint32_t lastSeen = GET_TIME()) noexcept;

	// usage needs to be locked!
	const UserMap& getUsers() const { return users; }

	string findHub(const string& ipPort, bool nmdc) const noexcept;
	const string& findHubEncoding(const string& aUrl) const noexcept;

	// Return OnlineUser found by CID and hint
	// aAllowFallback: return OnlineUserPtr from any hub if the hinted one is not found
	OnlineUserPtr findOnlineUser(const HintedUser& user, bool aAllowFallback = true) const noexcept;
	OnlineUserPtr findOnlineUser(const CID& cid, const string& hintUrl, bool aAllowFallback = true) const noexcept;

	UserPtr findUser(const string& aNick, const string& aHubUrl) const noexcept { return findUser(makeCid(aNick, aHubUrl)); }
	UserPtr findUser(const CID& cid) const noexcept;
	HintedUser findLegacyUser(const string& nick) const noexcept;

	UserPtr findUserByNick(const string& aNick, const string& aHubUrl) const noexcept;
	
	void addOfflineUser(const UserPtr& user, const string& nick, const string& url, uint32_t lastSeen = GET_TIME()) noexcept;

	string getMyNick(const string& hubUrl) const noexcept;
	optional<OfflineUser> getOfflineUser(const CID& cid);
	
	void setIPUser(const UserPtr& user, const string& IP, const string& udpPort = Util::emptyString) noexcept;
	
	optional<ProfileToken> findProfile(UserConnection& p, const string& userSID) const noexcept;
	void listProfiles(const UserPtr& aUser, ProfileTokenSet& profiles) const noexcept;

	string findMySID(const UserPtr& aUser, string& aHubUrl, bool allowFallback) const noexcept;

	bool isOp(const UserPtr& aUser, const string& aHubUrl) const noexcept;

	/** Constructs a synthetic, hopefully unique CID */
	CID makeCid(const string& nick, const string& hubUrl) const noexcept;

	void putOnline(const OnlineUserPtr& ou) noexcept;
	void putOffline(const OnlineUserPtr&, bool aDisconnectTransfers = false) noexcept;

	UserPtr& getMe() noexcept;

	struct ClientStats {
		int64_t totalShare = 0, sharePerUser = 0;
		int64_t uploadSpeed = 0, downloadSpeed = 0, nmdcConnection = 0;
		int64_t nmdcSpeedPerUser = 0, downPerAdcUser = 0, upPerAdcUser = 0;

		int nmdcUsers = 0, adcUsers = 0, adcHasDownload = 0, adcHasUpload = 0;

		int hiddenUsers = 0, bots = 0, activeUsers = 0, operators = 0;

		int totalUsers = 0, uniqueUsers = 0;

		double uniqueUsersPercentage = 0;
		double activeUserPercentage = 0, operatorPercentage = 0, botPercentage = 0, hiddenPercentage = 0;

		vector<pair<string, int> > clients;

		void finalize() noexcept;
		void forEachClient(function<void(const string&, int, double)> aHandler) const noexcept;
	};

	// No stats are returned if there are no hubs open (or users in them)
	optional<ClientStats> getClientStats() const noexcept;
	string printClientStats() const noexcept;
	
	bool sendUDP(AdcCommand& c, const CID& to, bool noCID = false, bool noPassive = false, const string& encryptionKey = Util::emptyString, const string& aHubUrl = Util::emptyString) noexcept;

	bool connect(const UserPtr& aUser, const string& aToken, bool allowUrlChange, string& lastError_, string& hubHint_, bool& isProtocolError, ConnectionType type = CONNECTION_TYPE_LAST) const noexcept;
	bool privateMessage(const HintedUser& aUser, const string& aMsg, string& error_, bool aThirdPerson, bool aEcho = true) noexcept;
	void userCommand(const HintedUser& aUser, const UserCommand& uc, ParamMap& params, bool compatibility) noexcept;

	bool isActive() const noexcept;
	bool isActive(const string& aHubUrl) const noexcept;

	SharedMutex& getCS() { return cs; }

	const Client::UrlMap& getClients() const noexcept { return clients; }
	void getOnlineClients(StringList& onlineClients) const noexcept;

	CID getMyCID() noexcept;
	const CID& getMyPID() noexcept;

	bool connectADCSearchResult(const CID& aCID, string& token_, string& hubUrl_, string& connection_, uint8_t& slots_) const noexcept;
	bool connectNMDCSearchResult(const string& userIP, const string& hubIpPort, HintedUser& user, string& nick, string& connection_, string& file, string& hubName) noexcept;

	//return users supporting the ASCH extension (and total users)
	pair<size_t, size_t> countAschSupport(const OrderedStringSet& aHubs) const noexcept;
private:
	static ClientPtr makeClient(const string& aHubURL, const ClientPtr& aOldClient = nullptr) noexcept;

	typedef unordered_map<CID*, OfflineUser> OfflineUserMap;

	typedef unordered_multimap<CID*, OnlineUser*> OnlineMap;
	typedef OnlineMap::iterator OnlineIter;
	typedef OnlineMap::const_iterator OnlineIterC;
	typedef pair<OnlineIter, OnlineIter> OnlinePair;
	typedef pair<OnlineIterC, OnlineIterC> OnlinePairC;
	
	Client::UrlMap clients;
	Client::IdMap clientsById;
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

	// ClientListener
	void on(ClientListener::Connected, const Client* c) noexcept;
	void on(ClientListener::UserUpdated, const Client*, const OnlineUserPtr& user) noexcept;
	void on(ClientListener::UsersUpdated, const Client* c, const OnlineUserList&) noexcept;
	void on(ClientListener::Disconnected, const string&, const string&) noexcept;
	void on(ClientListener::HubUpdated, const Client* c) noexcept;
	void on(ClientListener::HubUserCommand, const Client*, int, int, const string&, const string&) noexcept;
	void on(ClientListener::NmdcSearch, Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize,
		int aFileType, const string& aString, bool) noexcept;
	void on(ClientListener::OutgoingSearch, const Client*, const SearchPtr&) noexcept;
	void on(ClientListener::PrivateMessage, const Client*, const ChatMessagePtr&) noexcept;

	// TimerManagerListener
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
};

} // namespace dcpp

#endif // !defined(CLIENT_MANAGER_H)