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

#include <api/PrivateChatApi.h>

#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <airdcpp/ClientManager.h>
#include <airdcpp/ScopedFunctor.h>

namespace webserver {
	StringList PrivateChatApi::subscriptionList = {
		"private_chat_created",
		"private_chat_removed"
	};

	PrivateChatApi::PrivateChatApi(Session* aSession) : 
		ParentApiModule("sessions", CID_PARAM, Access::PRIVATE_CHAT_VIEW, aSession, subscriptionList, PrivateChatInfo::subscriptionList,
			[](const string& aId) { return Deserializer::parseCID(aId); },
			[](const PrivateChatInfo& aInfo) { return serializeChat(aInfo.getChat()); }
		) {

		MessageManager::getInstance()->addListener(this);

		METHOD_HANDLER(Access::PRIVATE_CHAT_EDIT,	METHOD_DELETE,	(EXACT_PARAM("sessions"), CID_PARAM),	PrivateChatApi::handleDeleteChat);
		METHOD_HANDLER(Access::PRIVATE_CHAT_EDIT,	METHOD_POST,	(EXACT_PARAM("sessions")),				PrivateChatApi::handlePostChat);

		METHOD_HANDLER(Access::PRIVATE_CHAT_SEND,	METHOD_POST,	(EXACT_PARAM("chat_message")),			PrivateChatApi::handlePostMessage);

		auto rawChats = MessageManager::getInstance()->getChats();
		for (const auto& c : rawChats | map_values) {
			addChat(c);
		}
	}

	PrivateChatApi::~PrivateChatApi() {
		MessageManager::getInstance()->removeListener(this);
	}

	api_return PrivateChatApi::handlePostChat(ApiRequest& aRequest) {
		auto user = Deserializer::deserializeHintedUser(aRequest.getRequestBody());
		auto chat = MessageManager::getInstance()->addChat(user, false);
		if (!chat) {
			aRequest.setResponseErrorStr("Chat session exists");
			return websocketpp::http::status_code::conflict;
		}

		aRequest.setResponseBody(serializeChat(chat));
		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatApi::handleDeleteChat(ApiRequest& aRequest) {
		auto chat = getSubModule(aRequest);

		MessageManager::getInstance()->removeChat(chat->getChat()->getUser());
		return websocketpp::http::status_code::no_content;
	}

	api_return PrivateChatApi::handlePostMessage(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto user = Deserializer::deserializeHintedUser(reqJson);
		auto message = Deserializer::deserializeChatMessage(reqJson);
		auto echo = JsonUtil::getOptionalFieldDefault<bool>("echo", reqJson, false);

		string error_;
		if (!ClientManager::getInstance()->privateMessage(user, message.first, error_, message.second, echo)) {
			aRequest.setResponseErrorStr(error_);
			return websocketpp::http::status_code::internal_server_error;
		}

		return websocketpp::http::status_code::no_content;
	}

	void PrivateChatApi::on(MessageManagerListener::ChatRemoved, const PrivateChatPtr& aChat) noexcept {
		removeSubModule(aChat->getUser()->getCID());

		if (!subscriptionActive("private_chat_removed")) {
			return;
		}

		send("private_chat_removed", serializeChat(aChat));
	}

	void PrivateChatApi::addChat(const PrivateChatPtr& aChat) noexcept {
		addSubModule(aChat->getUser()->getCID(), std::make_shared<PrivateChatInfo>(this, aChat));
	}

	void PrivateChatApi::on(MessageManagerListener::ChatCreated, const PrivateChatPtr& aChat, bool aReceivedMessage) noexcept {
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
			{ "message_counts", Serializer::serializeCacheInfo(aChat->getCache(), Serializer::serializeUnreadChat) },
		};
	}
}