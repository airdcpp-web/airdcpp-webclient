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

#include <airdcpp/StringMatch.h>

namespace webserver {
	class WebSocket;
	class ApiModule : private SessionListener {
	public:
#define NUM_PARAM (StringMatch::getSearch(R"(\d+)", StringMatch::REGEX))
#define TOKEN_PARAM NUM_PARAM
#define TTH_PARAM (StringMatch::getSearch(R"([0-9A-Z]{39})", StringMatch::REGEX))
#define CID_PARAM TTH_PARAM
#define STR_PARAM (StringMatch::getSearch(R"([a-z_]+)", StringMatch::REGEX))
#define EXACT_PARAM(pattern) (StringMatch::getSearch(pattern, StringMatch::EXACT))

#define BRACED_INIT_LIST(...) {__VA_ARGS__}
#define METHOD_HANDLER(section, access, method, params, requireJson, func) (requestHandlers[section].push_back(ApiModule::RequestHandler(access, method, requireJson, BRACED_INIT_LIST params, std::bind(&func, this, placeholders::_1))))

		ApiModule(Session* aSession, Access aSubscriptionAccess = Access::NONE, const StringList* aSubscriptions = nullptr);
		virtual ~ApiModule();

		typedef vector<StringMatch> ParamList;
		struct RequestHandler {
			typedef std::vector<RequestHandler> List;
			typedef std::function<api_return(ApiRequest& aRequest)> HandlerFunction;

			// Regular handler
			RequestHandler(Access aAccess, ApiRequest::Method aMethod, bool aRequireJson, ParamList&& aParams, HandlerFunction aFunction) :
				method(aMethod), requireJson(aRequireJson), params(move(aParams)), f(aFunction), access(aAccess) {
			
				dcassert((aMethod != ApiRequest::METHOD_DELETE && aMethod != ApiRequest::METHOD_GET) || !aRequireJson);
			}

			// Sub handler
			RequestHandler(const StringMatch& aMatch, HandlerFunction aFunction) :
				params({ aMatch }), f(aFunction), access(Access::ANY) { }

			const ApiRequest::Method method = ApiRequest::METHOD_LAST;
			const bool requireJson = false;
			const ParamList params;
			const HandlerFunction f;
			const Access access;

			bool matchParams(const ApiRequest::RequestParamList& aParams) const noexcept;
			bool isModuleHandler() const noexcept {
				return method == ApiRequest::METHOD_LAST;
			}
		};

		typedef std::map<const string, bool> SubscriptionMap;
		typedef std::map<std::string, RequestHandler::List> RequestHandlerMap;

		api_return handleRequest(ApiRequest& aRequest);

		virtual void on(SessionListener::SocketConnected, const WebSocketPtr&) noexcept;
		virtual void on(SessionListener::SocketDisconnected) noexcept;

		virtual int getVersion() const noexcept {
			dcdebug("Root module should always have version specified");
			return -1;
		}

		ApiModule(ApiModule&) = delete;
		ApiModule& operator=(ApiModule&) = delete;

		virtual bool send(const json& aJson);
		virtual bool send(const string& aSubscription, const json& aJson);

		typedef std::function<json()> JsonCallback;
		virtual bool maybeSend(const string& aSubscription, JsonCallback aCallback);

		virtual void addAsyncTask(CallBack&& aTask);
		virtual TimerPtr getTimer(CallBack&& aTask, time_t aIntervalMillis);

		// All custom async tasks should be run inside this to
		// ensure that the session won't get deleted

		Session* getSession() const noexcept {
			return session;
		}

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

		RequestHandlerMap& getRequestHandlers() noexcept {
			return requestHandlers;
		}

		Access getSubscriptionAccess() const noexcept {
			return subscriptionAccess;
		}

		virtual CallBack getAsyncWrapper(CallBack&& aTask) noexcept;
	protected:
		static void asyncRunWrapper(const CallBack& aTask, LocalSessionId aSessionId);

		const Access subscriptionAccess;
		Session* session;

		RequestHandlerMap requestHandlers;

		WebSocketPtr socket = nullptr;

		virtual api_return handleSubscribe(ApiRequest& aRequest);
		virtual api_return handleUnsubscribe(ApiRequest& aRequest);
	private:
		SubscriptionMap subscriptions;
	};

	typedef std::unique_ptr<ApiModule> HandlerPtr;
}

#endif