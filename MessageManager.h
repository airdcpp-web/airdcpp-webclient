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

#ifndef MESSAGE_MANAGER_H_
#define MESSAGE_MANAGER_H_

#include "forward.h"

#include "CriticalSection.h"
#include "Exception.h"

#include "Pointer.h"
#include "Singleton.h"
#include "TimerManager.h"
#include "UserConnection.h"
#include "ConnectionManager.h"
#include "MessageManagerListener.h"

namespace dcpp {
	class MessageManager : public Speaker<MessageManagerListener>,
		public Singleton<MessageManager>,
		private UserConnectionListener, private ConnectionManagerListener {

	public:

		MessageManager() noexcept;
		~MessageManager() noexcept;

		bool hasCCPMConn(const UserPtr& user);

		bool isIgnoredOrFiltered(const ChatMessage& msg, Client* client, bool PM); //TODO: Move ignore here
		void DisconnectCCPM(const UserPtr& aUser);
		bool sendPrivateMessage(const HintedUser& aUser, const tstring& msg, string& _error, bool thirdPerson);
		bool StartCCPM(HintedUser& aUser, string& _error, bool& allowAuto);

	private:
		unordered_map<UserPtr, UserConnection*, User::Hash> ccpms;
		CriticalSection ccpmMutex;

		// ConnectionManagerListener
		void on(ConnectionManagerListener::Connected, const ConnectionQueueItem* cqi, UserConnection* uc) noexcept;
		void on(ConnectionManagerListener::Removed, const ConnectionQueueItem* cqi) noexcept;

		// UserConnectionListener
		virtual void on(UserConnectionListener::PrivateMessage, UserConnection* uc, const ChatMessage& message) noexcept;
	};

}
#endif