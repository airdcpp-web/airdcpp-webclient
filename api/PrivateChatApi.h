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

#ifndef DCPLUSPLUS_DCPP_PRIVATEMESSAGEAPI_H
#define DCPLUSPLUS_DCPP_PRIVATEMESSAGEAPI_H

#include <api/base/HookApiModule.h>
#include <api/base/HierarchicalApiModule.h>
#include <api/PrivateChatInfo.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/PrivateChatManagerListener.h>

namespace webserver {
	class PrivateChatApi : public ParentApiModule<CID, PrivateChatInfo, HookApiModule>, private PrivateChatManagerListener {
	public:
		static StringList subscriptionList;

		PrivateChatApi(Session* aSession);
		~PrivateChatApi();
	private:
		ActionHookRejectionPtr incomingMessageHook(const ChatMessagePtr& aMessage, const HookRejectionGetter& aRejectionGetter);
		ActionHookRejectionPtr outgoingMessageHook(const string& aMessage, bool aThirdPerson, const HintedUser& aUser, bool aEcho, const HookRejectionGetter& aRejectionGetter);

		void addChat(const PrivateChatPtr& aChat) noexcept;

		api_return handlePostChat(ApiRequest& aRequest);
		api_return handleDeleteSubmodule(ApiRequest& aRequest) override;

		api_return handlePostMessage(ApiRequest& aRequest);

		void on(PrivateChatManagerListener::ChatCreated, const PrivateChatPtr& aChat, bool aReceivedMessage) noexcept override;
		void on(PrivateChatManagerListener::ChatRemoved, const PrivateChatPtr& aChat) noexcept override;

		static json serializeChat(const PrivateChatPtr& aChat) noexcept;
	};
}

#endif