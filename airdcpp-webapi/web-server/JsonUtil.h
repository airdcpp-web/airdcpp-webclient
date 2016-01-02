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

#ifndef DCPLUSPLUS_DCPP_JSONUTIL_H
#define DCPLUSPLUS_DCPP_JSONUTIL_H

#include <web-server/stdinc.h>

namespace webserver {
	class JsonUtil {
	public:
		enum ErrorType {
			ERROR_MISSING,
			ERROR_INVALID,
			ERROR_EXISTS,
			ERROR_LAST
		};

		// Return enum field with range validation
		template <typename T, typename JsonT>
		static optional<T> getEnumField(const string& aFieldName, const JsonT& aJson, bool aRequired, int aMin, int aMax) {
			auto value = getOptionalField<T>(aFieldName, aJson, false, aRequired);
			if (value) {
				if (*value < aMin || *value > aMax) {
					throwError(aFieldName, ERROR_INVALID,
						"Value " + std::to_string(*value) + " is not in range " + 
						std::to_string(aMin) + " - " + std::to_string(aMax));
				}
			}

			return value;
		}

		// Can be used to return null values for non-existing fields. Behaves similar to getField when throwIfMissing is true.
		template <typename T, typename JsonT>
		static optional<T> getOptionalField(const string& aFieldName, const JsonT& aJson, bool aAllowEmpty = true, bool throwIfMissing = false) {
			if (throwIfMissing) {
				return getField<T>(aFieldName, aJson, aAllowEmpty);
			}

			if (aJson.is_null()) {
				return boost::none;
			}

			auto p = aJson.find(aFieldName);
			if (p == aJson.end()) {
				return boost::none;
			}

			return parseValue<T>(aFieldName, *p, aAllowEmpty);
		}

		// Get the field value if it exists and return the default otherwise
		template <typename T, typename JsonT>
		static T getOptionalFieldDefault(const string& aFieldName, const JsonT& aJson, const T& aDefault, bool aAllowEmpty = true) {
			auto v = getOptionalField<T>(aFieldName, aJson, aAllowEmpty);
			if (v) {
				return *v;
			}

			return aDefault;
		}

		// Returns raw JSON value and throws if the field is missing
		template <typename JsonT>
		static json getRawValue(const string& aFieldName, const JsonT& aJson) {
			if (aJson.is_null()) {
				throwError(aFieldName, ERROR_MISSING, "JSON null");
			}

			auto p = aJson.find(aFieldName);
			if (p == aJson.end()) {
				throwError(aFieldName, ERROR_MISSING, "Field missing");
			}

			return *p;
		}

		// Find and parse the given field. Throws if not found.
		template <typename T, typename JsonT>
		static T getField(const string& aFieldName, const JsonT& aJson, bool aAllowEmpty = true) {
			return parseValue<T>(aFieldName, getRawValue(aFieldName, aJson), aAllowEmpty);
		}

		// Get value from the given JSON element
		template <typename T, typename JsonT>
		static T parseValue(const string& aFieldName, const JsonT& aJson, bool aAllowEmpty = true) {
			if (!aJson.is_null()) {
				T ret;
				try {
					ret = aJson.get<T>();
				}
				catch (const exception& e) {
					throwError(aFieldName, ERROR_INVALID, e.what());
				}

				if (!aAllowEmpty && (isEmpty<T>(ret) || aJson.empty())) {
					throwError(aFieldName, ERROR_INVALID, "Field can't be empty");
				}

				return ret;
			}

			if (!aAllowEmpty) {
				throwError(aFieldName, ERROR_INVALID, "Field can't be null");
			}

			// Strings get converted to "", throws otherwise
			return convertNullValue<T>(aFieldName);
		}

		static void throwError(const string& aFieldName, ErrorType aType, const string& aMessage)  {
			throw ArgumentException(getError(aFieldName, aType, aMessage));
		}

		static json getError(const string& aFieldName, ErrorType aType, const string& aMessage) noexcept;
	private:
		template <class T>
		static bool isEmpty(const typename std::enable_if<std::is_same<std::string, T>::value, T>::type& aStr) {
			return aStr.empty();
		}

		template <class T>
		static bool isEmpty(const typename std::enable_if<!std::is_same<std::string, T>::value, T>::type& aValue) {
			return false;
		}

		// Convert null strings, add more conversions if needed
		template <class T>
		static typename std::enable_if<std::is_same<std::string, T>::value, T>::type convertNullValue(const string&) {
			return "";
		}

		template <class T>
		static typename std::enable_if<!std::is_same<std::string, T>::value, T>::type convertNullValue(const string& aFieldName) {
			throw ArgumentException(getError(aFieldName, ERROR_INVALID, "Field can't be empty"));
		}
	};
}

#endif
