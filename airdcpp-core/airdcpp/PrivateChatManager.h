/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_MESSAGE_MANAGER_H_
#define DCPLUSPLUS_DCPP_MESSAGE_MANAGER_H_

#include "forward.h"

#include "ClientManagerListener.h"
#include "ConnectionManagerListener.h"
#include "PrivateChatManagerListener.h"
#include "UserConnectionListener.h"

#include "CriticalSection.h"
#include "PrivateChat.h"
#include "Singleton.h"


namespace dcpp {
	class PrivateChatManager : 
		public Speaker<PrivateChatManagerListener>, public Singleton<PrivateChatManager>,
		private UserConnectionListener, private ConnectionManagerListener, private ClientManagerListener {

	public:
		typedef unordered_map<UserPtr, PrivateChatPtr, User::Hash> ChatMap;

		PrivateChatManager() noexcept;
		~PrivateChatManager() noexcept;

		PrivateChatPtr addChat(const HintedUser& user, bool aReceivedMessage) noexcept;
		PrivateChatPtr getChat(const UserPtr& aUser) const noexcept;

		void DisconnectCCPM(const UserPtr& aUser);
		void onPrivateMessage(const ChatMessagePtr& message);
		bool removeChat(const UserPtr& aUser);
		void closeAll(bool Offline);

		ChatMap getChats() const noexcept;
	private:
		ChatMap chats;
		mutable SharedMutex cs;

		unordered_map<UserPtr, UserConnection*, User::Hash> ccpms;
		UserConnection* getPMConn(const UserPtr& user); //LOCK usage!!

		// ConnectionManagerListener
		void on(ConnectionManagerListener::Connected, const ConnectionQueueItem* cqi, UserConnection* uc) noexcept;
		void on(ConnectionManagerListener::Removed, const ConnectionQueueItem* cqi) noexcept;

		// UserConnectionListener
		void on(UserConnectionListener::PrivateMessage, UserConnection*, const ChatMessagePtr& message) noexcept;
		void on(AdcCommand::PMI, UserConnection* uc, const AdcCommand& cmd) noexcept;

		void on(ClientManagerListener::PrivateMessage, const ChatMessagePtr& aMessage) noexcept;
	};

}
#endif