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

#ifndef DCPLUSPLUS_DCPP_PRIVATEMESSAGE_H
#define DCPLUSPLUS_DCPP_PRIVATEMESSAGE_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/GetSet.h>

#include <airdcpp/Message.h>
#include <airdcpp/PrivateChat.h>
#include <airdcpp/User.h>

#include <api/HierarchicalApiModule.h>

namespace webserver {
	class PrivateChatInfo;

	class PrivateChatInfo : public SubApiModule<CID, PrivateChatInfo, std::string>, private PrivateChatListener {
	public:
		static StringList subscriptionList;

		typedef ParentApiModule<CID, PrivateChatInfo> ParentType;
		typedef shared_ptr<PrivateChatInfo> Ptr;
		typedef vector<Ptr> List;

		PrivateChatInfo(ParentType* aParentModule, const PrivateChatPtr& aChat);
		~PrivateChatInfo();

		PrivateChatPtr getChat() const noexcept { return chat; }

		static json serializeCCPMState(uint8_t aState) noexcept;
	private:
		api_return handleGetMessages(ApiRequest& aRequest);
		api_return handlePostMessage(ApiRequest& aRequest);
		api_return handleSetRead(ApiRequest& aRequest);

		api_return handleDisconnectCCPM(ApiRequest& aRequest);
		api_return handleConnectCCPM(ApiRequest& aRequest);

		api_return handleStartTyping(ApiRequest& aRequest);
		api_return handleEndTyping(ApiRequest& aRequest);

		void on(PrivateChatListener::PrivateMessage, PrivateChat*, const ChatMessagePtr&) noexcept;
		void on(PrivateChatListener::StatusMessage, PrivateChat*, const LogMessagePtr&) noexcept;

		void on(PrivateChatListener::Close, PrivateChat*) noexcept;
		void on(PrivateChatListener::UserUpdated, PrivateChat*) noexcept;
		void on(PrivateChatListener::PMStatus, PrivateChat*, uint8_t) noexcept;
		void on(PrivateChatListener::CCPMStatusUpdated, PrivateChat*) noexcept;
		void on(PrivateChatListener::MessagesRead, PrivateChat*) noexcept;


		void onSessionUpdated(const json& aData) noexcept;
		void sendUnread() noexcept;

		PrivateChatPtr chat;
	};

	typedef PrivateChatInfo::Ptr PrivateChatInfoPtr;
}

#endif