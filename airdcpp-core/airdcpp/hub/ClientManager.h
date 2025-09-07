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

#include <airdcpp/forward.h>

#include "ClientManagerListener.h"
#include "Client.h"
#include "UserConnectResult.h"

#include <airdcpp/core/timer/TimerManagerListener.h>

#include <airdcpp/core/ActionHook.h>
#include <airdcpp/protocol/AdcCommand.h>
#include <airdcpp/protocol/AdcSupports.h>
#include <airdcpp/connection/ConnectionType.h>
#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/user/OfflineUser.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/core/timer/TimerManager.h>


namespace dcpp {

class ClientManager : public Speaker<ClientManagerListener>, 
	private ClientListener, public Singleton<ClientManager>, 
	private TimerManagerListener
{
	using UserMap = unordered_map<CID *, UserPtr>;
	using UserIter = UserMap::iterator;

public:
	// HOOKS
	ActionHook<MessageHighlightList, const ChatMessagePtr> incomingHubMessageHook, incomingPrivateMessageHook;
	ActionHook<nullptr_t, const OutgoingChatMessage&, const HintedUser, const bool /*echo*/> outgoingPrivateMessageHook;
	ActionHook<nullptr_t, const OutgoingChatMessage&, const Client&> outgoingHubMessageHook;

	ActionHook<AdcCommand::ParamMap, const AdcCommand&, const Client&> outgoingHubCommandHook;
	ActionHook<AdcCommand::ParamMap, const AdcCommand&, const OnlineUserPtr&, const string&> outgoingUdpCommandHook;
	ActionHook<AdcCommand::ParamMap, const AdcCommand&, const UserConnection&> outgoingTcpCommandHook;


	// MESSAGES
	static bool processChatMessage(const ChatMessagePtr& aMessage, const Identity& aMyIdentity, const ActionHook<MessageHighlightList, const ChatMessagePtr>& aHook);

	bool privateMessageHooked(const HintedUser& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho = true) const noexcept;


	// CLIENTS
	AdcSupports hubSupports;
	AdcSupports hubUserSupports;

	// Returns the new ClientPtr
	// NOTE: the main app should perform connecting to the new hub
	ClientPtr createClient(const string& aHubUrl) noexcept;
	ClientPtr findClient(const string& aHubURL) const noexcept;
	ClientPtr findClient(ClientToken aClientId) const noexcept;

	string findClientByIpPort(const string& aIpPort, bool aNmdc) const noexcept;
	
	const Client::UrlMap& getClientsUnsafe() const noexcept { return clients; }
	void getOnlineClients(StringList& onlineClients_) const noexcept;

	bool putClient(ClientPtr& aClient) noexcept;
	void putClients() noexcept;

	// Returns the new ClientPtr
	// NOTE: the main app should perform connecting to the new hub
	ClientPtr redirect(const string& aHubUrl, const string& aNewUrl) noexcept;

	// Send my (possibly) updated fields to all connected hubs
	void myInfoUpdated() noexcept;


	// USERS
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

	StringList getNicks(const UserPtr& aUser) const noexcept;
	StringList getHubNames(const UserPtr& aUser) const noexcept;
	StringList getHubUrls(const UserPtr& aUser) const noexcept;

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
			ouList.erase(unique(ouList.begin(), ouList.end(), [](const OnlineUserPtr& a, const OnlineUserPtr& b) {
				return compare(NameOperator()(a), NameOperator()(b)) == 0; 
			}), ouList.end());
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
	OnlineUserList getOnlineUsers(const UserPtr& aUser) const noexcept;

	string getFormattedNicks(const HintedUser& aUser) const noexcept;
	string getFormattedHubNames(const HintedUser& aUser) const noexcept;
	
	string getNick(const UserPtr& aUser, const string& aHubUrl, bool aAllowFallback = true) const noexcept;

	// Get users with nick matching the pattern. Uses relevancies for priorizing the results.
	OnlineUserList searchNicks(const string& aPattern, size_t aMaxResults, bool aIgnorePrefix, const StringList& aHubUrls) const noexcept;

	// Fire UserUpdated via each connected hub
	void userUpdated(const UserPtr& aUser) const noexcept;

	UserPtr getUser(const CID& cid) noexcept;
	UserPtr loadUser(const string& aCID, const string& aUrl, const string& aNick, time_t aLastSeen = GET_TIME()) noexcept;

	// usage needs to be locked!
	const UserMap& getUsersUnsafe() const { return users; }

	// Return OnlineUser found by CID and hint
	// aAllowFallback: return OnlineUserPtr from any hub if the hinted one is not found
	OnlineUserPtr findOnlineUser(const HintedUser& aUser, bool aAllowFallback = true) const noexcept;
	OnlineUserPtr findOnlineUser(const CID& aCID, const string& aHubUrl, bool aAllowFallback = true) const noexcept;

	using OnlineUserCallback = std::function<void(const OnlineUserPtr&)>;
	void forEachOnlineUser(const OnlineUserCallback& aCallback, bool aIgnoreBots = false) const noexcept;

	UserPtr findUser(const CID& aCID) const noexcept;
	
	void addOfflineUser(const UserPtr& aUser, const string& aNick, const string& aHubUrl, time_t aLastSeen = 0) noexcept;
	optional<OfflineUser> getOfflineUser(const CID& cid);

	void putOnline(const OnlineUserPtr& ou) noexcept;
	void putOffline(const OnlineUserPtr&, bool aDisconnectTransfers = false) noexcept;


	// ME
	UserPtr& getMe() noexcept;

	const CID& getMyCID() noexcept;
	const CID& getMyPID() noexcept;


	// STATS
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
	

	// SEARCHING
	bool directSearchHooked(const HintedUser& user, const SearchPtr& aSearch, string& error_) const noexcept;
	optional<uint64_t> hubSearch(const string& aHubUrl, const SearchPtr& aSearch, string& error_) noexcept;

	bool cancelSearch(CallerPtr aOwner) noexcept;
	optional<uint64_t> getMaxSearchQueueTime(CallerPtr aOwner) const noexcept;
	bool hasSearchQueueOverflow() const noexcept;
	int getMaxSearchQueueSize() const noexcept;

	bool connectADCSearchResult(const CID& aCID, string& token_, string& hubUrl_, string& connection_, uint8_t& slots_) const noexcept;

	// Get ADC hub URL for UDP commands
	// Returns empty string in case of errors
	string getADCSearchHubUrl(const CID& aCID, const string& aHubIpPort) const noexcept;


	// CONNECT/PROTOCOL
	struct OutgoingUDPCommandOptions {
		OutgoingUDPCommandOptions(CallerPtr aOwner, bool aNoPassive) : noPassive(aNoPassive), owner(aOwner) {}

		string encryptionKey;
		bool noCID = false;

		const bool noPassive;
		CallerPtr owner;
	};

	bool sendUDPHooked(AdcCommand& c, const HintedUser& to, const OutgoingUDPCommandOptions& aOptions, string& error_) noexcept;

	UserConnectResult connect(const HintedUser& aUser, const string& aToken, bool aAllowUrlChange, ConnectionType type = CONNECTION_TYPE_LAST) const noexcept;


	SharedMutex& getCS() { return cs; }


	// LEGACY

	bool connectNMDCSearchResult(const string& aUserIP, const string& aHubIpPort, const string& aNick, HintedUser& user_, string& connection_, string& hubEncoding_) noexcept;

	// Get NMDC user + hub URL for UDP commands encoded in legacy encoding
	// Returns null user in case of errors
	HintedUser getNmdcSearchHintedUserEncoded(const string& aNick, const string& aHubIpPort, const string& aUserIP, string& encoding_) noexcept;

	// Get NMDC user + hub URL for UDP commands encoded in UTF-8
	// Returns null user in case of errors
	HintedUser getNmdcSearchHintedUserUtf8(const string& aUtf8Nick, const string& aHubIpPort, const string& aUserIP) noexcept;

	HintedUser findNmdcUser(const string& aNick) const noexcept;
	UserPtr findNmdcUser(const string& aNick, const string& aHubUrl) const noexcept { return findUser(makeNmdcCID(aNick, aHubUrl)); }

	// Create new NMDC user
	UserPtr getNmdcUser(const string& aNick, const string& aHubUrl) noexcept;

	/** Constructs a synthetic, hopefully unique CID */
	CID makeNmdcCID(const string& aNick, const string& aHubUrl) const noexcept;

	void setNmdcIPUser(const UserPtr& aUser, const string& aIP, const string& aUdpPort = Util::emptyString) noexcept;

	const string& findNmdcEncoding(const string& aUrl) const noexcept;

	bool sendNmdcUDP(const string& aData, const string& aIP, const string& aPort) noexcept;
private:
	bool connectADCSearchHubUnsafe(string& token_, string& hubUrl_) const noexcept;

	void addStatsUser(const OnlineUserPtr& aUser, ClientStats& stats_) const noexcept;

	static ClientPtr makeClient(const string& aHubURL, const ClientPtr& aOldClient = nullptr) noexcept;

	using OfflineUserMap = unordered_map<CID *, OfflineUser>;

	using OnlineMap = unordered_multimap<CID*, OnlineUserPtr>;
	using OnlineIter = OnlineMap::iterator;
	using OnlineIterC = OnlineMap::const_iterator;
	using OnlinePair = pair<OnlineIter, OnlineIter>;
	using OnlinePairC = pair<OnlineIterC, OnlineIterC>;
	
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

	~ClientManager() override;

	/// @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	OnlineUserPtr findOnlineUserHintUnsafe(const CID& aCID, const string& aHubUrl) const noexcept {
		OnlinePairC p;
		return findOnlineUserHintUnsafe(aCID, aHubUrl, p);
	}
	/**
	* @param p OnlinePair of all the users found by CID, even those who don't match the hint.
	* @return OnlineUser* found by CID and hint; discard any user that doesn't match the hint.
	*/
	OnlineUserPtr findOnlineUserHintUnsafe(const CID& aCID, const string_view& aHubUrl, OnlinePairC& p) const noexcept;

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

	void cleanUserMap() noexcept;
};

} // namespace dcpp

#endif // !defined(CLIENT_MANAGER_H)