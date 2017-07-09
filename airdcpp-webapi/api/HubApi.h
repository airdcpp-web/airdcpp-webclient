/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_HUBAPI_H
#define DCPLUSPLUS_DCPP_HUBAPI_H

#include <web-server/stdinc.h>

#include <api/base/HierarchicalApiModule.h>
#include <api/base/HookApiModule.h>
#include <api/HubInfo.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/Client.h>
#include <airdcpp/ClientManagerListener.h>

namespace webserver {
	class HubApi : public ParentApiModule<ClientToken, HubInfo, HookApiModule>, private ClientManagerListener {
	public:
		static StringList subscriptionList;

		HubApi(Session* aSession);
		~HubApi();

		static json serializeClient(const ClientPtr& aClient) noexcept;
	private:
		ActionHookRejectionPtr incomingMessageHook(const ChatMessagePtr& aMessage, const HookRejectionGetter& aRejectionGetter);
		ActionHookRejectionPtr outgoingMessageHook(const string& aMessage, bool aThirdPerson, const Client& aClient, const HookRejectionGetter& aRejectionGetter);

		void addHub(const ClientPtr& aClient) noexcept;

		api_return handlePostMessage(ApiRequest& aRequest);
		api_return handlePostStatus(ApiRequest& aRequest);

		api_return handleConnect(ApiRequest& aRequest);
		api_return handleDeleteSubmodule(ApiRequest& aRequest) override;
		api_return handleGetStats(ApiRequest& aRequest);

		api_return handleFindByUrl(ApiRequest& aRequest);

		void on(ClientManagerListener::ClientCreated, const ClientPtr&) noexcept override;
		void on(ClientManagerListener::ClientRemoved, const ClientPtr&) noexcept override;
	};
}

#endif