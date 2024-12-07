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

#ifndef DCPLUSPLUS_WEBSERVER_SUBSCRIBABLE_APIMODULE_H
#define DCPLUSPLUS_WEBSERVER_SUBSCRIBABLE_APIMODULE_H

#include "forward.h"

#include <web-server/SessionListener.h>

#include <api/base/ApiModule.h>

namespace webserver {
	class WebSocket;
#define LISTENER_PARAM_ID "listener_param"
	class SubscribableApiModule : public ApiModule, protected SessionListener {
	public:
		SubscribableApiModule(Session* aSession, Access aSubscriptionAccess);
		~SubscribableApiModule() override;

		using SubscriptionMap = std::map<const string, bool>;

		virtual void createSubscriptions(const StringList& aSubscriptions) noexcept;

		virtual bool send(const json& aJson);
		virtual bool send(const string& aSubscription, const json& aJson);

		using JsonCallback = std::function<json ()>;
		virtual bool maybeSend(const string& aSubscription, const JsonCallback& aCallback);

		virtual void setSubscriptionState(const string& aSubscription, bool aActive) noexcept {
			subscriptions[aSubscription] = aActive;
		}

		virtual bool subscriptionActive(const string& aSubscription) const noexcept {
			auto s = subscriptions.find(aSubscription);
			dcassert(s != subscriptions.end());
			return s->second;
		}

		virtual bool subscriptionExists(const string& aSubscription) const noexcept {
			auto i = subscriptions.find(aSubscription);
			return i != subscriptions.end();
		}

		Access getSubscriptionAccess() const noexcept {
			return subscriptionAccess;
		}

		const WebSocketPtr& getSocket() const noexcept {
			return socket;
		}
	protected:
		void createSubscription(const string& aSubscription) noexcept;

		void on(SessionListener::SocketConnected, const WebSocketPtr&) noexcept override;
		void on(SessionListener::SocketDisconnected) noexcept override;

		const Access subscriptionAccess;

		virtual api_return handleSubscribe(ApiRequest& aRequest);
		virtual api_return handleUnsubscribe(ApiRequest& aRequest);

		virtual const string& parseSubscription(ApiRequest& aRequest);
	private:
		WebSocketPtr socket = nullptr;
		SubscriptionMap subscriptions;
	};

	using HandlerPtr = std::unique_ptr<ApiModule>;
}

#endif