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

#include <web-server/stdinc.h>

#include <api/EventApi.h>
#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <airdcpp/LogManager.h>

namespace webserver {
	EventApi::EventApi(Session* aSession) : SubscribableApiModule(aSession, Access::EVENTS_VIEW) {
		LogManager::getInstance()->addListener(this);

		createSubscription("event_message");
		createSubscription("event_counts");

		METHOD_HANDLER("read", Access::EVENTS_VIEW, ApiRequest::METHOD_POST, (), false, EventApi::handleRead);
		METHOD_HANDLER("counts", Access::EVENTS_VIEW, ApiRequest::METHOD_GET, (), false, EventApi::handleGetInfo);
		METHOD_HANDLER("messages", Access::EVENTS_VIEW, ApiRequest::METHOD_GET, (NUM_PARAM), false, EventApi::handleGetLog);

		METHOD_HANDLER("clear", Access::EVENTS_EDIT, ApiRequest::METHOD_POST, (), false, EventApi::handleClear);
		METHOD_HANDLER("message", Access::EVENTS_EDIT, ApiRequest::METHOD_POST, (), true, EventApi::handlePostMessage);
	}

	EventApi::~EventApi() {
		LogManager::getInstance()->removeListener(this);
	}

	api_return EventApi::handlePostMessage(ApiRequest& aRequest) {
		auto message = Deserializer::deserializeStatusMessage(aRequest.getRequestBody());
		LogManager::getInstance()->message(message.first, message.second);
		return websocketpp::http::status_code::ok;
	}

	api_return EventApi::handleRead(ApiRequest& aRequest) {
		LogManager::getInstance()->setRead();
		return websocketpp::http::status_code::ok;
	}

	api_return EventApi::handleClear(ApiRequest& aRequest) {
		LogManager::getInstance()->clearCache();
		return websocketpp::http::status_code::ok;
	}

	api_return EventApi::handleGetLog(ApiRequest& aRequest) {
		auto j = Serializer::serializeFromEnd(
			aRequest.getRangeParam(0),
			LogManager::getInstance()->getCache().getLogMessages(),
			Serializer::serializeLogMessage);

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return EventApi::handleGetInfo(ApiRequest& aRequest) {
		json j;
		Serializer::serializeCacheInfo(j, LogManager::getInstance()->getCache(), Serializer::serializeUnreadLog);
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	void EventApi::on(LogManagerListener::Message, const LogMessagePtr& aMessageData) noexcept {
		if (subscriptionActive("event_message")) {
			send("event_message", Serializer::serializeLogMessage(aMessageData));
		}

		onMessagesChanged();
	}

	void EventApi::onMessagesChanged() noexcept {
		if (!subscriptionActive("event_counts")) {
			return;
		}

		json j;
		Serializer::serializeCacheInfo(j, LogManager::getInstance()->getCache(), Serializer::serializeUnreadLog);
		send("event_counts", j);
	}

	void EventApi::on(LogManagerListener::Cleared) noexcept {
		onMessagesChanged();
	}

	void EventApi::on(LogManagerListener::MessagesRead) noexcept {
		onMessagesChanged();
	}
}