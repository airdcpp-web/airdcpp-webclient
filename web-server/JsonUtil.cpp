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

#include <web-server/JsonUtil.h>

#include <airdcpp/core/header/debug.h>

namespace webserver {

	const json JsonUtil::emptyJson;


	JsonException::JsonException(const std::string& aFieldName, ErrorType aType, const std::string& aMessage) : fieldName(aFieldName), type(aType), std::runtime_error(aMessage.c_str()) { 
		dcassert(!aFieldName.empty());
		dcassert(!aMessage.empty());
	}

	string JsonException::errorTypeToString(ArgumentException::ErrorType aType) noexcept {
		switch (aType) {
			case ArgumentException::ERROR_MISSING: return "missing_field";
			case ArgumentException::ERROR_INVALID: return "invalid";
			case ArgumentException::ERROR_EXISTS: return "already_exists";
			default: dcassert(0); return "";
		}
	}

	json JsonException::toJSON() const noexcept {
		return {
			{ "message", what() },
			{ "field", fieldName },
			{ "code", errorTypeToString(type) }
		};
	}


	JsonException JsonException::toField(const string& aFieldName) const noexcept {
		return JsonException(aFieldName, type, what());
	}
}