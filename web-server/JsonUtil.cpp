/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#include <web-server/stdinc.h>

#include <web-server/JsonUtil.h>

namespace webserver {
	json JsonUtil::getError(const string& fieldName, ErrorType aType, const string& aMessage) noexcept {
		auto errorTypeToString = [](ErrorType aType) {
			switch (aType) {
			case ERROR_MISSING: return "missing_field";
			case ERROR_INVALID: return "invalid";
			case ERROR_EXISTS: return "already_exists";
			}

			return "error";
		};

		/*json error = {
			{ "message",  aMessage },
			{ "errors",{
				{ "field", fieldName },
				{ "code", errorTypeToString(aType) }
			}
			}
		};*/

		json error;
		error["message"] = aMessage;
		error["field"] = fieldName;
		error["code"] = errorTypeToString(aType);

		return error;
	}
}