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
#include <web-server/version.h>
#include <web-server/ApiRequest.h>

#include <airdcpp/CID.h>
#include <airdcpp/MerkleTree.h>

#include <airdcpp/StringTokenizer.h>
#include <airdcpp/Util.h>

namespace webserver {
	ApiRequest::ApiRequest(const string& aUrl, const string& aMethod, json&& aBody, const SessionPtr& aSession, const ApiDeferredHandler& aDeferredHandler, json& output_, json& error_) :
		session(aSession), path(aUrl), methodStr(aMethod), requestJson(std::move(aBody)), responseJsonData(output_), responseJsonError(error_), deferredHandler(aDeferredHandler)
	{

		if (aUrl.compare(0, 4, "/api") != 0) {
			throw std::invalid_argument("Invalid URL path (the path should start with /api/v" + Util::toString(API_VERSION) + "/)");
		}

		pathTokens = StringTokenizer<std::string, deque>(aUrl.substr(4), '/').getTokens();

		if (aMethod == "GET") {
			method = METHOD_GET;
		} else if (aMethod == "POST") {
			method = METHOD_POST;
		} else if (aMethod == "PUT") {
			method = METHOD_PUT;
		} else if (aMethod == "DELETE") {
			method = METHOD_DELETE;
		} else if (aMethod == "PATCH") {
			method = METHOD_PATCH;
		}

		validate();
	}

	void ApiRequest::validate() {
		// Method
		if (method == METHOD_LAST) {
			throw std::invalid_argument("Unsupported method");
		}

		// Version and module are always mandatory
		if (static_cast<int>(pathTokens.size()) < 2) {
			throw std::invalid_argument("Not enough URL parameters");
		}

		// Version
		const auto version = pathTokens.front();
		pathTokens.pop_front();

		// API Module
		apiModule = pathTokens.front();
		pathTokens.pop_front();

		if (version.size() < 2) {
			throw std::invalid_argument("Invalid API version format");
		}

		apiVersion = Util::toInt(version.substr(1));
	}

	void ApiRequest::setNamedParams(const NamedParamMap& aParams) noexcept {
		namedParameters = aParams;
	}

	void ApiRequest::popParam(size_t aCount) noexcept {
		pathTokens.erase(pathTokens.begin(), pathTokens.begin() + aCount);
	}

	size_t ApiRequest::getTokenParam(const string& aName) const noexcept {
		return Util::toSizeT(namedParameters.at(aName));
	}

	const string& ApiRequest::getStringParam(const string& aName) const noexcept {
		return namedParameters.at(aName);
	}

	int ApiRequest::getRangeParam(const string& aName) const noexcept {
		return Util::toInt(namedParameters.at(aName));
	}

	int64_t ApiRequest::getSizeParam(const string& aName) const noexcept {
		return Util::toInt64(namedParameters.at(aName));
	}

	const std::string& ApiRequest::getPathTokenAt(int aIndex) const noexcept {
		return pathTokens[aIndex];
	}

	TTHValue ApiRequest::getTTHParam(const string& aName) const {
		const auto& param = getStringParam(aName);
		if (!Encoder::isBase32(param.c_str())) {
			throw std::invalid_argument("Invalid TTH URL parameter");
		}

		return TTHValue(param);
	}

	CID ApiRequest::getCIDParam(const string& aName) const {
		const auto& param = getStringParam(aName);
		if (!Encoder::isBase32(param.c_str())) {
			throw std::invalid_argument("Invalid CID URL parameter");
		}

		return CID(param);
	}


	ApiCompletionF ApiRequest::defer() const noexcept {
		return deferredHandler();
	}
}
