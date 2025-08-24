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

#ifndef DCPLUSPLUS_WEBSERVER_APIREQUEST_H
#define DCPLUSPLUS_WEBSERVER_APIREQUEST_H

#include "forward.h"
#include "stdinc.h"

#include <airdcpp/core/header/typedefs.h>
#include <airdcpp/core/types/GetSet.h>

#define TOKEN_PARAM_ID "id_param"
#define TTH_PARAM_ID "tth_param"
#define CID_PARAM_ID "cid_param"

#define CODE_DEFERRED http_status::see_other

namespace webserver {
	enum RequestMethod {
		METHOD_POST,
		METHOD_GET,
		METHOD_PUT,
		METHOD_DELETE,
		METHOD_PATCH,
		METHOD_FORWARD, // Special 'any' method for internal API handlers
		METHOD_LAST
	};

	class ApiRequest {
	public:
		using PathTokenList = std::deque<std::string>;
		using NamedParamMap = std::map<std::string, std::string>;

		// Throws std::invalid_argument on validation errors
		ApiRequest(const std::string& aUrl, const std::string& aMethod, json&& aBody, const SessionPtr& aSession, const ApiDeferredHandler& aDeferredHandler, json& output_, json& error_);

		int getApiVersion() const noexcept {
			return apiVersion;
		}

		const std::string& getApiModule() const noexcept {
			return apiModule;
		}

		RequestMethod getMethod() const noexcept {
			return method;
		}

		const string& getMethodStr() const noexcept {
			return methodStr;
		}

		const PathTokenList& getPathTokens() const noexcept {
			return pathTokens;
		}

		void popParam(size_t aCount = 1) noexcept;

		const std::string& getStringParam(const string& aName) const noexcept;
		const std::string& getPathTokenAt(int aIndex) const noexcept;

		// Throws in case of errors
		TTHValue getTTHParam(const string& aName = TTH_PARAM_ID) const;

		// Throws in case of errors
		CID getCIDParam(const string& aName = CID_PARAM_ID) const;

		// Use different naming to avoid accidentally using wrong conversion...
		size_t getTokenParam(const string& aName = TOKEN_PARAM_ID) const noexcept;
		int getRangeParam(const string& aName) const noexcept;
		int64_t getSizeParam(const string& aName) const noexcept;

		bool hasRequestBody() const noexcept {
			return !requestJson.is_null();
		}

		bool hasErrorMessage() const noexcept {
			return !responseJsonError.is_null();
		}

		const json& getRequestBody() const noexcept {
			return requestJson;
		}

		void setResponseBody(const json& aResponse) {
			responseJsonData = aResponse;
		}

		void setResponseErrorStr(const std::string& aError) {
			responseJsonError = toResponseErrorStr(aError);
		}

		static json toResponseErrorStr(const std::string& aError) noexcept {
			return {
				{ "message", aError }
			};
		}

		void setResponseErrorJson(const json& aError) {
			responseJsonError = aError;
		}

		const SessionPtr& getSession() const noexcept {
			return session;
		}

		CallerPtr getOwnerPtr() const noexcept {
			return session.get();
		}

		const string& getRequestPath() const noexcept {
			return path;
		}

		void setNamedParams(const NamedParamMap& aParams) noexcept;

		ApiCompletionF defer() const noexcept;
	private:
		SessionPtr session;
		void validate();

		const string path;
		const string methodStr;
		PathTokenList pathTokens;
		NamedParamMap namedParameters;
		int apiVersion = -1;
		std::string apiModule;

		RequestMethod method = METHOD_LAST;

		const json requestJson;

		json& responseJsonData;
		json& responseJsonError;
		ApiDeferredHandler deferredHandler;
	};

	struct RouterRequest {
		ApiRequest& apiRequest;
		const bool isSecure;
		const SessionCallback& authenticationCallback;
		const string& ip;
	};
}

#endif