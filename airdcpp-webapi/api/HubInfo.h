/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/GetSet.h>

#include <airdcpp/Client.h>
#include <airdcpp/Message.h>
#include <airdcpp/MessageManagerListener.h>

#include <api/HierarchicalApiModule.h>
#include <api/common/ChatController.h>
#include <api/common/ListViewController.h>
#include <api/common/Property.h>


namespace webserver {
	class HubInfo;

	class HubInfo : public SubApiModule<ClientToken, HubInfo, ClientToken>, private ClientListener, private MessageManagerListener {
	public:
		static const StringList subscriptionList;

		typedef ParentApiModule<ClientToken, HubInfo> ParentType;
		typedef shared_ptr<HubInfo> Ptr;
		typedef vector<Ptr> List;

		HubInfo(ParentType* aParentModule, const ClientPtr& aClient);
		~HubInfo();

		ClientPtr getClient() const noexcept { return client; }

		static json serializeConnectState(const ClientPtr& aClient) noexcept;
		static json serializeIdentity(const ClientPtr& aClient) noexcept;
		static json serializeCounts(const ClientPtr& aClient) noexcept;

		enum {
			PROP_TOKEN = -1,
			PROP_NICK,
			PROP_SHARED,
			PROP_DESCRIPTION,
			PROP_TAG,
			PROP_UPLOAD_SPEED,
			PROP_DOWNLOAD_SPEED,
			PROP_IP4,
			PROP_IP6,
			PROP_EMAIL,
			//PROP_ACTIVE4,
			//PROP_ACTIVE6,
			PROP_FILES,
			PROP_HUB_URL,
			PROP_HUB_NAME,
			PROP_FLAGS,
			PROP_CID,
			PROP_LAST
		};

		static const PropertyList properties;

		static PropertyItemHandler<OnlineUserPtr> onlineUserPropertyHandler;
	private:
		api_return handleReconnect(ApiRequest& aRequest);
		api_return handleFavorite(ApiRequest& aRequest);
		api_return handlePassword(ApiRequest& aRequest);
		api_return handleRedirect(ApiRequest& aRequest);

		api_return handleGetCounts(ApiRequest& aRequest);

		void on(Redirect, const Client*, const string&) noexcept;
		void on(Failed, const string&, const string&) noexcept;
		void on(GetPassword, const Client*) noexcept;
		void on(HubUpdated, const Client*) noexcept;
		void on(HubTopic, const Client*, const string&) noexcept;
		void on(ConnectStateChanged, const Client*, uint8_t) noexcept;

		void on(UserConnected, const Client*, const OnlineUserPtr&) noexcept;
		void on(UserUpdated, const Client*, const OnlineUserPtr&) noexcept;
		void on(UsersUpdated, const Client*, const OnlineUserList&) noexcept;
		void on(UserRemoved, const Client*, const OnlineUserPtr&) noexcept;

		void on(MessageManagerListener::IgnoreAdded, const UserPtr& aUser) noexcept;
		void on(MessageManagerListener::IgnoreRemoved, const UserPtr& aUser) noexcept;

		void on(Disconnecting, const Client*) noexcept;
		void on(Redirected, const string&, const ClientPtr& aNewClient) noexcept;

		void on(ChatMessage, const Client*, const ChatMessagePtr& m) noexcept {
			chatHandler.onChatMessage(m);
		}
		void on(StatusMessage, const Client*, const LogMessagePtr& m, int = ClientListener::FLAG_NORMAL) noexcept {
			chatHandler.onStatusMessage(m);
		}
		void on(MessagesRead, const Client*) noexcept {
			chatHandler.onMessagesUpdated();
		}
		void on(MessagesCleared, const Client*) noexcept {
			chatHandler.onMessagesUpdated();
		}

		OnlineUserList getUsers() noexcept;
		void onUserUpdated(const OnlineUserPtr& ou) noexcept;
		void onUserUpdated(const OnlineUserPtr& ou, PropertyIdSet aUpdatedProperties) noexcept;
		void onFlagsUpdated(const UserPtr& aUser) noexcept;

		json previousCounts;

		void onHubUpdated(const json& aData) noexcept;
		void sendConnectState() noexcept;

		void onTimer() noexcept;

		ChatController<ClientPtr> chatHandler;
		ClientPtr client;

		typedef ListViewController<OnlineUserPtr, PROP_LAST> UserView;
		UserView view;

		TimerPtr timer;
	};

	typedef HubInfo::Ptr HubInfoPtr;
}

#endif