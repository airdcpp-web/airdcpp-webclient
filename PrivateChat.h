/*
* Copyright (C) 2011-2014 AirDC++ Project
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
	class PrivateChat : public Speaker<PrivateChatListener>, public UserConnectionListener, private boost::noncopyable {
	public:
		
		PrivateChat(const HintedUser& aUser) : uc(nullptr), replyTo(aUser), ccpmAttempts(0), allowAutoCCPM(true), lastCCPMAttempt(0), state(DISCONNECTED) {
			string _err = Util::emptyString;;
			supportsCCPM = ClientManager::getInstance()->getSupportsCCPM(aUser.user, _err);
			lastCCPMError = _err;
		}
		~PrivateChat() {}
		
		void CCPMConnected(UserConnection* uc);

		void CCPMDisconnected();

		bool sendPrivateMessage(const HintedUser& aUser, const string& msg, string& error_, bool thirdPerson);

		void Disconnect();

		void UserDisconnected(bool wentOffline);

		void UserUpdated(const OnlineUser& aUser);

		void Message(const ChatMessage& aMessage);

		void Activate(const HintedUser& replyTo, const string& msg, Client* c);
		
		void Close();

		void StartCCPM(HintedUser& aUser, string& _err, bool& allowAuto);

		void StartCC();

		void checkAlwaysCCPM();
		bool ccReady() const { return state == CONNECTED; };
		void setUc(UserConnection* aUc){ uc = aUc; state = aUc ? CONNECTED : DISCONNECTED; }
		
		GETSET(bool, supportsCCPM, SupportsCCPM);
		GETSET(string, lastCCPMError, LastCCPMError);
		
		HintedUser replyTo;

	private:
		enum EventType {
			USER_UPDATE,
			CCPM_TIMEOUT,
			CCPM_AUTO
		};
		enum State {
			CONNECTING,
			CONNECTED,
			DISCONNECTED
		};

		void checkCCPMTimeout();


		int ccpmAttempts;
		bool allowAutoCCPM;
		uint64_t lastCCPMAttempt;
		atomic<State> state;
		UserConnection* uc;

		DelayedEvents<EventType> delayEvents;

		// UserConnectionListener
		virtual void on(UserConnectionListener::PrivateMessage, UserConnection*, const ChatMessage& message) noexcept{
			Message(message);
		}
	};
}

#endif