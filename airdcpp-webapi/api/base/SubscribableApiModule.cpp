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

#include <web-server/SocketManager.h>
#include <web-server/WebSocket.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <api/base/SubscribableApiModule.h>

namespace webserver {
	SubscribableApiModule::SubscribableApiModule(Session* aSession, Access aSubscriptionAccess) : ApiModule(aSession), subscriptionAccess(aSubscriptionAccess) {
		socket = aSession->getServer()->getSocketManager().getSocket(aSession->getId());

		aSession->addListener(this);

		METHOD_HANDLER(aSubscriptionAccess, METHOD_POST, (EXACT_PARAM("listeners"), STR_PARAM(LISTENER_PARAM_ID)), SubscribableApiModule::handleSubscribe);
		METHOD_HANDLER(aSubscriptionAccess, METHOD_DELETE, (EXACT_PARAM("listeners"), STR_PARAM(LISTENER_PARAM_ID)), SubscribableApiModule::handleUnsubscribe);
	}

	SubscribableApiModule::~SubscribableApiModule() {
		session->removeListener(this);
		socket = nullptr;
	}

	void SubscribableApiModule::createSubscriptions(const StringList& aSubscriptions) noexcept {
		for (const auto& s : aSubscriptions) {
			createSubscription(s);
		}
	}

	void SubscribableApiModule::createSubscription(const string& aSubscription) noexcept {
		dcassert(subscriptions.find(aSubscription) == subscriptions.end());
		subscriptions.emplace(aSubscription, false);
	}

	void SubscribableApiModule::on(SessionListener::SocketConnected, const WebSocketPtr& aSocket) noexcept {
		socket = aSocket;
	}

	void SubscribableApiModule::on(SessionListener::SocketDisconnected) noexcept {
		// Disable all subscriptions
		for (auto& [_, enabled] : subscriptions) {
			enabled = false;
		}

		socket = nullptr;
	}

	const string& SubscribableApiModule::parseSubscription(ApiRequest& aRequest) {
		if (!socket) {
			throw RequestException(http_status::precondition_required, "Socket required");
		}

		const auto& subscription = aRequest.getStringParam(LISTENER_PARAM_ID);
		if (!subscriptionExists(subscription)) {
			throw RequestException(http_status::not_found, "No such subscription: " + subscription);
		}

		return subscription;
	}

	api_return SubscribableApiModule::handleSubscribe(ApiRequest& aRequest) {
		const auto& subscription = parseSubscription(aRequest);
		setSubscriptionState(subscription, true);
		return http_status::no_content;
	}

	api_return SubscribableApiModule::handleUnsubscribe(ApiRequest& aRequest) {
		const auto& subscription = parseSubscription(aRequest);
		setSubscriptionState(subscription, false);
		return http_status::no_content;
	}

	bool SubscribableApiModule::send(const json& aJson) {
		// Ensure that the socket won't be deleted while sending the message...
		auto s = socket;
		if (!s) {
			return false;
		}

		try {
			s->sendPlain(aJson);
		} catch (const json::exception&) {
			// Ignore JSON errors...
			return false;
		}

		return true;
	}

	bool SubscribableApiModule::send(const string& aSubscription, const json& aData) {
		return send({
			{ "event", aSubscription },
			{ "data", aData },
		});
	}

	bool SubscribableApiModule::maybeSend(const string& aSubscription, const JsonCallback& aCallback) {
		if (!subscriptionActive(aSubscription)) {
			return false;
		}

		return send(aSubscription, aCallback());
	}
}