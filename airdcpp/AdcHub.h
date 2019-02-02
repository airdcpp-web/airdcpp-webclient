/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_ADC_HUB_H
#define DCPLUSPLUS_DCPP_ADC_HUB_H

#include "typedefs.h"

#include "Client.h"
#include "CriticalSection.h"
#include "AdcCommand.h"
#include "Socket.h"

#include <future>

namespace dcpp {

class ClientManager;

class AdcHub : public Client, public CommandHandler<AdcHub> {
public:
	using Client::send;
	using Client::connect;

	int connect(const OnlineUser& aUser, const string& aToken, string& lastError_) noexcept override;
	
	bool hubMessage(const string& aMessage, string& error_, bool thirdPerson = false) noexcept override;
	bool privateMessage(const OnlineUserPtr& aUser, const string& aMessage, string& error_, bool aThirdPerson, bool aEcho) noexcept override;
	void sendUserCmd(const UserCommand& command, const ParamMap& params) override;
	void search(const SearchPtr& aSearch) noexcept override;
	bool directSearch(const OnlineUser& user, const SearchPtr& aSearch, string& error_) noexcept override;
	void password(const string& pwd) noexcept override;
	void infoImpl() noexcept override;
	void refreshUserList(bool) noexcept override;

	size_t getUserCount() const noexcept override;

	static string escape(const string& str) noexcept { return AdcCommand::escape(str, false); }
	bool send(const AdcCommand& cmd) override;

	string getMySID() const noexcept { return AdcCommand::fromSID(sid); }

	static const vector<StringList>& getSearchExts() noexcept;
	static StringList parseSearchExts(int flag) noexcept;

	static const string CLIENT_PROTOCOL;
	static const string SECURE_CLIENT_PROTOCOL_TEST;
	static const string ADCS_FEATURE;
	static const string TCP4_FEATURE;
	static const string TCP6_FEATURE;
	static const string UDP4_FEATURE;
	static const string UDP6_FEATURE;
	static const string NAT0_FEATURE;
	static const string SEGA_FEATURE;
	static const string BASE_SUPPORT;
	static const string BAS0_SUPPORT;
	static const string TIGR_SUPPORT;
	static const string UCM0_SUPPORT;
	static const string BLO0_SUPPORT;
	static const string ZLIF_SUPPORT;
	static const string SUD1_FEATURE;
	static const string HBRI_SUPPORT;
	static const string ASCH_FEATURE;
	static const string CCPM_FEATURE;

	AdcHub(const string& aHubURL, const ClientPtr& aOldClient = nullptr);
	~AdcHub();

	AdcHub(const AdcHub&) = delete;
	AdcHub& operator=(const AdcHub&) = delete;
private:
	bool isHubsoftVersionOrOlder(const string& aHubsoft, double aVersion);

	friend class ClientManager;
	friend class CommandHandler<AdcHub>;
	friend class Identity;

	/** Map session id to OnlineUser */
	typedef unordered_map<uint32_t, OnlineUser*> SIDMap;

	void getUserList(OnlineUserList& list, bool aListHidden) const noexcept override;

	void connect(const OnlineUser& aUser, const string& aToken, bool aSecure, bool aReplyingRCM = false) noexcept;

	/* Checks if we are allowed to connect to the user */
	AdcCommand::Error allowConnect(const OnlineUser& aUser, bool aSecure, string& failedProtocol_, bool checkBase) const noexcept;
	/* Does the same thing but also sends the error to the remote user */
	bool checkProtocol(const OnlineUser& aUser, bool& secure_, const string& aRemoteProtocol, const string& aToken) noexcept;

	bool oldPassword = false;
	Socket udp;
	SIDMap users;
	StringMap lastInfoMap;
	mutable SharedMutex cs;

	string salt;
	uint32_t sid = 0;

	std::unordered_set<uint32_t> forbiddenCommands;

	static const vector<StringList> searchExtensions;

	string checkNick(const string& nick) noexcept override;

	OnlineUser& getUser(const uint32_t aSID, const CID& aCID) noexcept;
	OnlineUser* findUser(const uint32_t aSID) const noexcept;
	OnlineUser* findUser(const CID& cid) const noexcept;
	
	OnlineUserPtr findUser(const string& aNick) const noexcept override;

	void putUser(const uint32_t aSID, bool aDisconnectTransfers) noexcept;

	void shutdown(ClientPtr& aClient, bool aRedirect) override;
	void clearUsers() noexcept override;
	void appendConnectivity(StringMap& aLastInfoMap, AdcCommand& c, bool v4, bool v6) const noexcept;

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
	void handle(AdcCommand::PSR, AdcCommand& c) noexcept;
	void handle(AdcCommand::PBD, AdcCommand& c) noexcept;
	void handle(AdcCommand::UBD, AdcCommand& c) noexcept;
	void handle(AdcCommand::ZON, AdcCommand& c) noexcept;
	void handle(AdcCommand::ZOF, AdcCommand& c) noexcept;
	void handle(AdcCommand::TCP, AdcCommand& c) noexcept;

	template<typename T> void handle(T, AdcCommand&) { }

	void constructSearch(AdcCommand& c, const SearchPtr& aSearch, bool isDirect) noexcept;
	void sendSearch(AdcCommand& c);
	void sendUDP(const AdcCommand& cmd) noexcept;

	bool v4only() const noexcept override { return false; }
	void on(BufferedSocketListener::Connected) noexcept override;
	void on(BufferedSocketListener::Line, const string& aLine) noexcept override;

	void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;

	bool supportsHBRI = false;
	unique_ptr<std::thread> hbriThread;
	void sendHBRI(const string& aIP, const string& aPort, const string& aToken, bool v6);
	bool stopValidation = false;
};

} // namespace dcpp

#endif // !defined(ADC_HUB_H)