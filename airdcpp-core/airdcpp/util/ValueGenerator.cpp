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

#include "stdinc.h"
#include <airdcpp/util/ValueGenerator.h>

#include <airdcpp/hash/value/MerkleTree.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/util/text/Text.h>
#include <airdcpp/util/Util.h>


namespace dcpp {

void ValueGenerator::initialize() {
	std::random_device dev;
	mt.seed(dev());
}

string ValueGenerator::toOpenFileName(const string& aFileName, const TTHValue& aTTH) noexcept {
	return aTTH.toBase32() + "_" + PathUtil::validateFileName(aFileName);
}

TTHValue ValueGenerator::generateDirectoryTTH(const string& aFileName, int64_t aSize) noexcept {
	TigerHash tmp;
	string str = Text::toLower(aFileName) + Util::toString(aSize);
	tmp.update(str.c_str(), str.length());
	return TTHValue(tmp.finalize());
}

TTHValue ValueGenerator::generatePathId(const string& aPath) noexcept {
	TigerHash tmp;
	auto str = Text::toLower(aPath);
	tmp.update(str.c_str(), str.length());
	return TTHValue(tmp.finalize());
}

uint32_t ValueGenerator::rand(uint32_t aMin, uint32_t aMax) noexcept {
	std::uniform_int_distribution<uint32_t> dist(aMin, aMax);
	return dist(mt);
}

int ValueGenerator::randInt(int aMin, int aMax) noexcept {
	return static_cast<int>(rand(aMin, aMax));
}

} // namespace dcpp