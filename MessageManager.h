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
#include "UserConnection.h"
#include "ConnectionManager.h"
#include "ClientManager.h"
#include "MessageManagerListener.h"
#include "PrivateChat.h"

namespace dcpp {
	class MessageManager : public Speaker<MessageManagerListener>,
		public Singleton<MessageManager>,
		private UserConnectionListener, private ConnectionManagerListener, private ClientManagerListener {

	public:

		MessageManager() noexcept;
		~MessageManager() noexcept;

		PrivateChat* getChat(const HintedUser& user);

		bool isIgnoredOrFiltered(const ChatMessage& msg, Client* client, bool PM); //TODO: Move ignore here
		void DisconnectCCPM(const UserPtr& aUser);
		bool sendPrivateMessage(const HintedUser& aUser, const tstring& msg, string& _error, bool thirdPerson);
		void onPrivateMessage(const ChatMessage& message);
		bool hasWindow(const UserPtr& aUser);
		void closeWindow(const UserPtr& aUser);
		void closeAll(bool Offline);

	private:
		unordered_map<UserPtr, PrivateChat*, User::Hash> chats;
		SharedMutex cs;

		unordered_map<UserPtr, UserConnection*, User::Hash> ccpms;

		// ClientManagerListener
		void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept;
		void on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept;

		// ConnectionManagerListener
		void on(ConnectionManagerListener::Connected, const ConnectionQueueItem* cqi, UserConnection* uc) noexcept;
		void on(ConnectionManagerListener::Removed, const ConnectionQueueItem* cqi) noexcept;

		// UserConnectionListener
		virtual void on(UserConnectionListener::PrivateMessage, UserConnection*, const ChatMessage& message) noexcept;
		UserConnection* getPMConn(const UserPtr& user, UserConnectionListener* listener);
	};

}
#endif