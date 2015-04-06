/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#ifndef PRIVATE_CHAT_H
#define PRIVATE_CHAT_H_

#include "forward.h"

#include "CriticalSection.h"
#include "Exception.h"
#include "Pointer.h"

#include "UserConnection.h"
#include "ClientManager.h"
#include "PrivateChatListener.h"
#include "DelayedEvents.h"


namespace dcpp {
	class PrivateChat : public Speaker<PrivateChatListener>, public UserConnectionListener,
		private ClientManagerListener, private boost::noncopyable {
	public:
		
		enum PMInfo {
			MSG_SEEN,  //Message seen, CPMI SN1
			TYPING_ON, //User started typing, CPMI TP1
			TYPING_OFF, //User stopped typing, CPMI TP0
			NO_AUTOCONNECT, //User Disconnected manually, Disable auto connect, CPMI AC0
			QUIT // The PM window was closed, Disconnect once both sides close, CPMI QU1
		};

		PrivateChat(const HintedUser& aUser, UserConnection* aUc = nullptr);
		~PrivateChat();

		bool sendPrivateMessage(const HintedUser& aUser, const string& msg, string& error_, bool thirdPerson);
		void Message(const ChatMessage& aMessage);

		void Activate(const string& msg, Client* c);
		void Close();

		void CloseCC(bool now, bool noAutoConnect);
		void StartCC();
		void onExit();
		void checkAlwaysCCPM();
		bool ccReady() const { return state == CONNECTED; };
		void setUc(UserConnection* aUc){ uc = aUc; state = aUc ? CONNECTED : DISCONNECTED; }
		UserConnection* getUc() { return uc; }
		void sendPMInfo(uint8_t aType);

		void CCPMConnected(UserConnection* uc);
		void CCPMDisconnected();

		void setHubUrl(const string& hint) { replyTo.hint = hint; }
		const UserPtr& getUser() const { return replyTo.user; }
		const string& getHubUrl() const { return replyTo.hint; }
		const HintedUser& getHintedUser() const { return replyTo; }

		Client* getClient() {
			return ClientManager::getInstance()->getClient(replyTo.hint);
		}
		
		GETSET(bool, supportsCCPM, SupportsCCPM);
		GETSET(string, lastCCPMError, LastCCPMError);
	
	private:

		enum State {
			CONNECTING,
			CONNECTED,
			DISCONNECTED
		};

		enum EventType {
			USER_UPDATE,
			CCPM_TIMEOUT,
			CCPM_AUTO
		};

		void checkCCPMTimeout();

		HintedUser replyTo;

		int ccpmAttempts;
		bool allowAutoCCPM;
		uint64_t lastCCPMAttempt;

		atomic<State> state;
		UserConnection* uc;

		DelayedEvents<uint8_t> delayEvents;

		// UserConnectionListener
		virtual void on(UserConnectionListener::PrivateMessage, UserConnection*, const ChatMessage& message) noexcept{
			Message(message);
		}
		virtual void on(AdcCommand::PMI, UserConnection*, const AdcCommand& cmd) noexcept;

		// ClientManagerListener
		void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept;
		void on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept;

	};
}

#endif