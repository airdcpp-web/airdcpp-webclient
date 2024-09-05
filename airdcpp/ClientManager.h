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

#ifndef DCPLUSPLUS_DCPP_CLIENT_MANAGER_H
#define DCPLUSPLUS_DCPP_CLIENT_MANAGER_H

#include "forward.h"

#include "ClientManagerListener.h"
#include "TimerManagerListener.h"

#include "ActionHook.h"
#include "AdcCommand.h"
#include "AdcSupports.h"
#include "ConnectionType.h"
#include "Client.h"
#include "CriticalSection.h"
#include "OfflineUser.h"
#include "Singleton.h"
#include "TimerManager.h"


namespace dcpp {

class UserCommand;

class ClientManager : public Speaker<ClientManagerListener>, 
	private ClientListener, public Singleton<ClientManager>, 
	private TimerManagerListener
{
	typedef unordered_map<CID*, UserPtr> UserMap;
	typedef UserMap::iterator UserIter;

public:
	ActionHook<MessageHighlightList, const ChatMessagePtr> incomingHubMessageHook, incomingPrivateMessageHook;
	ActionHook<nullptr_t, const OutgoingChatMessage&, const HintedUser, const bool /*echo*/> outgoingPrivateMessageHook;
	ActionHook<nullptr_t, const OutgoingChatMessage&, const Client&> outgoingHubMessageHook;

	ActionHook<AdcCommand::ParamMap, const AdcCommand&, const Client&> outgoingHubCommandHook;
	ActionHook<AdcCommand::ParamMap, const AdcCommand&, const OnlineUserPtr&> outgoingUdpCommandHook;

	static bool processChatMessage(const ChatMessagePtr& aMessage, const Identity& aMyIdentity, const ActionHook<MessageHighlightList, const ChatMessagePtr>& aHook);

	// Returns the new ClientPtr
	// NOTE: the main app should perform connecting to the new hub
	ClientPtr createClient(const string& aHubUrl) noexcept;
	ClientPtr getClient(const string& aHubURL) noexcept;
	ClientPtr getClient(ClientToken aClientId) noexcept;

	bool putClient(ClientToken aClientId) noexcept;
	bool putClient(const string& aHubURL) noexcept;
	bool putClient(ClientPtr& aClient) noexcept;
	void putClients() noexcept;

	// Returns the new ClientPtr
	// NOTE: the main app should perform connecting to the new hub
	ClientPtr redirect(const string& aHubUrl, const string& aNewUrl) noexcept;

	string getField(const CID& aCID, const string& aHintUrl, const char* aField) const noexcept;

	OrderedStringSet getHubSet(const CID& aCID) const noexcept;
	StringList getHubUrls(const CID& aCID) const noexcept;
	StringList getHubNames(const CID& aCID) const noexcept;
	string getHubName(const string& aHubUrl) const noexcept;
	StringList getNicks(const CID& aCID, bool aAllowCID = true) const noexcept;

	struct ShareInfo {
		const int64_t size;
		const int fileCount;
	};
	optional<ShareInfo> getShareInfo(const HintedUser& aUser) const noexcept;

	User::UserInfoList getUserInfoList(const UserPtr& aUser) const noexcept;

	// Updates the hinted URL in case the user is not online in the original one
	// Selects the hub where the user is sharing most files
	// URL won't be changed for offline users
	HintedUser checkDownloadUrl(const HintedUser& aUser) const noexcept;

	// Updates the hinted URL in case the user is not online in the original one
	// URL won't be changed for offline users
	HintedUser checkOnlineUrl(const HintedUser& aUser) const noexcept;

	StringList getNicks(const HintedUser& aUser) const noexcept;
	StringList getHubNames(const HintedUser& aUser) const noexcept;
	StringList getHubUrls(const HintedUser& aUser) const noexcept;

	template<class NameOperator>
	string formatUserProperty(const HintedUser& aUser, bool aRemoveDuplicates = true) const noexcept {
		OnlineUserList ouList;
		auto hinted = getOnlineUsers(aUser, ouList);

		return formatUserProperty<NameOperator>(hinted, ouList, aRemoveDuplicates);
	}

	template<class NameOperator>
	static string formatUserProperty(const OnlineUserPtr& aHintedUser, const OnlineUserList& aOtherUsers, bool aRemoveDuplicates = true) noexcept {
		auto ouList = aOtherUsers;

		if (aRemoveDuplicates) {
			ouList.erase(unique(ouList.begin(), ouList.end(), [](const OnlineUserPtr& a, const OnlineUserPtr& b) { return compare(NameOperator()(a), NameOperator()(b)) == 0; }), ouList.end());
			if (aHintedUser) {
				//erase users with the hinted nick
				auto p = equal_range(ouList.begin(), ouList.end(), aHintedUser, OnlineUser::NickSort());
				ouList.erase(p.first, p.second);
			}
		}

		string ret = aHintedUser ? NameOperator()(aHintedUser) : Util::emptyString;
		if (!ouList.empty()) {
			if (!ret.empty()) {
				ret += " ";
			}

			ret += Util::listToStringT<OnlineUserList, NameOperator>(ouList, aHintedUser ? true : false, aHintedUser ? false : true);
		}
		return ret;
	}

	// Gets the user matching the hinted hubs + other instances of the user from other hubs
	// Returns null if the hinted user was not found
	OnlineUserPtr getOnlineUsers(const HintedUser& aUser, OnlineUserList& users_) const noexcept;

	string getFormatedNicks(const HintedUser& aUser) const noexcept;
	string getFormatedHubNames(const HintedUser& aUser) const noexcept;

	StringPairList getHubs(const CID& aCID) const noexcept;

	map<string, Identity> getIdentities(const UserPtr& aUser) const noexcept;
	
	string getNick(const UserPtr& aUser, const string& aHubUrl, bool aAllowFallback = true) const noexcept;

	string getDLSpeed(const CID& aCID) const noexcept;
	uint8_t getSlots(const CID& aCID) const noexcept;

	bool hasClient(const string& aHubUrl) const noexcept;

	// Get users with nick matching the pattern. Uses relevancies for priorizing the results.
	OnlineUserList searchNicks(const string& aPattern, size_t aMaxResults, bool aIgnorePrefix, const StringList& aHubUrls) const noexcept;

	bool directSearchHooked(const HintedUser& user, const SearchPtr& aSearch, string& error_) noexcept;
	
	optional<uint64_t> search(string& aHubUrl, const SearchPtr& aSearch, string& error_) noexcept;
	bool cancelSearch(const void* aOwner) noexcept;
	optional<uint64_t> getMaxSearchQueueTime(const void* aOwner) const noexcept;
	bool hasSearchQueueOverflow() const noexcept;
	int getMaxSearchQueueSize() const noexcept;
		
	void infoUpdated() noexcept;

	// Fire UserUpdated via each connected hub
	void userUpdated(const UserPtr& aUser) const noexcept;

	UserPtr getUser(const string& aNick, const string& aHubUrl) noexcept;
	UserPtr getUser(const CID& cid) noexcept;
	UserPtr loadUser(const string& aCID, const string& aUrl, const string& aNick, uint32_t aLastSeen = GET_TIME()) noexcept;

	// usage needs to be locked!
	const UserMap& getUsersUnsafe() const { return users; }

	string findHub(const string& aIpPort, bool aNmdc) const noexcept;
	const string& findHubEncoding(const string& aUrl) const noexcept;

	// Return OnlineUser found by CID and hint
	// aAllowFallback: return OnlineUserPtr from any hub if the hinted one is not found
	OnlineUserPtr findOnlineUser(const HintedUser& aUser, bool aAllowFallback = true) const noexcept;
	OnlineUserPtr findOnlineUser(const CID& aCID, const string& aHubUrl, bool aAllowFallback = true) const noexcept;

	UserPtr findUser(const string& aNick, const string& aHubUrl) const noexcept { return findUser(makeCid(aNick, aHubUrl)); }
	UserPtr findUser(const CID& aCID) const noexcept;
	HintedUser findLegacyUser(const string& aNick) const noexcept;
	
	void addOfflineUser(const UserPtr& aUser, const string& aNick, const string& aHubUrl, uint32_t aLastSeen = 0) noexcept;

	string getMyNick(const string& aHubUrl) const noexcept;
	optional<OfflineUser> getOfflineUser(const CID& cid);
	
	void setIPUser(const UserPtr& aUser, const string& aIP, const string& aUdpPort = Util::emptyString) noexcept;
	
	optional<ProfileToken> findProfile(UserConnection& uc, const string& aUserSID) const noexcept;
	void listProfiles(const UserPtr& aUser, ProfileTokenSet& profiles_) const noexcept;

	string findMySID(const UserPtr& aUser, string& aHubUrl, bool aAllowFallback) const noexcept;

	bool isOp(const UserPtr& aUser, const string& aHubUrl) const noexcept;

	/** Constructs a synthetic, hopefully unique CID */
	CID makeCid(const string& aNick, const string& aHubUrl) const noexcept;

	void putOnline(const OnlineUserPtr& ou) noexcept;
	void putOffline(const OnlineUserPtr&, bool aDisconnectTransfers = false) noexcept;

	UserPtr& getMe() noexcept;

	struct ClientStats {
		int64_t totalShare = 0;
		int64_t uploadSpeed = 0, downloadSpeed = 0, nmdcConnection = 0;
		int64_t nmdcSpeedPerUser = 0, downPerAdcUser = 0, upPerAdcUser = 0;

		int nmdcUsers = 0, adcUsers = 0, adcHasDownload = 0, adcHasUpload = 0;

		int hiddenUsers = 0, bots = 0, activeUsers = 0, operators = 0;

		int totalUsers = 0, uniqueUsers = 0;

		vector<pair<string, int> > clients;

		void finalize() noexcept;
	};

	// No stats are returned if there are no hubs open (or users in them)
	optional<ClientStats> getClientStats() const noexcept;
	string printClientStats() const noexcept;
	
	bool sendUDPHooked(AdcCommand& c, const CID& to, bool aNoCID = false, bool aNoPassive = false, const string& aEncryptionKey = Util::emptyString, const string& aHubUrl = Util::emptyString) noexcept;
	bool sendUDP(const string& aData, const string& aIP, const string& aPort) noexcept;

	struct ConnectResult {
	public:
		void onSuccess(const string& aHubHint) noexcept {
			success = true;
			hubHint = aHubHint;
		}

		void onMinorError(const string& aError) noexcept {
			lastError = aError;
			protocolError = false;
		}

		void onProtocolError(const string& aError) noexcept {
			lastError = aError;
			protocolError = true;
		}

		void resetError() noexcept {
			lastError = Util::emptyString;
			protocolError = false;
		}


		GETPROP(string, lastError, Error);
		IGETPROP(bool, protocolError, IsProtocolError, false);

		GETPROP(string, hubHint, HubHint);
		IGETPROP(bool, success, IsSuccess, false);
	};

	ConnectResult connect(const HintedUser& aUser, const string& aToken, bool aAllowUrlChange, ConnectionType type = CONNECTION_TYPE_LAST) const noexcept;
	bool privateMessageHooked(const HintedUser& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho = true) noexcept;
	void userCommand(const HintedUser& aUser, const UserCommand& uc, ParamMap& params_, bool aCompatibility) noexcept;

	bool isActive() const noexcept;
	bool isActive(const string& aHubUrl) const noexcept;

	SharedMutex& getCS() { return cs; }

	const Client::UrlMap& getClientsUnsafe() const noexcept { return clients; }
	void getOnlineClients(StringList& onlineClients_) const noexcept;

	CID getMyCID() noexcept;
	const CID& getMyPID() noexcept;

	bool connectADCSearchResult(const CID& aCID, string& token_, string& hubUrl_, string& connection_, uint8_t& slots_) const noexcept;
	bool connectNMDCSearchResult(const string& aUserIP, const string& aHubIpPort, const string& aNick, HintedUser& user_, string& connection_, string& hubEncoding_) noexcept;

	// Get ADC hub URL for UDP commands
	// Returns empty string in case of errors
	string getADCSearchHubUrl(const CID& aCID, const string& aHubIpPort) const noexcept;

	// Get NMDC user + hub URL for UDP commands encoded in legacy encoding
	// Returns null user in case of errors
	HintedUser getNmdcSearchHintedUserEncoded(const string& aNick, const string& aHubIpPort, const string& aUserIP, string& encoding_) noexcept;

	// Get NMDC user + hub URL for UDP commands encoded in UTF-8
	// Returns null user in case of errors
	HintedUser getNmdcSearchHintedUserUtf8(const string& aUtf8Nick, const string& aHubIpPort, const string& aUserIP) noexcept;

	//return users supporting the ASCH extension (and total users)
	pair<size_t, size_t> countAschSupport(const OrderedStringSet& aHubs) const noexcept;

	AdcSupports hubSupports;
	AdcSupports hubUserSupports;
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

	unique_ptr<Socket> udp;
	
	CID pid;
	uint64_t lastOfflineUserCleanup;

	friend class Singleton<ClientManager>;

	ClientManager();

	virtual ~ClientManager();

	/// @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	OnlineUser* findOnlineUserHintUnsafe(const CID& aCID, const string& aHubUrl) const noexcept {
		OnlinePairC p;
		return findOnlineUserHintUnsafe(aCID, aHubUrl, p);
	}
	/**
	* @param p OnlinePair of all the users found by CID, even those who don't match the hint.
	* @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	*/
	OnlineUser* findOnlineUserHintUnsafe(const CID& aCID, const string& aHubUrl, OnlinePairC& p) const noexcept;

	// ClientListener
	void on(ClientListener::Connected, const Client* c) noexcept override;
	void on(ClientListener::UserUpdated, const Client*, const OnlineUserPtr& user) noexcept override;
	void on(ClientListener::UsersUpdated, const Client* c, const OnlineUserList&) noexcept override;
	void on(ClientListener::Disconnected, const string&, const string&) noexcept override;
	void on(ClientListener::HubUpdated, const Client* c) noexcept override;
	void on(ClientListener::HubUserCommand, const Client*, int, int, const string&, const string&) noexcept override;
	void on(ClientListener::OutgoingSearch, const Client*, const SearchPtr&) noexcept override;
	void on(ClientListener::PrivateMessage, const Client*, const ChatMessagePtr&) noexcept override;

	// TimerManagerListener
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;
};

} // namespace dcpp

#endif // !defined(CLIENT_MANAGER_H)