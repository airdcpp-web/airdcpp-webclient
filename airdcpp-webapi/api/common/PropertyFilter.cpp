/*
* Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
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

#include <api/common/PropertyFilter.h>

#include <airdcpp/TimerManager.h>
#include <airdcpp/Util.h>

namespace webserver {
	FilterToken lastFilterToken = 0;

	PropertyFilter::PropertyFilter(const PropertyList& aPropertyTypes) :
		propertyCount(aPropertyTypes.size()),
		defMethod(StringMatch::PARTIAL),
		currentFilterProperty(aPropertyTypes.size()),
		inverse(false),
		usingTypedMethod(false),
		numComparisonMode(LAST),
		propertyTypes(aPropertyTypes),
		id(lastFilterToken++)
	{
	}

	void PropertyFilter::clear() noexcept {
		WLock l(cs);
		if (!matcher.pattern.empty()) {
			matcher.pattern = Util::emptyString;
		}
	}

	void PropertyFilter::setInverse(bool aInverse) noexcept {
		WLock l(cs);
		inverse = aInverse;
	}

	void PropertyFilter::prepare(const string& aPattern, int aMethod, int aProperty) {
		WLock l(cs);
		setPattern(aPattern);
		setFilterMethod(static_cast<StringMatch::Method>(aMethod));
		setFilterProperty(aProperty);

		type = TYPE_TEXT;
		if (currentFilterProperty < 0 || currentFilterProperty >= propertyCount) {
			if (numComparisonMode != LAST) {
				// Attempt to detect the column type 

				type = TYPE_TIME;
				auto ret = prepareTime();

				if (!ret.second) {
					type = TYPE_SIZE;
					ret = prepareSize();
				}

				if (!ret.second) {
					type = TYPE_SPEED;
					ret = prepareSpeed();
				}

				if (!ret.second) {
					// Try generic columns
					type = TYPE_NUMERIC_OTHER;
					numericMatcher = Util::toDouble(matcher.pattern);
				} else {
					// Set the value if parsing succeed
					numericMatcher = ret.first;
				}
			} else {
				type = TYPE_TEXT;
				matcher.setMethod(static_cast<StringMatch::Method>(defMethod));
				matcher.prepare();
			}
		} else if (propertyTypes[currentFilterProperty].filterType == TYPE_SIZE) {
			type = TYPE_SIZE;
			numericMatcher = prepareSize().first;
		} else if (propertyTypes[currentFilterProperty].filterType == TYPE_TIME) {
			type = TYPE_TIME;
			numericMatcher = prepareTime().first;
		} else if (propertyTypes[currentFilterProperty].filterType == TYPE_SPEED) {
			type = TYPE_SPEED;
			numericMatcher = prepareSpeed().first;
		} else if (propertyTypes[currentFilterProperty].filterType == TYPE_NUMERIC_OTHER || propertyTypes[currentFilterProperty].filterType == TYPE_LIST_NUMERIC) {
			type = TYPE_NUMERIC_OTHER;
			numericMatcher = Util::toDouble(matcher.pattern);
		}
	}

	bool PropertyFilter::match(const NumericFunction& numericF, const InfoFunction& infoF, const CustomFilterFunction& aCustomF) const {
		if (empty())
			return true;

		bool hasMatch = false;
		if (currentFilterProperty < 0 || currentFilterProperty >= propertyCount) {
			// Any column
			if (defMethod < StringMatch::METHOD_LAST && numComparisonMode == LAST) {
				for (auto i = 0; i < propertyCount; ++i) {
					if (propertyTypes[i].filterType == type && matchText(i, infoF)) {
						hasMatch = true;
						break;
					}
				}
			} else {
				for (auto i = 0; i < propertyCount; ++i) {
					if (type == propertyTypes[i].filterType && matchNumeric(i, numericF)) {
						hasMatch = true;
						break;
					}
				}
			}
		} else if (propertyTypes[currentFilterProperty].filterType == TYPE_LIST_NUMERIC || propertyTypes[currentFilterProperty].filterType == TYPE_LIST_TEXT) {
			// No default matcher for list properies
			hasMatch = aCustomF(currentFilterProperty, matcher, numericMatcher);
		} else if (propertyTypes[currentFilterProperty].filterType == TYPE_TEXT) {
			hasMatch = matchText(currentFilterProperty, infoF);
		} else {
			hasMatch = matchNumeric(currentFilterProperty, numericF);
		}
		return inverse ? !hasMatch : hasMatch;
	}

	bool PropertyFilter::matchText(int aProperty, const InfoFunction& infoF) const {
		return matcher.match(infoF(aProperty));
	}

	bool PropertyFilter::matchNumeric(int aProperty, const NumericFunction& numericF) const {
		auto toCompare = numericF(aProperty);
		switch (numComparisonMode) {
			case NOT_EQUAL: return toCompare != numericMatcher;

			// inverse the match for time periods (smaller number = older age)
			case GREATER_EQUAL: return type == TYPE_TIME ? toCompare <= numericMatcher : toCompare >= numericMatcher;
			case LESS_EQUAL: return type == TYPE_TIME ? toCompare >= numericMatcher : toCompare <= numericMatcher;
			case GREATER: return type == TYPE_TIME ? toCompare < numericMatcher : toCompare > numericMatcher; break;
			case LESS: return type == TYPE_TIME ? toCompare > numericMatcher : toCompare < numericMatcher; break;
			case EQUAL:
			default: return toCompare == numericMatcher;
		}
	}

	bool PropertyFilter::empty() const noexcept {
		return matcher.pattern.empty();
	}

	void PropertyFilter::setPattern(const std::string& aFilter) noexcept {
		numComparisonMode = LAST;
		auto start = std::string::npos;
		if (!aFilter.empty()) {
			if (aFilter.compare(0, 2, ">=") == 0) {
				numComparisonMode = GREATER_EQUAL;
				start = 2;
			}
			else if (aFilter.compare(0, 2, "<=") == 0) {
				numComparisonMode = LESS_EQUAL;
				start = 2;
			}
			else if (aFilter.compare(0, 2, "==") == 0) {
				numComparisonMode = EQUAL;
				start = 2;
			}
			else if (aFilter.compare(0, 2, "!=") == 0) {
				numComparisonMode = NOT_EQUAL;
				start = 2;
			}
			else if (aFilter[0] == '<') {
				numComparisonMode = LESS;
				start = 1;
			}
			else if (aFilter[0] == '>') {
				numComparisonMode = GREATER;
				start = 1;
			}
			else if (aFilter[0] == '=') {
				numComparisonMode = EQUAL;
				start = 1;
			}
		}

		if (start != std::string::npos) {
			matcher.pattern = aFilter.substr(start, aFilter.length() - start);
			usingTypedMethod = true;
		} else {
			matcher.pattern = aFilter;
			usingTypedMethod = false;
		}
	}

	void PropertyFilter::setFilterProperty(int aFilterProperty) noexcept {
		currentFilterProperty = aFilterProperty;
	}

	// Use doFilter if filtering should be performed
	void PropertyFilter::setFilterMethod(StringMatch::Method aFilterMethod) noexcept {
		if (usingTypedMethod) {
			return;
		}

		defMethod = aFilterMethod;
	}

	pair<double, bool> PropertyFilter::prepareTime() const noexcept {
		size_t end;
		time_t multiplier;
		auto hasType = [&end, this](std::string&& id) {
			end = Util::findSubString(matcher.pattern, id, matcher.pattern.size() - id.size());
			return end != std::string::npos;
		};

		bool hasMatch = true;
		if (hasType("y")) {
			multiplier = 60 * 60 * 24 * 365;
		}
		else if (hasType("m")) {
			multiplier = 60 * 60 * 24 * 30;
		}
		else if (hasType("w")) {
			multiplier = 60 * 60 * 24 * 7;
		}
		else if (hasType("d")) {
			multiplier = 60 * 60 * 24;
		}
		else if (hasType("h")) {
			multiplier = 60 * 60;
		}
		else if (hasType("min")) {
			multiplier = 60;
		}
		else if (hasType("s")) {
			multiplier = 1;
		}
		else {
			hasMatch = false;
			multiplier = 1;
		}

		if (end == std::string::npos) {
			end = matcher.pattern.length();
		}

		time_t ret = Util::toInt64(matcher.pattern.substr(0, end)) * multiplier;
		return make_pair(static_cast<double>(ret > 0 ? GET_TIME() - ret : ret), hasMatch);
	}

	pair<double, bool> PropertyFilter::prepareSize() const noexcept {
		size_t end;
		int64_t multiplier;
		auto hasType = [&end, this](std::string  && id) {
			end = Util::findSubString(matcher.pattern, id, matcher.pattern.size() - id.size());
			return end != std::string::npos;
		};

		if (hasType("TiB")) {
			multiplier = 1024LL * 1024LL * 1024LL * 1024LL;
		}
		else if (hasType("GiB")) {
			multiplier = 1024 * 1024 * 1024;
		}
		else if (hasType("MiB")) {
			multiplier = 1024 * 1024;
		}
		else if (hasType("KiB")) {
			multiplier = 1024;
		}
		else if (hasType("TB")) {
			multiplier = 1000LL * 1000LL * 1000LL * 1000LL;
		}
		else if (hasType("GB")) {
			multiplier = 1000 * 1000 * 1000;
		}
		else if (hasType("MB")) {
			multiplier = 1000 * 1000;
		}
		else if (hasType("KB")) {
			multiplier = 1000;
		}
		else {
			multiplier = 1;
		}

		if (end == std::string::npos) {
			end = matcher.pattern.length();
		}

		return make_pair(Util::toDouble(matcher.pattern.substr(0, end)) * multiplier, multiplier > 1);
	}

	pair<double, bool> PropertyFilter::prepareSpeed() const noexcept {
		size_t end;
		int64_t multiplier;
		auto hasType = [&end, this](std::string  && id) {
			end = Util::findSubString(matcher.pattern, id, matcher.pattern.size() - id.size());
			return end != std::string::npos;
		};

		if (hasType("tbit")) {
			multiplier = 1000LL * 1000LL * 1000LL * 1000LL / 8LL;
		}
		else if (hasType("gbit")) {
			multiplier = 1000 * 1000 * 1000 / 8;
		}
		else if (hasType("mbit")) {
			multiplier = 1000 * 1000 / 8;
		}
		else if (hasType("kbit")) {
			multiplier = 1000 / 8;
		}
		else if (hasType("tibit")) {
			multiplier = 1024LL * 1024LL * 1024LL * 1024LL / 8LL;
		}
		else if (hasType("gibit")) {
			multiplier = 1024 * 1024 * 1024 / 8;
		}
		else if (hasType("mibit")) {
			multiplier = 1024 * 1024 / 8;
		}
		else if (hasType("kibit")) {
			multiplier = 1024 / 8;
		}
		else {
			multiplier = 1;
		}

		if (end == std::string::npos) {
			end = matcher.pattern.length();
		}

		return make_pair(Util::toDouble(matcher.pattern.substr(0, end)) * multiplier, multiplier > 1);
	}

}
