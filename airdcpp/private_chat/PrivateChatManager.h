/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_MESSAGE_MANAGER_H_
#define DCPLUSPLUS_DCPP_MESSAGE_MANAGER_H_

#include <airdcpp/forward.h>

#include <airdcpp/hub/ClientManagerListener.h>
#include <airdcpp/connection/ConnectionManagerListener.h>
#include <airdcpp/private_chat/PrivateChatManagerListener.h>
#include <airdcpp/connection/UserConnectionListener.h>

#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/private_chat/PrivateChat.h>
#include <airdcpp/core/Singleton.h>


namespace dcpp {
	class PrivateChatManager : 
		public Speaker<PrivateChatManagerListener>, public Singleton<PrivateChatManager>,
		private UserConnectionListener, private ConnectionManagerListener, private ClientManagerListener {

	public:
		typedef unordered_map<UserPtr, PrivateChatPtr, User::Hash> ChatMap;

		PrivateChatManager() noexcept;
		~PrivateChatManager() noexcept;

		// Returns the chat session and boolean whether the session was newly created
		pair<PrivateChatPtr, bool> addChat(const HintedUser& user, bool aReceivedMessage) noexcept;
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
		void on(ConnectionManagerListener::Connected, const ConnectionQueueItem* cqi, UserConnection* uc) noexcept override;
		void on(ConnectionManagerListener::Removed, const ConnectionQueueItem* cqi) noexcept override;

		// UserConnectionListener
		void on(UserConnectionListener::PrivateMessage, UserConnection*, const ChatMessagePtr& message) noexcept override;
		void on(AdcCommand::PMI, UserConnection* uc, const AdcCommand& cmd) noexcept override;

		void on(ClientManagerListener::PrivateMessage, const ChatMessagePtr& aMessage) noexcept override;
	};

}
#endif