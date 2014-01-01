/*
 * Copyright (C) 2011-2014 AirDC++ Project
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
#include "HubEntry.h"
#include "AirUtil.h"
#include "StringTokenizer.h"

#include <boost/range/algorithm/for_each.hpp>

namespace dcpp {

FavoriteHubEntry::FavoriteHubEntry() noexcept : connect(true), bottom(0), top(0), left(0), right(0), chatusersplit(0), favnoPM(false),
	stealth(false), userliststate(true), token(Util::randInt())  { }

FavoriteHubEntry::FavoriteHubEntry(const HubEntry& rhs) noexcept : name(rhs.getName()), description(rhs.getDescription()), connect(true),
	bottom(0), top(0), left(0), right(0), chatusersplit(0), favnoPM(false), stealth(false), userliststate(true), token(Util::randInt()) {

		servers.emplace_back(rhs.getServer(), false);
}

void FavoriteHubEntry::setServerStr(const string& aServers) {
	StringTokenizer<string> tmp(aServers, ';');
	servers.clear();
	for(auto& url: tmp.getTokens())
		servers.emplace_back(move(url), false);
	validateFailOvers();
}

bool FavoriteHubEntry::isAdcHub() const {
	if (servers.empty())
		return false;
	return AirUtil::isAdcHub(servers[0].first);
}

void FavoriteHubEntry::addFailOvers(StringList&& addresses) {
	ServerList tmp;
	for(auto& url: addresses) 
		tmp.emplace_back(move(url), false);

	servers.resize(tmp.size()+1);
	move(tmp.begin(), tmp.end(), servers.begin()+1);
	validateFailOvers();
}

void FavoriteHubEntry::blockFailOver(const string& aServer) {
	auto s = find_if(servers.begin(), servers.end(), CompareFirst<string, bool>(aServer));
	if (s != servers.end()) {
		s->second = true;
	}
}

string FavoriteHubEntry::getServerStr() const {
	string ret;
	if (!servers.empty()) {
		for(auto& sbp: servers)
			ret += sbp.first + ";";
		ret.pop_back();
	}
	return ret;
}

void FavoriteHubEntry::validateFailOvers() {
	//don't allow mixing NMDC and ADC hubs
	bool adc = isAdcHub();
	servers.erase(remove_if(servers.begin(), servers.end(), [adc](const ServerBoolPair& sbp) { return AirUtil::isAdcHub(sbp.first) != adc; }), servers.end());

	//no dupes
	servers.erase(std::unique(servers.begin(), servers.end(), [](const ServerBoolPair& sbp1, const ServerBoolPair& sbp2) { return sbp1.first == sbp2.first; }), servers.end());
}

}