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
#include <airdcpp/StringMatch.h>

#include <api/common/Property.h>

namespace webserver {

	class PropertyFilter : boost::noncopyable {
	public:
		typedef std::function<std::string(int)> InfoFunction;
		typedef std::function<double(int)> NumericFunction;

		struct Preparation {
			Preparation(const StringMatch& aMatch) : stringMatcher(aMatch) { }

			int column;
			int method;
			FilterPropertyType type;

			double numericMatcher;
			const StringMatch& stringMatcher;

			bool matchNumeric(int aColumn, const NumericFunction& numericF) const;
			bool matchText(int aColumn, const InfoFunction& infoF) const;
		};

		PropertyFilter(const PropertyList& aPropertyTypes);

		Preparation prepare();
		bool match(const Preparation& prep, const NumericFunction& numericF, const InfoFunction& infoF) const;

		bool empty() const noexcept;

		//void SetDefaultMatchColumn(int i) { currentMatchProperty = i; } //for setting the match column without column box
		void clear() noexcept;

		void setInverse(bool aInverse) noexcept;
		bool getInverse() const noexcept { return inverse; }


		void setText(const std::string& aText) noexcept;
		void setFilterProperty(int aFilterProperty) noexcept;
		void setFilterMethod(StringMatch::Method aFilterMethod) noexcept;
	private:
		PropertyList propertyTypes;

		pair<double, bool> prepareSize() const noexcept;
		pair<double, bool> prepareTime() const noexcept;
		pair<double, bool> prepareSpeed() const noexcept;

		StringMatch::Method defMethod;
		int currentFilterProperty;

		const int propertyCount;

		StringMatch matcher;

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