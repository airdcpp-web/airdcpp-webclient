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

#include <api/PrivateChatInfo.h>
#include <api/ApiModule.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/PrivateChat.h>

namespace webserver {
	StringList PrivateChatInfo::subscriptionList = {
		"chat_session_updated",
		"private_chat_message",
		"private_chat_status"
	};

	PrivateChatInfo::PrivateChatInfo(ParentType* aParentModule, const PrivateChatPtr& aChat) :
		SubApiModule(aParentModule, aChat->getUser()->getCID().toBase32(), subscriptionList), chat(aChat) {

		chat->addListener(this);

		METHOD_HANDLER("messages", ApiRequest::METHOD_GET, (NUM_PARAM), false, PrivateChatInfo::handleGetMessages);
		METHOD_HANDLER("message", ApiRequest::METHOD_POST, (), true, PrivateChatInfo::handlePostMessage);

		METHOD_HANDLER("ccpm", ApiRequest::METHOD_POST, (), false, PrivateChatInfo::handlePostMessage);
		METHOD_HANDLER("ccpm", ApiRequest::METHOD_DELETE, (), false, PrivateChatInfo::handlePostMessage);

		METHOD_HANDLER("typing", ApiRequest::METHOD_POST, (), false, PrivateChatInfo::handlePostMessage);
		METHOD_HANDLER("typing", ApiRequest::METHOD_DELETE, (), false, PrivateChatInfo::handlePostMessage);

		METHOD_HANDLER("read", ApiRequest::METHOD_POST, (), false, PrivateChatInfo::handleSetRead);
	}

	PrivateChatInfo::~PrivateChatInfo() {
		chat->removeListener(this);
	}

	api_return PrivateChatInfo::handleStartTyping(ApiRequest& aRequest) {
		chat->sendPMInfo(PrivateChat::TYPING_ON);
		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatInfo::handleEndTyping(ApiRequest& aRequest) {
		chat->sendPMInfo(PrivateChat::TYPING_OFF);
		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatInfo::handleDisconnectCCPM(ApiRequest& aRequest) {
		chat->closeCC(false, true);
		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatInfo::handleConnectCCPM(ApiRequest& aRequest) {
		chat->startCC();
		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatInfo::handleSetRead(ApiRequest& aRequest) {
		chat->setRead();
		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatInfo::handleGetMessages(ApiRequest& aRequest) {
		auto j = Serializer::serializeFromEnd(
			aRequest.getRangeParam(0),
			chat->getCache().getMessages(),
			Serializer::serializeMessage);

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return PrivateChatInfo::handlePostMessage(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto message = JsonUtil::getField<string>("message", reqJson, false);
		auto thirdPerson = JsonUtil::getOptionalField<bool>("third_person", reqJson);

		string error;
		if (!chat->sendPrivateMessage(message, error, thirdPerson ? *thirdPerson : false)) {
			aRequest.setResponseErrorStr(error);
			return websocketpp::http::status_code::internal_server_error;
		}

		return websocketpp::http::status_code::ok;
	}

	void PrivateChatInfo::on(PrivateChatListener::PrivateMessage, PrivateChat* aChat, const ChatMessagePtr& aMessage) noexcept {
		if (!aMessage->getRead()) {
			sendUnread();
		}

		if (!subscriptionActive("private_chat_message")) {
			return;
		}

		send("private_chat_message", Serializer::serializeChatMessage(aMessage));
	}

	void PrivateChatInfo::on(PrivateChatListener::StatusMessage, PrivateChat*, const LogMessagePtr& aMessage) noexcept {
		if (!subscriptionActive("private_chat_status")) {
			return;
		}

		send("private_chat_status", Serializer::serializeLogMessage(aMessage));
	}

	json PrivateChatInfo::serializeCCPMState(uint8_t aState) noexcept {
		return{
			{ "id", aState },
			{ "str", PrivateChat::ccpmStateToString(aState) }
		};
	}

	void PrivateChatInfo::on(PrivateChatListener::Close, PrivateChat*) noexcept {

	}

	void PrivateChatInfo::on(PrivateChatListener::UserUpdated, PrivateChat*) noexcept {
		onSessionUpdated({
			{ "user", Serializer::serializeHintedUser(chat->getHintedUser()) }
		});
	}

	void PrivateChatInfo::on(PrivateChatListener::PMStatus, PrivateChat*, uint8_t aSeverity) noexcept {

	}

	void PrivateChatInfo::on(PrivateChatListener::CCPMStatusUpdated, PrivateChat*) noexcept {
		onSessionUpdated({
			{ serializeCCPMState(chat->getCCPMState()) }
		});
	}

	void PrivateChatInfo::on(PrivateChatListener::MessagesRead, PrivateChat*) noexcept {
		sendUnread();
	}

	void PrivateChatInfo::sendUnread() noexcept {
		onSessionUpdated({
			{ "unread_messages", Serializer::serializeUnread(chat->getCache()) }
		});
	}

	void PrivateChatInfo::onSessionUpdated(const json& aData) noexcept {
		if (!subscriptionActive("chat_session_updated")) {
			return;
		}

		send("chat_session_updated", aData);
	}
}