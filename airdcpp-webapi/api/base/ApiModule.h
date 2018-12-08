/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_APIMODULE_H
#define DCPLUSPLUS_DCPP_APIMODULE_H

#include <web-server/Access.h>
#include <web-server/ApiRequest.h>
#include <web-server/SessionListener.h>

namespace webserver {
	using boost::regex;

	class WebSocket;
	class ApiModule {
	public:
#define LISTENER_PARAM_ID "listener_param"
#define MAX_COUNT "max_count_param"
#define START_POS "start_pos_param"

#define TTH_REG regex(R"(^[0-9A-Z]{39}$)")
#define CID_REG TTH_REG
#define TOKEN_REG regex(R"(^\d+$)")

#define NUM_PARAM(id) (ApiModule::RequestHandler::Param(id, TOKEN_REG))
#define TOKEN_PARAM NUM_PARAM(TOKEN_PARAM_ID)
#define RANGE_START_PARAM NUM_PARAM(START_POS)
#define RANGE_MAX_PARAM NUM_PARAM(MAX_COUNT)

#define TTH_PARAM (ApiModule::RequestHandler::Param(TTH_PARAM_ID, TTH_REG))
#define CID_PARAM (ApiModule::RequestHandler::Param(CID_PARAM_ID, CID_REG))

#define STR_PARAM(id) (ApiModule::RequestHandler::Param(id, regex(R"(^\w+$)")))
#define EXACT_PARAM(pattern) (ApiModule::RequestHandler::Param(pattern, regex("^" + string(pattern) + "$")))

#define BRACED_INIT_LIST(...) {__VA_ARGS__}
#define MODULE_METHOD_HANDLER(module, access, method, params, func) (module->getRequestHandlers().push_back(ApiModule::RequestHandler(access, method, BRACED_INIT_LIST params, std::bind(&func, this, placeholders::_1))))
#define METHOD_HANDLER(access, method, params, func) MODULE_METHOD_HANDLER(this, access, method, params, func)

		ApiModule(Session* aSession);
		virtual ~ApiModule();

		struct RequestHandler {
			struct Param {
				Param(string aParamId, regex&& aReg) : id(std::move(aParamId)), reg(std::move(aReg)) { }

				string id;
				regex reg;
			};

			typedef vector<Param> ParamList;

			typedef std::function<api_return(ApiRequest& aRequest)> HandlerFunction;

			// Regular handler
			RequestHandler(Access aAccess, RequestMethod aMethod, ParamList&& aParams, HandlerFunction aFunction) :
				method(aMethod), params(std::move(aParams)), f(aFunction), access(aAccess) {
			
			}

			const RequestMethod method;
			const ParamList params;
			const HandlerFunction f;
			const Access access;

			optional<ApiRequest::NamedParamMap> matchParams(const ApiRequest::PathTokenList& aPathTokens) const noexcept;
		};

		typedef std::vector<RequestHandler> RequestHandlerList;

		api_return handleRequest(ApiRequest& aRequest);

		ApiModule(ApiModule&) = delete;
		ApiModule& operator=(ApiModule&) = delete;

		virtual void addAsyncTask(CallBack&& aTask);
		virtual TimerPtr getTimer(CallBack&& aTask, time_t aIntervalMillis);

		Session* getSession() const noexcept {
			return session;
		}

		RequestHandlerList& getRequestHandlers() noexcept {
			return requestHandlers;
		}

		// All custom async tasks should be run inside this to
		// ensure that the session won't get deleted
		virtual CallBack getAsyncWrapper(CallBack&& aTask) noexcept;
	protected:
		static void asyncRunWrapper(const CallBack& aTask, LocalSessionId aSessionId);

		Session* session;

		RequestHandlerList requestHandlers;
	};

	
	class SubscribableApiModule : public ApiModule, protected SessionListener {
	public:
		SubscribableApiModule(Session* aSession, Access aSubscriptionAccess, const StringList& aSubscriptions);
		virtual ~SubscribableApiModule();

		typedef std::map<const string, bool> SubscriptionMap;

		virtual bool send(const json& aJson);
		virtual bool send(const string& aSubscription, const json& aJson);

		typedef std::function<json()> JsonCallback;
		virtual bool maybeSend(const string& aSubscription, JsonCallback aCallback);

		// All custom async tasks should be run inside this to
		// ensure that the session won't get deleted

		virtual void setSubscriptionState(const string& aSubscription, bool active) noexcept {
			subscriptions[aSubscription] = active;
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

		virtual void createSubscription(const string& aSubscription) noexcept {
			dcassert(subscriptions.find(aSubscription) == subscriptions.end());
			subscriptions[aSubscription];
		}

		Access getSubscriptionAccess() const noexcept {
			return subscriptionAccess;
		}

		const WebSocketPtr& getSocket() const noexcept {
			return socket;
		}
	protected:
		virtual void on(SessionListener::SocketConnected, const WebSocketPtr&) noexcept override;
		virtual void on(SessionListener::SocketDisconnected) noexcept override;

		const Access subscriptionAccess;

		virtual api_return handleSubscribe(ApiRequest& aRequest);
		virtual api_return handleUnsubscribe(ApiRequest& aRequest);
	private:
		WebSocketPtr socket = nullptr;
		SubscriptionMap subscriptions;
	};

	typedef std::unique_ptr<ApiModule> HandlerPtr;
}

#endif