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

#include "stdinc.h"

#include <api/PrivateChatApi.h>

#include <api/common/Deserializer.h>
#include <api/common/MessageUtils.h>
#include <api/common/Serializer.h>

#include <airdcpp/ClientManager.h>
#include <airdcpp/PrivateChatManager.h>
#include <airdcpp/ScopedFunctor.h>

namespace webserver {
	StringList PrivateChatApi::subscriptionList = {
		"private_chat_created",
		"private_chat_removed"
	};

	ActionHookResult<> PrivateChatApi::incomingMessageHook(const ChatMessagePtr& aMessage, const ActionHookResultGetter<>& aResultGetter) {
		return HookCompletionData::toResult(
			fireHook("private_chat_incoming_message_hook", 2, [&]() {
				return MessageUtils::serializeChatMessage(aMessage);
			}),
			aResultGetter
		);
	}

	ActionHookResult<> PrivateChatApi::outgoingMessageHook(const OutgoingChatMessage& aMessage, const HintedUser& aUser, bool aEcho, const ActionHookResultGetter<>& aResultGetter) {
		return HookCompletionData::toResult(
			fireHook("private_chat_outgoing_message_hook", 2, [&]() {
				return json({
					{ "text", aMessage.text },
					{ "third_person", aMessage.thirdPerson },
					{ "echo", aEcho },
					{ "user", Serializer::serializeHintedUser(aUser) },
				});
			}),
			aResultGetter
		);
	}

	PrivateChatApi::PrivateChatApi(Session* aSession) : 
		ParentApiModule(CID_PARAM, Access::PRIVATE_CHAT_VIEW, aSession, subscriptionList, PrivateChatInfo::subscriptionList,
			[](const string& aId) { return Deserializer::parseCID(aId); },
			[](const PrivateChatInfo& aInfo) { return serializeChat(aInfo.getChat()); },
			Access::PRIVATE_CHAT_EDIT
		) {

		PrivateChatManager::getInstance()->addListener(this);

		createHook("private_chat_incoming_message_hook", [this](const string& aId, const string& aName) {
			return ClientManager::getInstance()->incomingPrivateMessageHook.addSubscriber(aId, aName, HOOK_HANDLER(PrivateChatApi::incomingMessageHook));
		}, [this](const string& aId) {
			ClientManager::getInstance()->incomingPrivateMessageHook.removeSubscriber(aId);
		});

		createHook("private_chat_outgoing_message_hook", [this](const string& aId, const string& aName) {
			return ClientManager::getInstance()->outgoingPrivateMessageHook.addSubscriber(aId, aName, HOOK_HANDLER(PrivateChatApi::outgoingMessageHook));
		}, [this](const string& aId) {
			ClientManager::getInstance()->outgoingPrivateMessageHook.removeSubscriber(aId);
		});

		METHOD_HANDLER(Access::PRIVATE_CHAT_EDIT,	METHOD_POST,	(),								PrivateChatApi::handlePostChat);

		METHOD_HANDLER(Access::PRIVATE_CHAT_SEND,	METHOD_POST,	(EXACT_PARAM("chat_message")),	PrivateChatApi::handlePostMessage);

		auto rawChats = PrivateChatManager::getInstance()->getChats();
		for (const auto& c : rawChats | map_values) {
			addChat(c);
		}
	}

	PrivateChatApi::~PrivateChatApi() {
		PrivateChatManager::getInstance()->removeListener(this);
	}

	api_return PrivateChatApi::handlePostChat(ApiRequest& aRequest) {
		auto user = Deserializer::deserializeHintedUser(aRequest.getRequestBody());
		auto res = PrivateChatManager::getInstance()->addChat(user, false);
		if (!res.second) {
			aRequest.setResponseErrorStr("Chat session exists");
			return websocketpp::http::status_code::conflict;
		}

		aRequest.setResponseBody(serializeChat(res.first));
		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatApi::handleDeleteSubmodule(ApiRequest& aRequest) {
		auto chat = getSubModule(aRequest);

		PrivateChatManager::getInstance()->removeChat(chat->getChat()->getUser());
		return websocketpp::http::status_code::no_content;
	}

	api_return PrivateChatApi::handlePostMessage(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto user = Deserializer::deserializeHintedUser(reqJson);
		auto message = Deserializer::deserializeChatMessage(reqJson);
		auto echo = JsonUtil::getOptionalFieldDefault<bool>("echo", reqJson, false);

		const auto complete = aRequest.defer();
		addAsyncTask([=] {
			string error_;
			if (!ClientManager::getInstance()->privateMessageHooked(user, OutgoingChatMessage(message.first, aRequest.getSession().get(), message.second), error_, echo)) {
				complete(websocketpp::http::status_code::internal_server_error, nullptr, ApiRequest::toResponseErrorStr(error_));
			} else {
				complete(websocketpp::http::status_code::no_content, nullptr, nullptr);
			}
		});

		return websocketpp::http::status_code::see_other;
	}

	void PrivateChatApi::on(PrivateChatManagerListener::ChatRemoved, const PrivateChatPtr& aChat) noexcept {
		removeSubModule(aChat->getUser()->getCID());

		if (!subscriptionActive("private_chat_removed")) {
			return;
		}

		send("private_chat_removed", serializeChat(aChat));
	}

	void PrivateChatApi::addChat(const PrivateChatPtr& aChat) noexcept {
		addSubModule(aChat->getUser()->getCID(), std::make_shared<PrivateChatInfo>(this, aChat));
	}

	void PrivateChatApi::on(PrivateChatManagerListener::ChatCreated, const PrivateChatPtr& aChat, bool /*aReceivedMessage*/) noexcept {
		addChat(aChat);
		if (!subscriptionActive("private_chat_created")) {
			return;
		}

		send("private_chat_created", serializeChat(aChat));
	}

	json PrivateChatApi::serializeChat(const PrivateChatPtr& aChat) noexcept {
		return {
			{ "id", aChat->getUser()->getCID().toBase32() },
			{ "user", Serializer::serializeHintedUser(aChat->getHintedUser()) },
			{ "ccpm_state", PrivateChatInfo::serializeCCPMState(aChat) },
			{ "message_counts", MessageUtils::serializeCacheInfo(aChat->getCache(), MessageUtils::serializeUnreadChat) },
		};
	}
}