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

#ifndef DCPP_PROPERTYFILTER_H
#define DCPP_PROPERTYFILTER_H

#include <airdcpp/stdinc.h>
#include <airdcpp/CriticalSection.h>

#include <api/common/Property.h>

namespace webserver {
	class PropertyFilter;
	typedef uint32_t FilterToken;


	class PropertyFilter : boost::noncopyable {
	public:
		typedef std::function<std::string(int)> InfoFunction;
		typedef std::function<double(int)> NumericFunction;
		typedef std::function<bool(int, const StringMatch&, double)> CustomFilterFunction;

		typedef shared_ptr<PropertyFilter> Ptr;
		typedef vector<Ptr> List;

		class Matcher {
		public:
			Matcher(const PropertyFilter::Ptr& aFilter);
			~Matcher();

			Matcher(Matcher&) = delete;
			Matcher& operator=(Matcher&) = delete;
			Matcher(Matcher&& rhs) noexcept;
			Matcher& operator=(Matcher&& rhs) = delete;

			typedef vector<Matcher> List;
			static inline bool match(const List& prep, const NumericFunction& aNumericF, const InfoFunction& aStringF, const CustomFilterFunction& aCustomF) {
				return std::all_of(prep.begin(), prep.end(), [&](const Matcher& aMatcher) { return aMatcher.filter->match(aNumericF, aStringF, aCustomF); });
			}

		private:
			PropertyFilter::Ptr filter;
		};

		PropertyFilter(const PropertyList& aPropertyTypes);

		void prepare(const string& aPattern, int aMethod, int aProperty);

		bool empty() const noexcept;
		void clear() noexcept;

		void setInverse(bool aInverse) noexcept;
		bool getInverse() const noexcept { return inverse; }

		FilterToken getId() const noexcept {
			return id;
		}

	private:
		friend class Preparation;

		mutable SharedMutex cs;
		bool match(const NumericFunction& numericF, const InfoFunction& infoF, const CustomFilterFunction& aCustomF) const;
		bool matchText(int aProperty, const InfoFunction& infoF) const;
		bool matchNumeric(int aProperty, const NumericFunction& infoF) const;

		void setPattern(const std::string& aText) noexcept;
		void setFilterProperty(int aFilterProperty) noexcept;
		void setFilterMethod(StringMatch::Method aFilterMethod) noexcept;

		const FilterToken id;
		PropertyList propertyTypes;

		pair<double, bool> prepareSize() const noexcept;
		pair<double, bool> prepareTime() const noexcept;
		pair<double, bool> prepareSpeed() const noexcept;

		StringMatch::Method defMethod;
		int currentFilterProperty;
		FilterPropertyType type;

		const int propertyCount;

		StringMatch matcher;
		double numericMatcher;

		// Hide matching items
		bool inverse;

		// Filtering mode was typed into filtering expression
		bool usingTypedMethod;

		enum FilterMode {
			EQUAL,
			GREATER_EQUAL,
			LESS_EQUAL,
			GREATER,
			LESS,
			NOT_EQUAL,
			LAST
		};

		FilterMode numComparisonMode;
	};
}

#endif