/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_HUBINFO_H
#define DCPLUSPLUS_DCPP_HUBINFO_H

#include <airdcpp/typedefs.h>
#include <airdcpp/GetSet.h>

#include <airdcpp/Client.h>
#include <airdcpp/Message.h>

#include <api/base/HierarchicalApiModule.h>
#include <api/base/HookApiModule.h>
#include <api/OnlineUserUtils.h>

#include <api/common/ChatController.h>
#include <api/common/ListViewController.h>


namespace webserver {
	class HubInfo;

	class HubInfo : public SubApiModule<ClientToken, HubInfo, ClientToken, HookApiModule>, private ClientListener {
	public:
		static const StringList subscriptionList;

		typedef shared_ptr<HubInfo> Ptr;
		typedef vector<Ptr> List;

		HubInfo(ParentType* aParentModule, const ClientPtr& aClient);
		~HubInfo();

		const ClientPtr& getClient() const noexcept { return client; }

		static json serializeConnectState(const ClientPtr& aClient) noexcept;
		static json serializeIdentity(const ClientPtr& aClient) noexcept;
		static json serializeCounts(const ClientPtr& aClient) noexcept;

		void init() noexcept override;
		ClientToken getId() const noexcept override;
	private:
		api_return handleReconnect(ApiRequest& aRequest);
		api_return handleFavorite(ApiRequest& aRequest);
		api_return handlePassword(ApiRequest& aRequest);
		api_return handleRedirect(ApiRequest& aRequest);

		api_return handleGetCounts(ApiRequest& aRequest);
		api_return handleGetUsers(ApiRequest& aRequest);
		api_return handleGetUserCid(ApiRequest& aRequest);
		api_return handleGetUserId(ApiRequest& aRequest);

		void on(ClientListener::Redirect, const Client*, const string&) noexcept override;
		void on(ClientListener::Disconnected, const string&, const string&) noexcept override;
		void on(ClientListener::GetPassword, const Client*) noexcept override;
		void on(ClientListener::HubUpdated, const Client*) noexcept override;
		void on(ClientListener::HubTopic, const Client*, const string&) noexcept override;
		void on(ClientListener::ConnectStateChanged, const Client*, uint8_t) noexcept override;

		void on(ClientListener::UserConnected, const Client*, const OnlineUserPtr&) noexcept override;
		void on(ClientListener::UserUpdated, const Client*, const OnlineUserPtr&) noexcept override;
		void on(ClientListener::UsersUpdated, const Client*, const OnlineUserList&) noexcept override;
		void on(ClientListener::UserRemoved, const Client*, const OnlineUserPtr&) noexcept override;

		void on(ClientListener::Close, const Client*) noexcept override;
		void on(ClientListener::Redirected, const string&, const ClientPtr& aNewClient) noexcept override;

		void on(ClientListener::ChatMessage, const Client*, const ChatMessagePtr& m) noexcept override {
			chatHandler.onChatMessage(m);
		}
		void on(ClientListener::StatusMessage, const Client*, const LogMessagePtr& m, int = ClientListener::FLAG_NORMAL) noexcept override {
			chatHandler.onStatusMessage(m);
		}
		void on(ClientListener::MessagesRead, const Client*) noexcept override {
			chatHandler.onMessagesUpdated();
		}
		void on(ClientListener::MessagesCleared, const Client*) noexcept override {
			chatHandler.onMessagesUpdated();
		}
		void on(ClientListener::ChatCommand, const Client*, const OutgoingChatMessage& aMessage) noexcept override {
			chatHandler.onChatCommand(aMessage);
		}

		OnlineUserList getUsers() noexcept;
		void onUserUpdated(const OnlineUserPtr& ou) noexcept;
		void onUserUpdated(const OnlineUserPtr& ou, const PropertyIdSet& aUpdatedProperties) noexcept;

		json previousCounts;

		void onHubUpdated(const json& aData) noexcept;
		void sendConnectState() noexcept;

		void onTimer() noexcept;

		ChatController<ClientPtr> chatHandler;
		ClientPtr client;

		typedef ListViewController<OnlineUserPtr, OnlineUserUtils::PROP_LAST> UserView;
		UserView view;

		TimerPtr timer;
	};

	typedef HubInfo::Ptr HubInfoPtr;
}

#endif