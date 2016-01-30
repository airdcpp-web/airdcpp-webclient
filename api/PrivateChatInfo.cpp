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

#include <api/PrivateChatInfo.h>
#include <api/ApiModule.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/PrivateChat.h>

namespace webserver {
	StringList PrivateChatInfo::subscriptionList = {
		"private_chat_updated",
		"private_chat_message",
		"private_chat_status"
	};

	PrivateChatInfo::PrivateChatInfo(ParentType* aParentModule, const PrivateChatPtr& aChat) :
		SubApiModule(aParentModule, aChat->getUser()->getCID().toBase32(), subscriptionList), chat(aChat),
		chatHandler(this, aChat, "private_chat") {

		METHOD_HANDLER("ccpm", Access::PRIVATE_CHAT_EDIT, ApiRequest::METHOD_POST, (), false, PrivateChatInfo::handleConnectCCPM);
		METHOD_HANDLER("ccpm", Access::PRIVATE_CHAT_EDIT, ApiRequest::METHOD_DELETE, (), false, PrivateChatInfo::handleDisconnectCCPM);

		METHOD_HANDLER("typing", Access::PRIVATE_CHAT_SEND, ApiRequest::METHOD_POST, (), false, PrivateChatInfo::handleStartTyping);
		METHOD_HANDLER("typing", Access::PRIVATE_CHAT_SEND, ApiRequest::METHOD_DELETE, (), false, PrivateChatInfo::handleEndTyping);
	}

	void PrivateChatInfo::init() noexcept {
		chat->addListener(this);
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

	string PrivateChatInfo::formatCCPMState(PrivateChat::CCPMState aState) noexcept {
		switch (aState) {
			case PrivateChat::DISCONNECTED: return "disconnected";
			case PrivateChat::CONNECTING: return "connecting";
			case PrivateChat::CONNECTED: return "connected";
		}

		dcassert(0);
		return Util::emptyString;
	}

	json PrivateChatInfo::serializeCCPMState(const PrivateChatPtr& aChat) noexcept {
		return{
			{ "id", formatCCPMState(aChat->getCCPMState()) },
			{ "str", PrivateChat::ccpmStateToString(aChat->getCCPMState()) },
			{ "supported", aChat->getSupportsCCPM() },
			{ "encryption", aChat->getUc() ? aChat->getUc()->getEncryptionInfo() : Util::emptyString },
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
			{ "ccpm_state", serializeCCPMState(chat) }
		});
	}

	void PrivateChatInfo::onSessionUpdated(const json& aData) noexcept {
		if (!subscriptionActive("private_chat_updated")) {
			return;
		}

		send("private_chat_updated", aData);
	}
}