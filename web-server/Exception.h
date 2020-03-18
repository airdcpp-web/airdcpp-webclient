/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#ifndef DCPLUSPLUS_WEBSERVER_EXCEPTION_H
#define DCPLUSPLUS_WEBSERVER_EXCEPTION_H

#include <nlohmann/json.hpp>
#include <websocketpp/http/constants.hpp>
#include <string>

#include <airdcpp/debug.h>

namespace webserver {
	using json = nlohmann::json;

	class JsonException : public std::runtime_error
	{
	public:
		JsonException(const json& aError, const std::string& aMessage) : error(aError), std::runtime_error(aMessage.c_str()) { }
		JsonException(json&& aError, const std::string& aMessage) : error(move(aError)), std::runtime_error(aMessage.c_str()) { }

		virtual ~JsonException() noexcept { }
		const json& getErrorJson() const { return error; }
	protected:
		json error;
	};

	class RequestException : public std::runtime_error
	{
	public:
		RequestException(websocketpp::http::status_code::value aCode, const std::string& aMessage) : code(aCode), std::runtime_error(aMessage.c_str()) { }

		websocketpp::http::status_code::value getCode() const noexcept { return code; }
	protected:
		const websocketpp::http::status_code::value code;
	};
}

#endif // !defined(EXCEPTION_H)