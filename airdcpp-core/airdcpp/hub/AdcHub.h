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

#ifndef DCPLUSPLUS_DCPP_ADC_HUB_H
#define DCPLUSPLUS_DCPP_ADC_HUB_H

#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/hub/Client.h>
#include <airdcpp/protocol/AdcCommand.h>

namespace dcpp {

class ClientManager;
class HBRIValidator;

class AdcHub : public Client, public CommandHandler<AdcHub> {
public:
	using Client::send;
	using Client::connect;

	int connect(const OnlineUser& aUser, const string& aToken, string& lastError_) noexcept override;
	
	bool hubMessageHooked(const OutgoingChatMessage& aMessage, string& error_) noexcept override;
	bool privateMessageHooked(const OnlineUserPtr& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho) noexcept override;
	void sendUserCmd(const UserCommand& command, const ParamMap& params) override;
	void search(const SearchPtr& aSearch) noexcept override;
	bool directSearchHooked(const OnlineUser& user, const SearchPtr& aSearch, string& error_) noexcept override;
	void password(const string& pwd) noexcept override;
	void infoImpl() noexcept override;
	void refreshUserList(bool) noexcept override;

	size_t getUserCount() const noexcept override;

	static string escape(const string& str) noexcept { return AdcCommand::escape(str, false); }
	bool sendHooked(const AdcCommand& cmd, CallerPtr aOwner, string& error_) override;
	bool sendHooked(const AdcCommand& c) {
		string error;
		return sendHooked(c, this, error);
	}

	string getMySID() const noexcept { return AdcCommand::fromSID(mySID); }

	static const vector<StringList>& getSearchExts() noexcept;
	static StringList parseSearchExts(int flag) noexcept;

	// Hub
	static const string BASE_SUPPORT;
	static const string BAS0_SUPPORT;
	static const string UCM0_SUPPORT;
	static const string BLO0_SUPPORT;
	static const string ZLIF_SUPPORT;
	static const string HBRI_SUPPORT;
	static const string TIGR_SUPPORT;

	AdcHub(const string& aHubURL, const ClientPtr& aOldClient = nullptr);
	~AdcHub() override;

	AdcHub(const AdcHub&) = delete;
	AdcHub& operator=(const AdcHub&) = delete;
private:
	friend class ClientManager;
	friend class CommandHandler<AdcHub>;
	friend class Identity;

	/** Map session id to OnlineUser */
	using SIDMap = unordered_map<dcpp::SID, OnlineUserPtr>;

	void getUserList(OnlineUserList& list, bool aListHidden) const noexcept override;

	void connect(const OnlineUser& aUser, const string& aToken, bool aSecure, bool aReplyingRCM = false) noexcept;

	// Can we accept incoming connections from the other user?
	bool acceptUserConnections(const OnlineUser& aUser) const noexcept;

	/* Checks if we are allowed to connect to the user */
	AdcCommand::Error allowConnect(const OnlineUser& aUser, bool aSecure, string& failedProtocol_, bool checkBase) const noexcept;
	/* Does the same thing but also sends the error to the remote user */
	bool checkProtocol(const OnlineUser& aUser, bool& secure_, const string& aRemoteProtocol, const string& aToken) noexcept;
	bool validateConnectUser(const OnlineUserPtr& aUser, bool& secure_, const string& aRemoteProtocol, const string& aToken, const string& aRemotePort) noexcept;

	bool oldPassword = false;
	SIDMap users;
	StringMap lastInfoMap;

	string salt;
	dcpp::SID mySID = 0;

	std::unordered_set<uint32_t> forbiddenCommands;

	static const vector<StringList> searchExtensions;

	string checkNick(const string& nick) noexcept override;

	OnlineUserPtr getUser(dcpp::SID aSID, const CID& aCID) noexcept;
	OnlineUserPtr findUser(dcpp::SID aSID) const noexcept override;
	OnlineUserPtr findUser(const CID& cid) const noexcept;
	
	OnlineUserPtr findUser(const string& aNick) const noexcept override;

	// Returns the user and whether the user had to be created
	pair<OnlineUserPtr, bool> parseInfUser(const AdcCommand& c) noexcept;
	void updateInfUserProperties(const OnlineUserPtr& aUser, const StringList& aParams) noexcept;
	void recalculateConnectModes() noexcept;

	void putUser(dcpp::SID aSID, bool aDisconnectTransfers) noexcept;

	void shutdown(ClientPtr& aClient, bool aRedirect) override;
	void clearUsers() noexcept override;
	void appendConnectivity(StringMap& aLastInfoMap, AdcCommand& c, bool v4, bool v6) const noexcept;
	void appendClientSupports(StringMap& aLastInfoMap, AdcCommand& c, bool v4, bool v6) const noexcept;
	void appendConnectionSpeed(StringMap& aLastInfoMap, AdcCommand& c, const string& aParam, const string& aConnection, int64_t aLimit) const noexcept;

	static void appendHubSupports(AdcCommand& aCmd);

	void handle(AdcCommand::SUP, AdcCommand& c) noexcept;
	void handle(AdcCommand::SID, AdcCommand& c) noexcept;
	void handle(AdcCommand::MSG, AdcCommand& c) noexcept;
	void handle(AdcCommand::INF, AdcCommand& c) noexcept;
	void handle(AdcCommand::GPA, AdcCommand& c) noexcept;
	void handle(AdcCommand::QUI, AdcCommand& c) noexcept;
	void handle(AdcCommand::CTM, AdcCommand& c) noexcept;
	void handle(AdcCommand::RCM, AdcCommand& c) noexcept;
	void handle(AdcCommand::STA, AdcCommand& c) noexcept;
	void handle(AdcCommand::SCH, AdcCommand& c) noexcept;
	void handle(AdcCommand::CMD, AdcCommand& c) noexcept;
	void handle(AdcCommand::RES, AdcCommand& c) noexcept;
	void handle(AdcCommand::GET, AdcCommand& c) noexcept;
	void handle(AdcCommand::NAT, AdcCommand& c) noexcept;
	void handle(AdcCommand::RNT, AdcCommand& c) noexcept;
	void handle(AdcCommand::ZON, AdcCommand& c) noexcept;
	void handle(AdcCommand::ZOF, AdcCommand& c) noexcept;
	void handle(AdcCommand::TCP, AdcCommand& c) noexcept;

	template<typename T> void handle(T, AdcCommand&) { }

	static uint8_t groupExtensions(StringList& exts_, StringList& rx_) noexcept;
	void handleSearchExtensions(AdcCommand& c, const SearchPtr& aSearch, const OnlineUser* aDirectUser) noexcept;
	bool sendSearchHooked(AdcCommand& c, const SearchPtr& aSearch, const OnlineUser* aDirectUser) noexcept;
	void sendSearch(AdcCommand& c);

	bool v4only() const noexcept override { return false; }
	void on(BufferedSocketListener::Connected) noexcept override;
	void on(BufferedSocketListener::Line, const string& aLine) noexcept override;

	void onErrorMessage(const AdcCommand& c, const OnlineUserPtr& aSender) noexcept;

	void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;

	unique_ptr<HBRIValidator> hbriValidator;

	AdcCommand getHBRIRequest(bool v6, const string& aToken) const noexcept;
	void resetHBRI() noexcept;
};

} // namespace dcpp

#endif // !defined(ADC_HUB_H)