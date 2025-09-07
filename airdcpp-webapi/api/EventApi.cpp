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

#include <api/EventApi.h>
#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>
#include <api/common/MessageUtils.h>

#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/events/LogManager.h>

namespace webserver {
	EventApi::EventApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::EVENTS_VIEW) 
	{
		createSubscriptions({ "event_message", "event_counts" });

		METHOD_HANDLER(Access::EVENTS_VIEW, METHOD_POST,	(EXACT_PARAM("read")),		EventApi::handleRead);
		METHOD_HANDLER(Access::EVENTS_VIEW, METHOD_GET,		(EXACT_PARAM("counts")),	EventApi::handleGetInfo);

		METHOD_HANDLER(Access::EVENTS_VIEW, METHOD_GET,		(RANGE_MAX_PARAM),			EventApi::handleGetMessages);
		METHOD_HANDLER(Access::EVENTS_EDIT, METHOD_DELETE,	(),							EventApi::handleClearMessages);
		METHOD_HANDLER(Access::EVENTS_EDIT, METHOD_POST,	(),							EventApi::handlePostMessage);

		LogManager::getInstance()->addListener(this);
	}

	EventApi::~EventApi() {
		LogManager::getInstance()->removeListener(this);
	}

	api_return EventApi::handlePostMessage(ApiRequest& aRequest) {
		auto messageInput = Deserializer::deserializeStatusMessage(aRequest.getRequestBody());
		LogManager::getInstance()->message(messageInput.message, messageInput.severity, MessageUtils::parseStatusMessageLabel(aRequest.getSession()));
		return http_status::no_content;
	}

	api_return EventApi::handleRead(ApiRequest&) {
		LogManager::getInstance()->setRead();
		return http_status::no_content;
	}

	api_return EventApi::handleClearMessages(ApiRequest&) {
		LogManager::getInstance()->clearCache();
		return http_status::no_content;
	}

	api_return EventApi::handleGetMessages(ApiRequest& aRequest) {
		auto j = Serializer::serializeFromEnd(
			aRequest.getRangeParam(MAX_COUNT),
			LogManager::getInstance()->getCache().getLogMessages(),
			MessageUtils::serializeLogMessage
		);

		aRequest.setResponseBody(j);
		return http_status::ok;
	}

	api_return EventApi::handleGetInfo(ApiRequest& aRequest) {
		aRequest.setResponseBody(MessageUtils::serializeCacheInfo(LogManager::getInstance()->getCache(), MessageUtils::serializeUnreadLog));
		return http_status::ok;
	}

	void EventApi::on(LogManagerListener::Message, const LogMessagePtr& aMessageData) noexcept {
		// Avoid deadlocks if the event is fired from inside a lock
		addAsyncTask([this, aMessageData] {
			if (subscriptionActive("event_message")) {
				send("event_message", MessageUtils::serializeLogMessage(aMessageData));
			}

			onMessagesChanged();
		});
	}

	void EventApi::onMessagesChanged() noexcept {
		if (!subscriptionActive("event_counts")) {
			return;
		}

		send("event_counts", MessageUtils::serializeCacheInfo(LogManager::getInstance()->getCache(), MessageUtils::serializeUnreadLog));
	}

	void EventApi::on(LogManagerListener::Cleared) noexcept {
		onMessagesChanged();
	}

	void EventApi::on(LogManagerListener::MessagesRead) noexcept {
		onMessagesChanged();
	}
}