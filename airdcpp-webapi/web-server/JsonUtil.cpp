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

#include <web-server/stdinc.h>

#include <web-server/JsonUtil.h>

namespace webserver {
	string JsonUtil::errorTypeToString(ErrorType aType) noexcept {
		switch (aType) {
			case ERROR_MISSING: return "missing_field";
			case ERROR_INVALID: return "invalid";
			case ERROR_EXISTS: return "already_exists";
			default: dcassert(0); return "";
		}
	}

	json JsonUtil::getError(const string& aFieldName, ErrorType aType, const string& aMessage) noexcept {
		return {
			{ "message", aMessage },
			{ "field", aFieldName },
			{ "code", errorTypeToString(aType) }
		};
	}

	json JsonUtil::filterExactValues(const json& aNew, const json& aCompareTo) noexcept {
		json ret = aNew;
		for (const auto& v: json::iterator_wrapper(aCompareTo)) {
			auto key = v.key();
			auto i = aNew.find(key);
			if (i != aNew.end() && aNew.at(key) == aCompareTo.at(key)) {
				ret.erase(key);
			}
		}

		return ret;
	}

	void JsonUtil::ensureType(const string& aFieldName, const json& aNew, const json& aExisting) {
		if (aExisting.is_number()) {
			if (!aNew.is_number()) {
				throwError(aFieldName, ERROR_INVALID, "The new value must be a number");
			}
		} else if (aNew.type() != aExisting.type()) {
			throwError(aFieldName, ERROR_INVALID, "Type of the new value doesn't match with the existing type");
		}
	}
}