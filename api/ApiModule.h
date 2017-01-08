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

#ifndef DCPLUSPLUS_DCPP_APIMODULE_H
#define DCPLUSPLUS_DCPP_APIMODULE_H

#include <web-server/stdinc.h>

#include <web-server/Access.h>
#include <web-server/ApiRequest.h>
#include <web-server/SessionListener.h>

namespace webserver {
	using boost::regex;

	class WebSocket;
	class ApiModule {
	public:
#define NUM_PARAM (regex(R"(\d+)"))
#define TOKEN_PARAM NUM_PARAM
#define TTH_PARAM (regex(R"([0-9A-Z]{39})"))
#define CID_PARAM TTH_PARAM
#define STR_PARAM (regex(R"(\w+)"))
#define EXACT_PARAM(pattern) (regex("^" + string(pattern) + "$"))

#define BRACED_INIT_LIST(...) {__VA_ARGS__}
#define METHOD_HANDLER(section, access, method, params, requireJson, func) (requestHandlers[section].push_back(ApiModule::RequestHandler(access, method, requireJson, BRACED_INIT_LIST params, std::bind(&func, this, placeholders::_1))))

		ApiModule(Session* aSession);
		virtual ~ApiModule();

		struct RequestHandler {
			typedef vector<regex> ParamList;

			typedef std::vector<RequestHandler> List;
			typedef std::function<api_return(ApiRequest& aRequest)> HandlerFunction;

			// Regular handler
			RequestHandler(Access aAccess, ApiRequest::Method aMethod, bool aRequireJson, ParamList&& aParams, HandlerFunction aFunction) :
				method(aMethod), requireJson(aRequireJson), params(std::move(aParams)), f(aFunction), access(aAccess) {
			
				dcassert((aMethod != ApiRequest::METHOD_DELETE && aMethod != ApiRequest::METHOD_GET) || !aRequireJson);
			}

			// Forwarder
			// Used with hierarchial modules when adding matcher for submodule IDs in the parent
			RequestHandler(const regex& aMatch, HandlerFunction aFunction) :
				params({ aMatch }), f(aFunction), access(Access::ANY), method(ApiRequest::METHOD_FORWARD), requireJson(false) { }

			const ApiRequest::Method method;
			const bool requireJson;
			const ParamList params;
			const HandlerFunction f;
			const Access access;

			bool matchParams(const ApiRequest::RequestParamList& aParams) const noexcept;
		};

		typedef std::map<std::string, RequestHandler::List> RequestHandlerMap;

		api_return handleRequest(ApiRequest& aRequest);

		virtual int getVersion() const noexcept {
			// Root module should always have version specified (and this shouldn't be called for submodules)
			dcassert(0);
			return -1;
		}

		ApiModule(ApiModule&) = delete;
		ApiModule& operator=(ApiModule&) = delete;

		virtual void addAsyncTask(CallBack&& aTask);
		virtual TimerPtr getTimer(CallBack&& aTask, time_t aIntervalMillis);

		Session* getSession() const noexcept {
			return session;
		}

		RequestHandlerMap& getRequestHandlers() noexcept {
			return requestHandlers;
		}

		// All custom async tasks should be run inside this to
		// ensure that the session won't get deleted
		virtual CallBack getAsyncWrapper(CallBack&& aTask) noexcept;
	protected:
		static void asyncRunWrapper(const CallBack& aTask, LocalSessionId aSessionId);

		Session* session;

		RequestHandlerMap requestHandlers;
	};

	
	class SubscribableApiModule : public ApiModule, private SessionListener {
	public:
		SubscribableApiModule(Session* aSession, Access aSubscriptionAccess, const StringList* aSubscriptions = nullptr);
		virtual ~SubscribableApiModule();

		typedef std::map<const string, bool> SubscriptionMap;

		virtual void on(SessionListener::SocketConnected, const WebSocketPtr&) noexcept override;
		virtual void on(SessionListener::SocketDisconnected) noexcept override;

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
			subscriptions[aSubscription];
		}

		Access getSubscriptionAccess() const noexcept {
			return subscriptionAccess;
		}
	protected:
		const Access subscriptionAccess;

		WebSocketPtr socket = nullptr;

		virtual api_return handleSubscribe(ApiRequest& aRequest);
		virtual api_return handleUnsubscribe(ApiRequest& aRequest);
	private:
		SubscriptionMap subscriptions;
	};

	typedef std::unique_ptr<ApiModule> HandlerPtr;
}

#endif