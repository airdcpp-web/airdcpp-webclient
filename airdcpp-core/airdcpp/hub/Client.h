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

#ifndef DCPLUSPLUS_DCPP_CLIENT_H
#define DCPLUSPLUS_DCPP_CLIENT_H

#include <airdcpp/core/header/compiler.h>
#include <airdcpp/forward.h>

#include <airdcpp/connection/socket/BufferedSocketListener.h>
#include <airdcpp/hub/ClientListener.h>
#include <airdcpp/share/profiles/ShareProfileManagerListener.h>
#include <airdcpp/core/timer/TimerManagerListener.h>

#include <airdcpp/protocol/AdcSupports.h>
#include <airdcpp/core/classes/FloodCounter.h>
#include <airdcpp/settings/HubSettings.h>
#include <airdcpp/message/MessageCache.h>
#include <airdcpp/user/OnlineUser.h>
#include <airdcpp/search/SearchQueue.h>
#include <airdcpp/core/Speaker.h>

#include <boost/noncopyable.hpp>

namespace dcpp {

class ClientBase
{
public:
	
	ClientBase() { }
	
	virtual const string& getHubUrl() const noexcept = 0;
	virtual string getHubName() const noexcept = 0;
	virtual bool isOp() const noexcept = 0;
	virtual int connect(const OnlineUser& user, const string& token, string& lastError_) noexcept = 0;
	virtual bool privateMessageHooked(const OnlineUserPtr& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho = true) noexcept = 0;
	virtual bool directSearchHooked(const OnlineUser&, const SearchPtr&, string&) noexcept {
		dcassert(0);
		return false;
	}
};

/** Yes, this should probably be called a Hub */
class Client : 
	public ClientBase, public ChatHandlerBase, public Speaker<ClientListener>, public BufferedSocketListener, protected TimerManagerListener, 
	private ShareProfileManagerListener, public HubSettings, private boost::noncopyable {
public:
	using UrlMap = unordered_map<string *, ClientPtr, noCaseStringHash, noCaseStringEq>;
	using IdMap = unordered_map<ClientToken, ClientPtr>;

	virtual void connect(bool withKeyprint = true) noexcept;
	virtual void disconnect(bool graceless) noexcept;

	// Default message method
	bool sendMessageHooked(const OutgoingChatMessage& aMessage, string& error_) noexcept override;
	bool sendPrivateMessageHooked(const OnlineUserPtr& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho = true) noexcept;

	virtual int connect(const OnlineUser& user, const string& token, string& lastError_) noexcept override = 0;
	virtual void sendUserCmd(const UserCommand& command, const ParamMap& params) = 0;

	uint64_t queueSearch(const SearchPtr& aSearch) noexcept;
	optional<uint64_t> getQueueTime(CallerPtr aOwner) const noexcept;
	bool cancelSearch(CallerPtr aOwner) noexcept { return searchQueue.cancelSearch(aOwner); }
	int getSearchQueueSize() const noexcept { return searchQueue.getQueueSize(); }
	bool hasSearchOverflow() const noexcept { return searchQueue.hasOverflow(); }
	
	virtual void password(const string& pwd) noexcept = 0;
	void info();

	virtual size_t getUserCount() const noexcept = 0;
	int64_t getTotalShare() const noexcept { return availableBytes; };
	
	virtual bool sendHooked(const AdcCommand& command, CallerPtr aOwner, string& error_) = 0;

	void callAsync(AsyncF f) noexcept;

	bool isConnected() const noexcept;
	bool isSocketSecure() const noexcept;
	bool isTrusted() const noexcept;
	std::string getEncryptionInfo() const noexcept;
	ByteVector getKeyprint() const noexcept;

	bool isOp() const noexcept override { return getMyIdentity().isOp(); }

	virtual void refreshUserList(bool) noexcept = 0;
	virtual void getUserList(OnlineUserList& list, bool aListHidden) const noexcept = 0;
	virtual OnlineUserPtr findUser(const string& aNick) const noexcept = 0;
	virtual OnlineUserPtr findUser(dcpp::SID aSID) const noexcept = 0;
	
	const string& getPort() const noexcept { return port; }
	const string& getAddress() const noexcept { return address; }

	const string& getIp() const noexcept { return ip; }
	string getIpPort() const noexcept { return getIp() + ':' + port; }

	void updated(const OnlineUserPtr& aUser) noexcept;
	void updated(OnlineUserList& users) noexcept;
	
	void setActive() noexcept;
	void reconnect() noexcept;
	virtual void shutdown(ClientPtr& aClient, bool aRedirect);
	bool isActive() const noexcept;
	bool isActiveV4() const noexcept;
	bool isActiveV6() const noexcept;

	void send(const string& aMessage) { send(aMessage.c_str(), aMessage.length()); }
	void send(const char* aMessage, size_t aLen);

	string getMyNick() const noexcept { return myIdentity.getNick(); }
	string getHubName() const noexcept override { return hubIdentity.getNick().empty() ? getHubUrl() : hubIdentity.getNick(); }
	string getHubDescription() const noexcept { return hubIdentity.getDescription(); }

	GETSET(Identity, myIdentity, MyIdentity);
	GETSET(Identity, hubIdentity, HubIdentity);

	const string& getHubUrl() const noexcept override { return hubUrl; }

	GETSET(string, defpassword, Password);

	GETSET(uint64_t, lastActivity, LastActivity);
	IGETSET(uint32_t, reconnDelay, ReconnDelay, 120);
	
	IGETSET(bool, registered, Registered, false);
	IGETSET(bool, autoReconnect, AutoReconnect, false);
	IGETSET(FavoriteHubToken, favToken, FavToken, 0);

	ClientToken getToken() const noexcept {
		return clientId;
	}

	void setHubSetting(HubSettings::HubBoolSetting aSetting, bool aNewValue) noexcept;

	/* Toggle a hub setting and return the new value */
	bool toggleHubBoolSetting(HubSettings::HubBoolSetting aSetting) noexcept;

	enum CountType: uint8_t {
		COUNT_NORMAL = 0x00,
		COUNT_REGISTERED = 0x01,
		COUNT_OP = 0x04,
		COUNT_UNCOUNTED = 0x08
	};

	CountType getCountType() const noexcept { return countType; }
	long getDisplayCount(CountType aCountType) const noexcept;
	static string getAllCountsStr() noexcept;

	bool isSharingHub() const noexcept;

	void statusMessage(const string& aMessage, LogMessage::Severity aSeverity, LogMessage::Type aType = LogMessage::Type::SERVER, const string& aLabel = Util::emptyString, const string& aOwner = Util::emptyString) noexcept override;

	virtual ~Client();

	const MessageCache& getCache() const noexcept override {
		return cache;
	}

	int clearCache() noexcept override;
	void setRead() noexcept override;
	const string& getRedirectUrl() const noexcept {
		return redirectUrl;
	}

	void doRedirect() noexcept;
	FavoriteHubEntryPtr saveFavorite();

	enum State: uint8_t {
		STATE_CONNECTING,	///< Waiting for socket to connect
		STATE_PROTOCOL,		///< Protocol setup
		STATE_IDENTIFY,		///< Nick setup
		STATE_VERIFY,		///< Checking password
		STATE_NORMAL,		///< Running
		STATE_DISCONNECTED	///< Nothing in particular
	};

	State getConnectState() const noexcept {
		return state;
	}

	bool stateNormal() const noexcept {
		return state == STATE_NORMAL;
	}

	void allowUntrustedConnect() noexcept;
	bool isKeyprintMismatch() const noexcept;

	AdcSupports& getSupports() noexcept {
		return supports;
	}
protected:
	mutable SharedMutex cs;

	virtual bool hubMessageHooked(const OutgoingChatMessage& aMessage, string& error_) noexcept = 0;
	virtual bool privateMessageHooked(const OnlineUserPtr& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho) noexcept override = 0;
	virtual void clearUsers() noexcept = 0;

	void setConnectState(State aState) noexcept;
	MessageCache cache;

	Client(const string& hubURL, char separator, const ClientPtr& aOldClient);

	SearchQueue searchQueue;
	BufferedSocket* sock = nullptr;

	int64_t availableBytes = 0;

	bool updateCounts(bool aRemove) noexcept;
	void updateActivity() noexcept;

	/** Reload details from favmanager or settings */
	void reloadSettings(bool updateNick) noexcept;
	/// Get the external IP the user has defined for this hub, if any.
	const string& getUserIp4() const noexcept;
	const string& getUserIp6() const noexcept;

	string getDescription() const noexcept;

	virtual string checkNick(const string& nick) noexcept = 0;
	virtual void search(const SearchPtr& aSearch) noexcept = 0;
	virtual void infoImpl() noexcept = 0;

	// TimerManagerListener
	virtual void on(Second, uint64_t aTick) noexcept override;

	// BufferedSocketListener
	virtual void on(BufferedSocketListener::Connecting) noexcept override;
	virtual void on(BufferedSocketListener::Connected) noexcept override;
	virtual void on(BufferedSocketListener::Line, const string& aLine) noexcept override;
	virtual void on(BufferedSocketListener::Failed, const string&) noexcept override;

	// ShareManagerListener
	void on(ShareProfileManagerListener::DefaultProfileChanged, ProfileToken aOldDefault, ProfileToken aNewDefault) noexcept override;
	void on(ShareProfileManagerListener::ProfileRemoved, ProfileToken aProfile) noexcept override;

	virtual bool v4only() const noexcept = 0;
	void onPassword() noexcept;

	void onPrivateMessage(const ChatMessagePtr& aMessage) noexcept;
	void onChatMessage(const ChatMessagePtr& aMessage) noexcept;
	void onRedirect(const string& aRedirectUrl) noexcept;

	void onUserConnected(const OnlineUserPtr& aUser) noexcept;
	void onUserDisconnected(const OnlineUserPtr& aUser, bool aDisconnectTransfers) noexcept;

	string redirectUrl;

	FloodCounter ctmFloodCounter;
	FloodCounter searchFloodCounter;

	static FloodCounter::FloodLimits getCTMLimits(const OnlineUserPtr& aAdcUser) noexcept;
	static FloodCounter::FloodLimits getSearchLimits() noexcept;

	bool checkIncomingCTM(const string& aTarget, const OnlineUserPtr& aAdcUser = nullptr) noexcept;
	bool checkIncomingSearch(const string& aTarget, const OnlineUserPtr& aAdcUser = nullptr) noexcept;

	AdcSupports supports;
private:
	const ClientToken clientId;
	static atomic<long> allCounts[COUNT_UNCOUNTED];
	static atomic<long> sharingCounts[COUNT_UNCOUNTED];

	atomic<State> state { STATE_DISCONNECTED };

	const string hubUrl;
	string address;
	string ip;
	string localIp;
	string keyprint;

	string port;
	const char separator;

	// Last used count type information for this hub
	CountType countType = COUNT_UNCOUNTED;
	bool countIsSharing = false;

	void destroySocket(const AsyncF& aShutdownAction = nullptr) noexcept;
	void handleFlood(const FloodCounter::FloodResult& aResult, const string& aMessage) noexcept;
};

} // namespace dcpp

#endif // !defined(CLIENT_H)
