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

#ifndef DCPLUSPLUS_DCPP_MESSAGECACHE_MODULE_H
#define DCPLUSPLUS_DCPP_MESSAGECACHE_MODULE_H

#include <web-server/stdinc.h>
#include <web-server/JsonUtil.h>

#include <api/ApiModule.h>
#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

namespace webserver {
	template<class T>
	class ChatController {
	public:
		typedef typename std::function<const T&()> ChatGetterF;
		ChatController(SubscribableApiModule* aModule, const ChatGetterF& aChatF, const string& aSubscriptionId, Access aViewPermission, Access aEditPermission, Access aSendPermission) :
			module(aModule), subscriptionId(aSubscriptionId), chatF(aChatF)
		{
			auto& requestHandlers = aModule->getRequestHandlers();

			METHOD_HANDLER(aSendPermission, METHOD_POST, (EXACT_PARAM("chat_message")), ChatController::handlePostChatMessage);
			METHOD_HANDLER(aEditPermission, METHOD_POST, (EXACT_PARAM("status_message")), ChatController::handlePostStatusMessage);

			METHOD_HANDLER(aViewPermission, METHOD_GET, (EXACT_PARAM("messages"), RANGE_MAX_PARAM), ChatController::handleGetMessages);
			METHOD_HANDLER(aViewPermission, METHOD_POST, (EXACT_PARAM("messages"), EXACT_PARAM("read")), ChatController::handleSetRead);
			METHOD_HANDLER(aEditPermission, METHOD_DELETE, (EXACT_PARAM("messages")), ChatController::handleClear);
		}

		void onChatMessage(const ChatMessagePtr& aMessage) noexcept {
			onMessagesUpdated();

			auto s = toListenerName("message");
			if (!module->subscriptionActive(s)) {
				return;
			}

			module->send(s, Serializer::serializeChatMessage(aMessage));
		}

		void onStatusMessage(const LogMessagePtr& aMessage) noexcept {
			onMessagesUpdated();

			auto s = toListenerName("status");
			if (!module->subscriptionActive(s)) {
				return;
			}

			module->send(s, Serializer::serializeLogMessage(aMessage));
		}

		void onMessagesUpdated() {
			sendUnread();
		}
	private:
		void sendUnread() noexcept {
			auto s = toListenerName("updated");
			if (!module->subscriptionActive(s)) {
				return;
			}

			module->send(s, {
				{ "message_counts",  Serializer::serializeCacheInfo(chatF()->getCache(), Serializer::serializeUnreadChat) },
			});
		}

		api_return handlePostChatMessage(ApiRequest& aRequest) {
			const auto& reqJson = aRequest.getRequestBody();
			auto message = Deserializer::deserializeChatMessage(reqJson);

			string error_;
			if (!chatF()->sendMessage(message.first, error_, message.second)) {
				aRequest.setResponseErrorStr(error_);
				return websocketpp::http::status_code::internal_server_error;
			}

			return websocketpp::http::status_code::no_content;
		}

		api_return handlePostStatusMessage(ApiRequest& aRequest) {
			const auto& reqJson = aRequest.getRequestBody();

			auto message = Deserializer::deserializeStatusMessage(reqJson);
			chatF()->statusMessage(message.first, message.second);
			return websocketpp::http::status_code::no_content;
		}

		api_return handleClear(ApiRequest& aRequest) {
			chatF()->clearCache();
			return websocketpp::http::status_code::no_content;
		}

		api_return handleSetRead(ApiRequest& aRequest) {
			chatF()->setRead();
			return websocketpp::http::status_code::no_content;
		}

		api_return handleGetMessages(ApiRequest& aRequest) {
			auto j = Serializer::serializeFromEnd(
				aRequest.getRangeParam(MAX_COUNT),
				chatF()->getCache().getMessages(),
				Serializer::serializeMessage);

			aRequest.setResponseBody(j);
			return websocketpp::http::status_code::ok;
		}

		string toListenerName(const string& aSubscription) {
			return subscriptionId + "_" + aSubscription;
		}

		ChatGetterF chatF;
		string subscriptionId;
		SubscribableApiModule* module;
	};
}

#endif
