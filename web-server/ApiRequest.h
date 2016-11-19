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

#ifndef DCPLUSPLUS_DCPP_APIREQUEST_H
#define DCPLUSPLUS_DCPP_APIREQUEST_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/GetSet.h>

namespace webserver {
	class ApiRequest {
	public:
		typedef deque<std::string> RequestParamList;
		enum Method {
			METHOD_POST,
			METHOD_GET,
			METHOD_PUT,
			METHOD_DELETE,
			METHOD_PATCH,
			METHOD_FORWARD, // Special 'any' method for internal API handlers
			METHOD_LAST
		};

		ApiRequest(const std::string& aUrl, const std::string& aMethod, json& output_, json& error_) noexcept;

		void validate();

		int getApiVersion() const noexcept {
			return apiVersion;
		}

		const std::string& getApiModule() const noexcept {
			return apiModule;
		}

		Method getMethod() const noexcept {
			return method;
		}

		const RequestParamList& getParameters() const noexcept {
			return parameters;
		}

		void popParam(size_t aCount = 1) noexcept;

		const std::string& getStringParam(int pos) const noexcept;

		// Use different naming to avoid accidentally using wrong conversion...
		uint32_t getTokenParam(int pos) const noexcept;
		int getRangeParam(int pos) const noexcept;

		//GETSET(std::string , response, Response);
		GETSET(SessionPtr, session, Session);

		void parseHttpRequestJson(const std::string& aRequestBody);
		void parseSocketRequestJson(const json& aJson);

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
	private:
		RequestParamList parameters;
		int apiVersion = -1;
		std::string apiModule;

		Method method = METHOD_LAST;

		json requestJson;

		json& responseJsonData;
		json& responseJsonError;
	};
}

#endif