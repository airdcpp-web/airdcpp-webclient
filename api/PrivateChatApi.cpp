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

#include <api/PrivateChatApi.h>

#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <airdcpp/ScopedFunctor.h>

namespace webserver {
	StringList PrivateChatApi::subscriptionList = {
		"chat_session_created",
		"chat_session_removed"
	};

	PrivateChatApi::PrivateChatApi(Session* aSession) : ParentApiModule("session", CID_PARAM, aSession, subscriptionList, PrivateChatInfo::subscriptionList, [](const string& aId) { return Deserializer::deserializeCID(aId); }) {

		MessageManager::getInstance()->addListener(this);

		METHOD_HANDLER("sessions", ApiRequest::METHOD_GET, (), false, PrivateChatApi::handleGetThreads);

		METHOD_HANDLER("session", ApiRequest::METHOD_DELETE, (CID_PARAM), false, PrivateChatApi::handleDeleteChat);
		METHOD_HANDLER("session", ApiRequest::METHOD_POST, (), true, PrivateChatApi::handlePostChat);

		auto rawChats = MessageManager::getInstance()->getChats();
		for (const auto& c : rawChats | map_values) {
			addChat(c);
		}
	}

	PrivateChatApi::~PrivateChatApi() {
		MessageManager::getInstance()->removeListener(this);
	}

	api_return PrivateChatApi::handlePostChat(ApiRequest& aRequest) {
		auto c = MessageManager::getInstance()->addChat(Deserializer::deserializeHintedUser(aRequest.getRequestBody()), false);
		if (!c) {
			aRequest.setResponseErrorStr("Chat session exists");
			return websocketpp::http::status_code::bad_request;
		}

		aRequest.setResponseBody({
			{ "id", c->getUser()->getCID().toBase32() }
		});

		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatApi::handleDeleteChat(ApiRequest& aRequest) {
		auto chat = getSubModule(aRequest.getStringParam(0));
		if (!chat) {
			aRequest.setResponseErrorStr("Chat session not found");
			return websocketpp::http::status_code::not_found;
		}

		MessageManager::getInstance()->removeChat(chat->getChat()->getUser());
		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatApi::handleGetThreads(ApiRequest& aRequest) {
		json retJson;

		{
			RLock l(cs);
			if (!subModules.empty()) {
				for (const auto& c : subModules | map_values) {
					retJson.push_back(serializeChat(c->getChat()));
				}
			} else {
				retJson = json::array();
			}
		}

		aRequest.setResponseBody(retJson);
		return websocketpp::http::status_code::ok;
	}

	void PrivateChatApi::on(MessageManagerListener::ChatRemoved, const PrivateChatPtr& aChat) noexcept {
		{
			WLock l(cs);
			subModules.erase(aChat->getUser()->getCID());
		}

		if (!subscriptionActive("chat_session_removed")) {
			return;
		}

		send("chat_session_removed", {
			{ "id", aChat->getUser()->getCID().toBase32() }
		});
	}

	void PrivateChatApi::addChat(const PrivateChatPtr& aChat) noexcept {
		auto chatInfo = make_shared<PrivateChatInfo>(this, aChat);

		{
			WLock l(cs);
			subModules.emplace(aChat->getUser()->getCID(), move(chatInfo));
		}
	}

	void PrivateChatApi::on(MessageManagerListener::ChatCreated, const PrivateChatPtr& aChat, bool aReceivedMessage) noexcept {
		addChat(aChat);
		if (!subscriptionActive("chat_session_created")) {
			return;
		}

		send("chat_session_created", serializeChat(aChat));
	}

	json PrivateChatApi::serializeChat(const PrivateChatPtr& aChat) noexcept {
		return {
			{ "id", aChat->getUser()->getCID().toBase32() },
			{ "user", Serializer::serializeHintedUser(aChat->getHintedUser()) },
			{ "ccpm_state", PrivateChatInfo::serializeCCPMState(aChat->getCCPMState()) },
			{ "unread_messages", Serializer::serializeUnread(aChat->getCache()) }
		};
	}
}