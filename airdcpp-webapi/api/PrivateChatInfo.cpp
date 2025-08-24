/*
* Copyright (C) 2011-2024 AirDC++ Project
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
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

#include <api/PrivateChatInfo.h>
#include <api/base/ApiModule.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/hub/Client.h>
#include <airdcpp/private_chat/PrivateChat.h>

namespace webserver {
	StringList PrivateChatInfo::subscriptionList = {
		"private_chat_updated",
		"private_chat_message",
		"private_chat_status",
		"private_chat_text_command"
	};

	PrivateChatInfo::PrivateChatInfo(ParentType* aParentModule, const PrivateChatPtr& aChat) :
		SubApiModule(aParentModule, aChat->getUser()->getCID().toBase32()), chat(aChat),
		chatHandler(this, aChat.get(), "private_chat", Access::PRIVATE_CHAT_VIEW, Access::PRIVATE_CHAT_EDIT, Access::PRIVATE_CHAT_SEND) {

		createSubscriptions(subscriptionList);

		METHOD_HANDLER(Access::PRIVATE_CHAT_VIEW, METHOD_PATCH,		(),							PrivateChatInfo::handleUpdateSession);

		METHOD_HANDLER(Access::PRIVATE_CHAT_EDIT, METHOD_POST,		(EXACT_PARAM("ccpm")),		PrivateChatInfo::handleConnectCCPM);
		METHOD_HANDLER(Access::PRIVATE_CHAT_EDIT, METHOD_DELETE,	(EXACT_PARAM("ccpm")),		PrivateChatInfo::handleDisconnectCCPM);

		METHOD_HANDLER(Access::PRIVATE_CHAT_SEND, METHOD_POST,		(EXACT_PARAM("typing")),	PrivateChatInfo::handleStartTyping);
		METHOD_HANDLER(Access::PRIVATE_CHAT_SEND, METHOD_DELETE,	(EXACT_PARAM("typing")),	PrivateChatInfo::handleEndTyping);
	}

	void PrivateChatInfo::init() noexcept {
		chat->addListener(this);
	}

	CID PrivateChatInfo::getId() const noexcept {
		return chat->getUser()->getCID();
	}

	PrivateChatInfo::~PrivateChatInfo() {
		chat->removeListener(this);
	}

	api_return PrivateChatInfo::handleUpdateSession(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto client = Deserializer::deserializeClient(reqJson, true);
		if (client) {
			chat->setHubUrl(client->getHubUrl());
		}

		return http_status::no_content;
	}

	api_return PrivateChatInfo::handleStartTyping(ApiRequest&) {
		chat->setTypingState(true);
		return http_status::no_content;
	}

	api_return PrivateChatInfo::handleEndTyping(ApiRequest&) {
		chat->setTypingState(false);
		return http_status::no_content;
	}

	api_return PrivateChatInfo::handleDisconnectCCPM(ApiRequest&) {
		chat->closeCC(false, true);
		return http_status::no_content;
	}

	api_return PrivateChatInfo::handleConnectCCPM(ApiRequest&) {
		chat->startCC();
		return http_status::no_content;
	}

	string PrivateChatInfo::formatCCPMState(PrivateChat::CCPMState aState) noexcept {
		switch (aState) {
			case PrivateChat::CCPMState::DISCONNECTED: return "disconnected";
			case PrivateChat::CCPMState::CONNECTING: return "connecting";
			case PrivateChat::CCPMState::CONNECTED: return "connected";
		}

		dcassert(0);
		return Util::emptyString;
	}

	json PrivateChatInfo::serializeCCPMState(const PrivateChatPtr& aChat) noexcept {
		json encryption;
		if (aChat->getUc()) {
			encryption = Serializer::serializeEncryption(aChat->getUc()->getEncryptionInfo(), aChat->getUc()->isTrusted());
		}

		return{
			{ "id", formatCCPMState(aChat->getCCPMState()) },
			{ "str", PrivateChat::ccpmStateToString(aChat->getCCPMState()) },
			{ "encryption", encryption },
		};
	}

	void PrivateChatInfo::on(PrivateChatListener::Close, PrivateChat*) noexcept {

	}

	void PrivateChatInfo::on(PrivateChatListener::UserUpdated, PrivateChat*) noexcept {
		onSessionUpdated({
			{ "user", Serializer::serializeHintedUser(chat->getHintedUser()) },
			{ "ccpm_state", serializeCCPMState(chat) }
		});
	}

	void PrivateChatInfo::on(PrivateChatListener::PMStatus, PrivateChat*, uint8_t /*aSeverity*/) noexcept {

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