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

#ifndef DCPLUSPLUS_DCPP_APIREQUEST_H
#define DCPLUSPLUS_DCPP_APIREQUEST_H

#include "stdinc.h"

#include <airdcpp/typedefs.h>
#include <airdcpp/GetSet.h>

#define TOKEN_PARAM_ID "id_param"
#define TTH_PARAM_ID "tth_param"
#define CID_PARAM_ID "cid_param"

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
		typedef std::deque<std::string> PathTokenList;
		typedef std::map<std::string, std::string> NamedParamMap;

		// Throws on errors
		ApiRequest(const std::string& aUrl, const std::string& aMethod, const json& aBody, const SessionPtr& aSession, json& output_, json& error_);

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
		uint32_t getTokenParam(const string& aName = TOKEN_PARAM_ID) const noexcept;
		int getRangeParam(const string& aName) const noexcept;
		int64_t getSizeParam(const string& aName) const noexcept;

		bool hasRequestBody() const noexcept {
			return !requestJson.is_null();
		}

		const json& getRequestBody() const noexcept {
			return requestJson;
		}

		void setResponseBody(const json& aResponse) {
			responseJsonData = aResponse;
		}

		void setResponseErrorStr(const std::string& aError) {
			responseJsonError = {
				{ "message", aError } 
			};
		}

		void setResponseErrorJson(const json& aError) {
			responseJsonError = aError;
		}

		const SessionPtr& getSession() const noexcept {
			return session;
		}

		const string& getRequestPath() const noexcept {
			return path;
		}

		void setNamedParams(const NamedParamMap& aParams) noexcept;
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
	};
}

#endif