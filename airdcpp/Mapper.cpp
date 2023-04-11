/*
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"
#include "Mapper.h"

namespace dcpp {

const char* Mapper::protocols[PROTOCOL_LAST] = {
	"TCP",
	"UDP"
};

Mapper::Mapper(const string& localIp, bool v6) :
localIp(localIp), v6(v6)
{
}

bool Mapper::open(const string& port, const Protocol protocol, const string& description) {
	if(!add(port, protocol, description))
		return false;

	rules.emplace(port, protocol);
	return true;
}

bool Mapper::close() {
	bool ret = true;

	for(auto& i: rules)
		ret &= remove(i.first, i.second);
	rules.clear();

	return ret;
}

bool Mapper::hasRules() const {
	return !rules.empty();
}

} // namespace dcpp
