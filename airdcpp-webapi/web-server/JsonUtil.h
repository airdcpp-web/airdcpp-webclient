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

#ifndef DCPLUSPLUS_DCPP_JSONUTIL_H
#define DCPLUSPLUS_DCPP_JSONUTIL_H

#include "stdinc.h"

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
			auto value = getOptionalField<T>(aFieldName, aJson, aRequired);
			if (value) {
				validateRange(aFieldName, *value, aMin, aMax);
			}

			return value;
		}

		template <typename T>
		static void validateRange(const string& aFieldName, const T& aValue, int aMin, int aMax) {
			if (aValue < aMin || aValue > aMax) {
				throwError(aFieldName, ERROR_INVALID,
					"Value " + std::to_string(aValue) + " is not in range " +
					std::to_string(aMin) + " - " + std::to_string(aMax));
			}
		}

		template <typename T, typename JsonT>
		static T getEnumFieldDefault(const string& aFieldName, const JsonT& aJson, T aDefault, int aMin, int aMax) {
			auto value = getEnumField<T, JsonT>(aFieldName, aJson, false, aMin, aMax);
			return value ? *value : aDefault;
		}

		// Can be used to return null values for non-existing fields. Behaves similar to getField when throwIfMissing is true.
		template <typename T, typename JsonT>
		static optional<T> getOptionalField(const string& aFieldName, const JsonT& aJson, bool aThrowIfMissing = false) {
			if (aThrowIfMissing) {
				return getField<T>(aFieldName, aJson, false);
			}

			if (aJson.is_null()) {
				return nullopt;
			}

			auto p = aJson.find(aFieldName);
			if (p == aJson.end()) {
				return nullopt;
			}

			auto value = parseValue<T>(aFieldName, *p, true);
			if (isEmpty<T>(value, *p)) {
				return nullopt;
			}

			return value;
		}

		// Get the field value if it exists and return the default otherwise
		template <typename T, typename JsonT>
		static T getOptionalFieldDefault(const string& aFieldName, const JsonT& aJson, const T& aDefault) {
			auto v = getOptionalField<T>(aFieldName, aJson);
			if (v) {
				return *v;
			}

			return aDefault;
		}

		// Returns raw JSON value and throws if the field is missing
		template <typename JsonT>
		static json getRawField(const string& aFieldName, const JsonT& aJson) {
			return getRawValue<JsonT>(aFieldName, aJson, true);
		}

		// Returns raw JSON value and returns null JSON if the field is missing
		template <typename JsonT>
		static json getOptionalRawField(const string& aFieldName, const JsonT& aJson, bool aThrowIfMissing = false) {
			return getRawValue<JsonT>(aFieldName, aJson, aThrowIfMissing);
		}

		// Find and parse the given field. Throws if not found.
		template <typename T, typename JsonT>
		static T getField(const string& aFieldName, const JsonT& aJson, bool aAllowEmpty = true) {
			return parseValue<T>(aFieldName, getRawValue(aFieldName, aJson, true), aAllowEmpty);
		}

		// Get value from the given JSON element
		template <typename T, typename JsonT>
		static T parseValue(const string& aFieldName, const JsonT& aJson, bool aAllowEmpty = true) {
			if (!aJson.is_null()) {
				T ret;
				try {
					ret = aJson.template get<T>();
				} catch (const exception& e) {
					throwError(aFieldName, ERROR_INVALID, e.what());
					return T(); // avoid MSVC warning
				}

				if (!aAllowEmpty && isEmpty<T>(ret, aJson)) {
					throwError(aFieldName, ERROR_INVALID, "Field can't be empty");
					return T(); // avoid MSVC warning
				}

				return ret;
			}

			if (!aAllowEmpty) {
				throwError(aFieldName, ERROR_INVALID, "Field can't be null");
			}

			return convertNullValue<T>(aFieldName);
		}

		static void throwError(const string& aFieldName, ErrorType aType, const string& aMessage)  {
			throw ArgumentException(getError(aFieldName, aType, aMessage));
		}

		static json getError(const string& aFieldName, ErrorType aType, const string& aMessage) noexcept;

		// Return a new JSON object with exact key-value pairs removed
		static json filterExactValues(const json& aNew, const json& aCompareTo) noexcept;
	private:
		// Returns raw JSON value and optionally throws
		template <typename JsonT>
		static json getRawValue(const string& aFieldName, const JsonT& aJson, bool aThrowIfMissing) {
			if (aJson.is_null()) {
				if (!aThrowIfMissing) {
					return json();
				}

				throwError(aFieldName, ERROR_MISSING, "JSON null");
			}

			auto p = aJson.find(aFieldName);
			if (p == aJson.end()) {
				if (!aThrowIfMissing) {
					return json();
				}

				throwError(aFieldName, ERROR_MISSING, "Field missing");
			}

			return *p;
		}

		template <class T, typename JsonT>
		static bool isEmpty(const typename std::enable_if<std::is_same<std::string, T>::value, T>::type& aStr, const JsonT&) {
			return aStr.empty();
		}

		template <class T, typename JsonT>
		static bool isEmpty(const typename std::enable_if<!std::is_same<std::string, T>::value, T>::type&, const JsonT& aJson) {
			return aJson.empty();
		}

		// Non-integral types should be initialized with the default constructor
		template <class T>
		static typename std::enable_if<!std::is_integral<T>::value, T>::type convertNullValue(const string&) {
			return T();
		}

		template <class T>
		static typename std::enable_if<std::is_integral<T>::value, T>::type convertNullValue(const string& aFieldName) {
			throw ArgumentException(getError(aFieldName, ERROR_INVALID, "Field can't be empty"));
		}

		static string errorTypeToString(ErrorType aType) noexcept;
	};
}

#endif
