/*
* Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

	PropertyFilter::PropertyFilter(const PropertyList& aPropertyTypes) :
		propertyCount(aPropertyTypes.size()),
		defMethod(StringMatch::PARTIAL),
		currentFilterProperty(aPropertyTypes.size()),
		inverse(false),
		usingTypedMethod(false),
		numComparisonMode(LAST),
		propertyTypes(aPropertyTypes)
	{
	}

	void PropertyFilter::clear() noexcept {
		if (!matcher.pattern.empty()) {
			matcher.pattern = Util::emptyString;
		}
	}

	void PropertyFilter::setInverse(bool aInverse) noexcept {
		inverse = aInverse;
	}

	PropertyFilter::Preparation PropertyFilter::prepare() {
		Preparation prep = Preparation(matcher);
		if (empty())
			return prep;

		prep.method = defMethod;
		prep.column = currentFilterProperty;
		prep.type = TYPE_TEXT;
		if (prep.method < StringMatch::METHOD_LAST || prep.column >= propertyCount) {
			if (numComparisonMode != LAST) {
				// Attempt to detect the column type 

				prep.type = TYPE_TIME;
				auto ret = prepareTime();

				if (!ret.second) {
					prep.type = TYPE_SIZE;
					ret = prepareSize();
				}

				if (!ret.second) {
					prep.type = TYPE_SPEED;
					ret = prepareSpeed();
				}

				if (!ret.second) {
					// Try generic columns
					prep.type = TYPE_NUMERIC_OTHER;
					prep.numericMatcher = Util::toDouble(matcher.pattern);
				} else {
					// Set the value if parsing succeed
					prep.numericMatcher = ret.first;
				}
			} else {
				prep.type = TYPE_TEXT;
				matcher.setMethod(static_cast<StringMatch::Method>(prep.method));
				matcher.prepare();
			}
		}
		else if (propertyTypes[prep.column].filterType == TYPE_SIZE) {
			prep.type = TYPE_SIZE;
			prep.numericMatcher = prepareSize().first;
		}
		else if (propertyTypes[prep.column].filterType == TYPE_TIME) {
			prep.type = TYPE_TIME;
			prep.numericMatcher = prepareTime().first;
		}
		else if (propertyTypes[prep.column].filterType == TYPE_SPEED) {
			prep.type = TYPE_SPEED;
			prep.numericMatcher = prepareSpeed().first;
		}
		else if (propertyTypes[prep.column].filterType == TYPE_NUMERIC_OTHER) {
			prep.type = TYPE_NUMERIC_OTHER;
			prep.numericMatcher = Util::toDouble(matcher.pattern);
		}

		return prep;
	}

	bool PropertyFilter::match(const Preparation& prep, const NumericFunction& numericF, const InfoFunction& infoF) const {
		if (empty())
			return true;

		bool hasMatch = false;
		if (prep.column >= propertyCount || prep.column < propertyCount) {
			// Any column
			if (prep.method < StringMatch::METHOD_LAST) {
				for (auto i = 0; i < propertyCount; ++i) {
					if (propertyTypes[i].filterType == prep.type && prep.matchText(i, infoF)) {
						hasMatch = true;
						break;
					}
				}
			} else {
				for (auto i = 0; i < propertyCount; ++i) {
					if (prep.type == propertyTypes[i].filterType && prep.matchNumeric(i, numericF)) {
						hasMatch = true;
						break;
					}
				}
			}
		}
		else if (prep.method < StringMatch::METHOD_LAST || propertyTypes[prep.column].filterType == TYPE_TEXT) {
			hasMatch = prep.matchText(prep.column, infoF);
		}
		else {
			hasMatch = prep.matchNumeric(prep.column, numericF);
		}
		return inverse ? !hasMatch : hasMatch;
	}

	bool PropertyFilter::Preparation::matchNumeric(int aColumn, const NumericFunction& numericF) const {
		auto toCompare = numericF(aColumn);
		switch (method - StringMatch::METHOD_LAST) {
			case EQUAL: return toCompare == numericMatcher;
			case NOT_EQUAL: return toCompare != numericMatcher;

			// inverse the match for time periods (smaller number = older age)
			case GREATER_EQUAL: return type == TYPE_TIME ? toCompare <= numericMatcher : toCompare >= numericMatcher;
			case LESS_EQUAL: return type == TYPE_TIME ? toCompare >= numericMatcher : toCompare <= numericMatcher;
			case GREATER: return type == TYPE_TIME ? toCompare < numericMatcher : toCompare > numericMatcher; break;
			case LESS: return type == TYPE_TIME ? toCompare > numericMatcher : toCompare < numericMatcher; break;
		}

		dcassert(0);
		return false;
	}

	bool PropertyFilter::Preparation::matchText(int aColumn, const InfoFunction& infoF) const {
		return stringMatcher.match(infoF(aColumn));
	}

	bool PropertyFilter::empty() const noexcept {
		return matcher.pattern.empty();
	}

	void PropertyFilter::setText(const std::string& aFilter) noexcept {
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
