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
		
		enum EventType {
			USER_UPDATE,
			CCPM_TIMEOUT,
			CCPM_AUTO,
			MSG_SEEN,
			TYPING_ON,
			TYPING_OFF
		};

		PrivateChat(const HintedUser& aUser, UserConnection* aUc = nullptr);
		~PrivateChat();

		void CCPMConnected(UserConnection* uc);

		void CCPMDisconnected();

		bool sendPrivateMessage(const HintedUser& aUser, const string& msg, string& error_, bool thirdPerson);

		void Disconnect(bool manual = false);

		void Message(const ChatMessage& aMessage);

		void Activate(const string& msg, Client* c);
		
		void Close();

		void StartCCPM(HintedUser& aUser, string& _err, bool& allowAuto);

		void StartCC();

		void checkAlwaysCCPM();
		bool ccReady() const { return state == CONNECTED; };
		void setUc(UserConnection* aUc){ uc = aUc; state = aUc ? CONNECTED : DISCONNECTED; }

		void setHubUrl(const string& hint) { replyTo.hint = hint; }
		const UserPtr& getUser() const { return replyTo.user; }
		const string& getHubUrl() const { return replyTo.hint; }
		const HintedUser& getHintedUser() const { return replyTo; }

		Client* getClient() {
			return ClientManager::getInstance()->getClient(replyTo.hint);
		}

		void sendPMInfo(uint8_t aType);
		
		GETSET(bool, supportsCCPM, SupportsCCPM);
		GETSET(string, lastCCPMError, LastCCPMError);
	

	private:

		enum State {
			CONNECTING,
			CONNECTED,
			DISCONNECTED
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

		// ClientManagerListener
		void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept;
		void on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept;

		virtual void on(AdcCommand::PMI, UserConnection*, const AdcCommand& cmd) noexcept;
	};
}

#endif