/*
* Copyright (C) 2011-2022 AirDC++ Project
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

#ifndef DCPP_PRIVATE_CHAT_H
#define DCPP_PRIVATE_CHAT_H

#include "forward.h"

#include "ClientManagerListener.h"
#include "DelayedEvents.h"
#include "MessageCache.h"
#include "PrivateChatListener.h"
#include "UserConnection.h"

namespace dcpp {
	class PrivateChat: public ChatHandlerBase, public Speaker<PrivateChatListener>, public UserConnectionListener,
		private ClientManagerListener, private boost::noncopyable {
	public:
		
		enum PMInfo: uint8_t {
			//CPMI types
			MSG_SEEN,		// Message seen, CPMI SN1
			TYPING_ON,		// User started typing, CPMI TP1
			TYPING_OFF,		// User stopped typing, CPMI TP0
			NO_AUTOCONNECT, // User Disconnected manually, Disable auto connect, CPMI AC0
			QUIT,			// The PM window was closed, Disconnect once both sides close, CPMI QU1
			PMINFO_LAST

		};

		enum CCPMState : uint8_t {
			CONNECTING,
			CONNECTED,
			DISCONNECTED
		};

		static const string& ccpmStateToString(uint8_t aState) noexcept;

		PrivateChat(const HintedUser& aUser, UserConnection* aUc = nullptr);
		~PrivateChat();

		const CID& getToken() const noexcept {
			return replyTo.user->getCID();
		}

		bool sendMessageHooked(const OutgoingChatMessage& aMessage, string& error_) override;
		void handleMessage(const ChatMessagePtr& aMessage) noexcept;
		void statusMessage(const string& aMessage, LogMessage::Severity aSeverity, const string& aLabel = Util::emptyString) noexcept override;

		void close();

		void closeCC(bool now, bool noAutoConnect);
		void startCC();
		bool ccReady() const { return ccpmState == CONNECTED; };
		UserConnection* getUc() { return uc; }
		void sendPMInfo(uint8_t aType);

		void CCPMConnected(UserConnection* uc);
		void CCPMDisconnected();

		void setHubUrl(const string& hint) noexcept;
		const UserPtr& getUser() const noexcept { return replyTo.user; }
		const string& getHubUrl() const noexcept override { return replyTo.hint; }
		const HintedUser& getHintedUser() const noexcept { return replyTo; }

		ClientPtr getClient() const noexcept;

		string getLastCCPMError();
	
		void logMessage(const string& aMessage) const noexcept;
		void fillLogParams(ParamMap& params) const noexcept;
		string getLogPath() const noexcept;
		bool isOnline() const noexcept { return online; }

		bool allowCCPM();

		CCPMState getCCPMState() const noexcept {
			return ccpmState;
		}

		const MessageCache& getCache() const noexcept override {
			return cache;
		}

		void setRead() noexcept override;
		int clearCache() noexcept override;

		// Posts an info status message of the user is ignored
		void checkIgnored() noexcept;
	private:
		MessageCache cache;
		enum EventType {
			USER_UPDATE,
			CCPM_TIMEOUT,
			CCPM_AUTO
		};

		void checkAlwaysCCPM();
		void checkCCPMTimeout();
		void checkCCPMHubBlocked() noexcept;
		void setUc(UserConnection* aUc) noexcept { uc = aUc; ccpmState = aUc ? CONNECTED : DISCONNECTED; }

		HintedUser replyTo;

		int ccpmAttempts;
		bool allowAutoCCPM;
		uint64_t lastCCPMAttempt;

		atomic<CCPMState> ccpmState;
		UserConnection* uc;

		DelayedEvents<uint8_t> delayEvents;

		// UserConnectionListener
		virtual void on(UserConnectionListener::PrivateMessage, UserConnection*, const ChatMessagePtr& message) noexcept override {
			handleMessage(message);
		}
		virtual void on(AdcCommand::PMI, UserConnection*, const AdcCommand& cmd) noexcept override;

		// ClientManagerListener
		void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool aWasOffline) noexcept override;
		void on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept override;
		void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool aWentOffline) noexcept override;

		bool online = true;

		// Last hubname (that was used for messaging)
		string hubName;

		// Checks that the user still exists in the hinted hub and changes to another hub when needed
		void checkUserHub(bool aWentOffline) noexcept;

		void onUserUpdated(const OnlineUser& aUser) noexcept;
	};
}

#endif