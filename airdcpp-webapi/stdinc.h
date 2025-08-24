/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#ifndef DCPLUSPLUS_WEBSERVER_STDINC_H
#define DCPLUSPLUS_WEBSERVER_STDINC_H

#ifndef _WEBSOCKETPP_CPP11_STL_
#define _WEBSOCKETPP_CPP11_STL_
#endif

#include <airdcpp/stdinc.h>

#include <websocketpp/http/constants.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>

#include "json.h"


namespace webserver {
	using json = nlohmann::json;
	using http_status = websocketpp::http::status_code::value;

	// define types for two different server endpoints, one for each config we are
	// using
	using server_plain = websocketpp::server<websocketpp::config::asio>;
	using server_tls = websocketpp::server<websocketpp::config::asio_tls>;
	using api_return = http_status;

	using HTTPFileCompletionF = std::function<void(api_return aStatus, const std::string& aOutput, const std::vector<std::pair<std::string, std::string>>& aHeaders)>;
	using ApiCompletionF = std::function<void(api_return aStatus, const json& aResponseJsonData, const json& aResponseErrorJson)>;
	using FileDeferredHandler = std::function<HTTPFileCompletionF()> ;
	using ApiDeferredHandler = std::function<ApiCompletionF()>;

	using namespace dcpp;
}

#endif // !defined(DCPLUSPLUS_WEBSERVER_STDINC_H)
