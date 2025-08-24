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

#ifndef DCPLUSPLUS_WEBSERVER_EXCEPTION_H
#define DCPLUSPLUS_WEBSERVER_EXCEPTION_H

#include "stdinc.h"

namespace webserver {
	using json = nlohmann::json;
	using http_status = websocketpp::http::status_code::value;

	class JsonException : public std::runtime_error
	{
	public:
		enum ErrorType {
			ERROR_MISSING,
			ERROR_INVALID,
			ERROR_EXISTS,
			ERROR_LAST
		};

		JsonException(const std::string& aFieldName, ErrorType aType, const std::string& aMessage);

		virtual ~JsonException() noexcept { }
		json toJSON() const noexcept;
		JsonException toField(const std::string& aFieldName) const noexcept;

		const std::string& getField() const noexcept {
			return fieldName;
		}

		const ErrorType getType() const noexcept {
			return type;
		}
	protected:
		const std::string fieldName;
		const ErrorType type;

		static std::string errorTypeToString(ErrorType aType) noexcept;
	};

	class RequestException : public std::runtime_error
	{
	public:
		RequestException(http_status aCode, const std::string& aMessage) : code(aCode), std::runtime_error(aMessage.c_str()) { }

		http_status getCode() const noexcept { return code; }
	protected:
		const http_status code;
	};
}

#endif // !defined(EXCEPTION_H)