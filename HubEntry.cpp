/*
 * Copyright (C) 2011-2012 AirDC++ Project
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

FavoriteHubEntry::FavoriteHubEntry() noexcept : connect(true), bottom(0), top(0), left(0), right(0), encoding(Text::systemCharset), chatusersplit(0), favnoPM(false), hubShowJoins(false), 
	hubLogMainchat(true), stealth(false), userliststate(true), mode(0), ip(Util::emptyString), chatNotify(false), searchInterval(SETTING(MINIMUM_SEARCH_INTERVAL)), token(Util::rand())  { }

FavoriteHubEntry::FavoriteHubEntry(const HubEntry& rhs) noexcept : name(rhs.getName()), encoding(Text::systemCharset), searchInterval(SETTING(MINIMUM_SEARCH_INTERVAL)),
	description(rhs.getDescription()), connect(true), bottom(0), top(0), left(0), right(0), chatusersplit(0), favnoPM(false), hubShowJoins(false), hubLogMainchat(true), 
	stealth(false), userliststate(true), mode(0), chatNotify(false), token(Util::randInt()) {

		servers.push_back(make_pair(rhs.getServer(), false));
}

FavoriteHubEntry::FavoriteHubEntry(const FavoriteHubEntry& rhs) noexcept : userdescription(rhs.userdescription), name(rhs.getName()), 
	servers(rhs.getServers()), description(rhs.getDescription()), password(rhs.getPassword()), connect(rhs.getConnect()), bottom(0), top(0), left(0), right(0),
	nick(rhs.nick), chatusersplit(rhs.chatusersplit), favnoPM(rhs.favnoPM), hubShowJoins(rhs.hubShowJoins), hubLogMainchat(rhs.hubLogMainchat), stealth(rhs.stealth), searchInterval(rhs.searchInterval),
	userliststate(rhs.userliststate), mode(rhs.mode), ip(rhs.ip), chatNotify(rhs.chatNotify), encoding(rhs.getEncoding()), shareProfile(rhs.getShareProfile()), token(rhs.getToken()) { }

const string& FavoriteHubEntry::getNick(bool useDefault /*true*/) const { 
	return (!nick.empty() || !useDefault) ? nick : SETTING(NICK);
}

void FavoriteHubEntry::setServerStr(const string& aServers) {
	StringTokenizer<string> tmp(aServers, ';');
	servers.clear();
	boost::for_each(tmp.getTokens(), [this](const string& aUrl) { servers.push_back(make_pair(move(aUrl), false)); });
	validateFailOvers();
}

bool FavoriteHubEntry::isAdcHub() const {
	if (servers.empty())
		return false;
	return AirUtil::isAdcHub(servers[0].first);
}

void FavoriteHubEntry::addFailOvers(StringList&& addresses) {
	ServerList tmp;
	boost::for_each(addresses, [this, &tmp](const string& aUrl) { tmp.push_back(make_pair(move(aUrl), false)); });

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

string FavoriteHubEntry::getServerStr() {
	string ret;
	if (!servers.empty()) {
		boost::for_each(servers, [&ret](const ServerBoolPair& sbp) { ret += sbp.first + ";"; });
		ret.pop_back();
	}
	return ret;
}

void FavoriteHubEntry::validateFailOvers() {
	//don't allow mixing NMDC and ADC hubs
	bool adc = isAdcHub();
	servers.erase(remove_if(servers.begin(), servers.end(), [adc](const ServerBoolPair& sbp) { return AirUtil::isAdcHub(sbp.first) != adc; }), servers.end());
}

}