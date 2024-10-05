/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_VALUE_GENERATOR_H
#define DCPLUSPLUS_DCPP_VALUE_GENERATOR_H

#include <airdcpp/core/header/compiler.h>
#include <airdcpp/core/header/constants.h>
#include <airdcpp/core/header/typedefs.h>

#include <random>

namespace dcpp {

class ValueGenerator  
{
public:
	static void initialize();

	// Calculates TTH value from the lowercase filename and size
	static TTHValue generateDirectoryTTH(const string& aFileName, int64_t aSize) noexcept;

	// Calculates TTH value from the lowercase path
	static TTHValue generatePathId(const string& aPath) noexcept;

	static string toOpenFileName(const string& aFileName, const TTHValue& aTTH) noexcept;


	static int randInt(int min = 0, int max = std::numeric_limits<int>::max()) noexcept;
	static uint32_t rand(uint32_t aMin = 0, uint32_t aMax = std::numeric_limits<uint32_t>::max()) noexcept;

private:
	inline static std::mt19937 mt;
};

} // namespace dcpp

#endif // !defined(UTIL_H)
