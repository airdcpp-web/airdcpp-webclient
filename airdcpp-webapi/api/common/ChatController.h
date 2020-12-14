/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_MESSAGECACHE_MODULE_H
#define DCPLUSPLUS_DCPP_MESSAGECACHE_MODULE_H

#include <web-server/JsonUtil.h>

#include <api/base/ApiModule.h>
#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>
#include <api/common/MessageUtils.h>

#include <airdcpp/StringTokenizer.h>

namespace webserver {
	class ChatController {
	public:
		ChatController(SubscribableApiModule* aModule, ChatHandlerBase* aChat, const string& aSubscriptionId, Access aViewPermission, Access aEditPermission, Access aSendPermission) :
			module(aModule), subscriptionId(aSubscriptionId), chat(aChat)
		{
			MODULE_METHOD_HANDLER(aModule, aSendPermission, METHOD_POST, (EXACT_PARAM("chat_message")), ChatController::handlePostChatMessage);
			MODULE_METHOD_HANDLER(aModule, aEditPermission, METHOD_POST, (EXACT_PARAM("status_message")), ChatController::handlePostStatusMessage);

			MODULE_METHOD_HANDLER(aModule, aViewPermission, METHOD_GET, (EXACT_PARAM("messages"), RANGE_MAX_PARAM), ChatController::handleGetMessages);
			MODULE_METHOD_HANDLER(aModule, aViewPermission, METHOD_POST, (EXACT_PARAM("messages"), EXACT_PARAM("read")), ChatController::handleSetRead);
			MODULE_METHOD_HANDLER(aModule, aEditPermission, METHOD_DELETE, (EXACT_PARAM("messages")), ChatController::handleClear);

			MODULE_METHOD_HANDLER(aModule, aEditPermission, METHOD_GET, (EXACT_PARAM("messages"), EXACT_PARAM("highlights"), TOKEN_PARAM), ChatController::handleGetMessageHighlight);
		}

		void onChatMessage(const ChatMessagePtr& aMessage) noexcept {
			onMessagesUpdated();

			auto s = toListenerName("message");
			if (!module->subscriptionActive(s)) {
				return;
			}

			module->send(s, MessageUtils::serializeChatMessage(aMessage));
		}

		void onStatusMessage(const LogMessagePtr& aMessage) noexcept {
			onMessagesUpdated();

			auto s = toListenerName("status");
			if (!module->subscriptionActive(s)) {
				return;
			}

			module->send(s, MessageUtils::serializeLogMessage(aMessage));
		}

		void onMessagesUpdated() {
			sendUnread();
		}

		void onChatCommand(const OutgoingChatMessage& aMessage) {
			auto s = toListenerName("text_command");
			if (!module->subscriptionActive(s)) {
				return;
			}

			auto tokens = CommandTokenizer<std::string, std::deque>(aMessage.text).getTokens();
			if (tokens.empty()) {
				return;
			}

			auto command = tokens.front();
			if (command.length() == 1) {
				return;
			}

			tokens.pop_front();

			module->send(s, {
				{ "command", command.substr(1) },
				{ "args", tokens },
				{ "permissions",  Serializer::serializePermissions(parseMessageAuthorAccess(aMessage)) },
			});
		}

		void setChat(ChatHandlerBase* aChat) noexcept {
			chat = aChat;
		}
	private:
		void sendUnread() noexcept {
			auto s = toListenerName("updated");
			if (!module->subscriptionActive(s)) {
				return;
			}

			module->send(s, {
				{ "message_counts",  MessageUtils::serializeCacheInfo(chat->getCache(), MessageUtils::serializeUnreadChat) },
			});
		}

		AccessList parseMessageAuthorAccess(const OutgoingChatMessage& aMessage) {
			const auto sessions = module->getSession()->getServer()->getUserManager().getSessions();
			const auto ownerSessionIter = std::find_if(sessions.begin(), sessions.end(), [&aMessage](const SessionPtr& aSession) {
				return aSession.get() == aMessage.owner;
			});

			AccessList permissions;
			if (ownerSessionIter != sessions.end()) {
				permissions = (*ownerSessionIter)->getUser()->getPermissions();
			} else {
				// GUI/extension etc
				permissions.push_back(Access::ADMIN);
			}

			return permissions;
		}

		api_return handlePostChatMessage(ApiRequest& aRequest) {
			const auto& reqJson = aRequest.getRequestBody();

			module->addAsyncTask([
				this,
				message = Deserializer::deserializeChatMessage(reqJson),
				complete = aRequest.defer(),
				callerPtr = aRequest.getOwnerPtr()
			] {
				string error;
				if (!chat->sendMessageHooked(OutgoingChatMessage(message.first, callerPtr, message.second), error) && !error.empty()) {
					complete(websocketpp::http::status_code::internal_server_error, nullptr, ApiRequest::toResponseErrorStr(error));
				} else {
					complete(websocketpp::http::status_code::no_content, nullptr, nullptr);
				}
			});

			return CODE_DEFERRED;
		}

		api_return handlePostStatusMessage(ApiRequest& aRequest) {
			const auto& reqJson = aRequest.getRequestBody();

			auto message = Deserializer::deserializeStatusMessage(reqJson);
			chat->statusMessage(message.first, message.second, MessageUtils::parseStatusMessageLabel(aRequest.getSession()));
			return websocketpp::http::status_code::no_content;
		}

		api_return handleClear(ApiRequest&) {
			chat->clearCache();
			return websocketpp::http::status_code::no_content;
		}

		api_return handleSetRead(ApiRequest&) {
			chat->setRead();
			return websocketpp::http::status_code::no_content;
		}

		api_return handleGetMessageHighlight(ApiRequest& aRequest) {
			const auto id = aRequest.getTokenParam();
			auto highlight = chat->getCache().findMessageHighlight(id);
			if (!highlight) {
				aRequest.setResponseErrorStr("Message highlight " + Util::toString(id) + " was not found");
				return websocketpp::http::status_code::not_found;
			}

			aRequest.setResponseBody(MessageUtils::serializeMessageHighlight(highlight));
			return websocketpp::http::status_code::ok;
		}

		api_return handleGetMessages(ApiRequest& aRequest) {
			auto j = Serializer::serializeFromEnd(
				aRequest.getRangeParam(MAX_COUNT),
				chat->getCache().getMessages(),
				MessageUtils::serializeMessage
			);

			aRequest.setResponseBody(j);
			return websocketpp::http::status_code::ok;
		}

		string toListenerName(const string& aSubscription) {
			return subscriptionId + "_" + aSubscription;
		}

		ChatHandlerBase* chat;
		string subscriptionId;
		SubscribableApiModule* module;
	};
}

#endif
