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

#ifndef DCPLUSPLUS_WEBSERVER_APIMODULE_H
#define DCPLUSPLUS_WEBSERVER_APIMODULE_H

#include "forward.h"

#include <web-server/Access.h>
#include <web-server/ApiRequest.h>

#include <airdcpp/core/header/debug.h>

namespace webserver {
	using boost::regex;

	class ApiModule {
	public:
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

// Private
#define INLINE_MODULE_METHOD_HANDLER(module, access, method, params, func) (module->getRequestHandlers().push_back(ApiModule::RequestHandler(access, method, BRACED_INIT_LIST params, func)))
#define MODULE_METHOD_HANDLER_BOUND(module, access, method, params, func, bound) INLINE_MODULE_METHOD_HANDLER(module, access, method, params, std::bind_front(&func, bound))


// Public

// Module is a variable module with a module handler method as a member of the calling class
#define MODULE_METHOD_HANDLER(module, access, method, params, func) MODULE_METHOD_HANDLER_BOUND(module, access, method, params, func, this)

// Handler for the current module with a lambda handler (forwarding to INLINE_MODULE_METHOD_HANDLER causes errors with GCC in MenuApi...)
// #define INLINE_METHOD_HANDLER(access, method, params, func) INLINE_MODULE_METHOD_HANDLER(this, access, method, params, func)
#define INLINE_METHOD_HANDLER(access, method, params, func) (this->getRequestHandlers().push_back(ApiModule::RequestHandler(access, method, BRACED_INIT_LIST params, func)))

// Regular handler for the current module with a module handler method
#define METHOD_HANDLER(access, method, params, func) MODULE_METHOD_HANDLER(this, access, method, params, func)

// Handler is bound to a custom variable
#define VARIABLE_METHOD_HANDLER(access, method, params, func, bound) MODULE_METHOD_HANDLER_BOUND(this, access, method, params, func, bound)

		explicit ApiModule(Session* aSession);
		virtual ~ApiModule();

		struct RequestHandler {
			struct Param {
				Param(string aParamId, regex&& aReg) : id(std::move(aParamId)), reg(std::move(aReg)) { }

				string id;
				regex reg;
			};

			using ParamList = vector<Param>;

			using HandlerFunction = std::function<api_return (ApiRequest &)>;

			// Regular handler
			RequestHandler(Access aAccess, RequestMethod aMethod, ParamList&& aParams, HandlerFunction&& aFunction) :
				method(aMethod), params(std::move(aParams)), f(std::move(aFunction)), access(aAccess) {
			
			}

			const RequestMethod method;
			const ParamList params;
			const HandlerFunction f;
			const Access access;

			optional<ApiRequest::NamedParamMap> matchParams(const ApiRequest::PathTokenList& aPathTokens) const noexcept;
		};

		using RequestHandlerList = std::vector<RequestHandler>;

		api_return handleRequest(ApiRequest& aRequest);

		ApiModule(ApiModule&) = delete;
		ApiModule& operator=(ApiModule&) = delete;

		virtual void addAsyncTask(Callback&& aTask);
		virtual TimerPtr getTimer(Callback&& aTask, time_t aIntervalMillis);

		Session* getSession() const noexcept {
			return session;
		}

		RequestHandlerList& getRequestHandlers() noexcept {
			return requestHandlers;
		}

		// All custom async tasks should be run inside this to
		// ensure that the session won't get deleted
		virtual Callback getAsyncWrapper(Callback&& aTask) noexcept;
	protected:
		static void asyncRunWrapper(const Callback& aTask, LocalSessionId aSessionId);

		Session* session;

		RequestHandlerList requestHandlers;
	};

	using HandlerPtr = std::unique_ptr<ApiModule>;
}

#endif