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

#include <web-server/stdinc.h>

#include <api/LogApi.h>
#include <api/common/Serializer.h>

#include <airdcpp/LogManager.h>

namespace webserver {
	LogApi::LogApi(Session* aSession) : ApiModule(aSession, Access::EVENTS) {
		LogManager::getInstance()->addListener(this);

		createSubscription("log_message");
		createSubscription("log_info");

		METHOD_HANDLER("clear", Access::EVENTS, ApiRequest::METHOD_POST, (), false, LogApi::handleClear);
		METHOD_HANDLER("read", Access::EVENTS, ApiRequest::METHOD_POST, (), false, LogApi::handleRead);
		METHOD_HANDLER("info", Access::EVENTS, ApiRequest::METHOD_GET, (), false, LogApi::handleGetInfo);
		METHOD_HANDLER("messages", Access::EVENTS, ApiRequest::METHOD_GET, (NUM_PARAM), false, LogApi::handleGetLog);
	}

	LogApi::~LogApi() {
		LogManager::getInstance()->removeListener(this);
	}

	api_return LogApi::handleRead(ApiRequest& aRequest) {
		LogManager::getInstance()->setRead();
		return websocketpp::http::status_code::ok;
	}

	api_return LogApi::handleClear(ApiRequest& aRequest) {
		LogManager::getInstance()->clearCache();
		return websocketpp::http::status_code::ok;
	}

	api_return LogApi::handleGetLog(ApiRequest& aRequest) {
		auto j = Serializer::serializeFromEnd(
			aRequest.getRangeParam(0),
			LogManager::getInstance()->getCache().getLogMessages(),
			Serializer::serializeLogMessage);

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return LogApi::handleGetInfo(ApiRequest& aRequest) {
		json j;
		Serializer::serializeCacheInfo(j, LogManager::getInstance()->getCache(), Serializer::serializeUnreadLog);
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	void LogApi::on(LogManagerListener::Message, const LogMessagePtr& aMessageData) noexcept {
		if (subscriptionActive("log_message")) {
			send("log_message", Serializer::serializeLogMessage(aMessageData));
		}

		onMessagesChanged();
	}

	void LogApi::onMessagesChanged() noexcept {
		if (!subscriptionActive("log_info")) {
			return;
		}

		json j;
		Serializer::serializeCacheInfo(j, LogManager::getInstance()->getCache(), Serializer::serializeUnreadLog);
		send("log_info", j);
	}

	void LogApi::on(LogManagerListener::Cleared) noexcept {
		onMessagesChanged();
	}

	void LogApi::on(LogManagerListener::MessagesRead) noexcept {
		onMessagesChanged();
	}
}