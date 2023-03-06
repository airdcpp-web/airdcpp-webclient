/*
 * Copyright (C) 2012-2022 AirDC++ Project
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
#include "Magnet.h"

#include "AirUtil.h"
#include "QueueManager.h"
#include "ShareManager.h"
#include "StringTokenizer.h"
#include "Text.h"
#include "Util.h"

namespace dcpp {

using std::map;



optional<Magnet> Magnet::parseMagnet(const string& aLink, const UserPtr& aTo) noexcept {
	Magnet m(aLink, aTo);
	if (m.fname.empty() || m.fsize == -1 || m.hash.empty()) {
		return nullopt;
	}

	if (m.hash.length() != 39 || !Encoder::isBase32(m.hash.c_str())) {
		return nullopt;
	}

	return m;
}

string Magnet::makeMagnet(const TTHValue& aHash, const string& aFile, int64_t aSize) noexcept {
	string ret = "magnet:?xt=urn:tree:tiger:" + aHash.toBase32();
	if (aSize > 0)
		ret += "&xl=" + Util::toString(aSize);
	return ret + "&dn=" + Util::encodeURI(aFile);
}

Magnet::Magnet(const string& aLink, const UserPtr& aTo) : to(aTo) {
	// official types that are of interest to us
	//  xt = exact topic
	//  xs = exact substitute
	//  as = acceptable substitute
	//  dn = display name
	//  xl = exact length
	StringTokenizer<string> mag(aLink.substr(8), '&');
	map<string, string> hashes;
	for(auto& idx: mag.getTokens()) {
		// break into pairs
		auto pos = idx.find('=');
		if(pos != string::npos) {
			type = Text::toLower(Util::encodeURI(idx.substr(0, pos), true));
			param = Util::encodeURI(idx.substr(pos+1), true);
		} else {
			type = Util::encodeURI(idx, true);
			param.clear();
		}
		// extract what is of value
		if (param.length() == 85 && Util::strnicmp(param.c_str(), "urn:bitprint:", 13) == 0) {
			hashes[type] = param.substr(46);
		} else if (param.length() == 54 && Util::strnicmp(param.c_str(), "urn:tree:tiger:", 15) == 0) {
			hashes[type] = param.substr(15);
		} else if (param.length() == 55 && Util::strnicmp(param.c_str(), "urn:tree:tiger/:", 16) == 0) {
			hashes[type] = param.substr(16);
		} else if (param.length() == 59 && Util::strnicmp(param.c_str(), "urn:tree:tiger/1024:", 20) == 0) {
			hashes[type] = param.substr(20);
		} else if (type.length() == 2 && Util::strnicmp(type.c_str(), "dn", 2) == 0) {
			fname = param;
		} else if (type.length() == 2 && Util::strnicmp(type.c_str(), "xl", 2) == 0) {
			fsize = Util::toInt64(param);
		}
	}

	// pick the most authoritative hash out of all of them.
	if(hashes.find("xt") != hashes.end()) {
		hash = hashes["xt"];
	} else if(hashes.find("xs") != hashes.end()) {
		hash = hashes["xs"];
	} else if(hashes.find("as") != hashes.end()) {
		hash = hashes["as"];
	}

	/*if(!fhash.empty() && Encoder::isBase32(Text::fromT(fhash).c_str())){
		tth = TTHValue(Text::fromT(fhash));
	} */
}

DupeType Magnet::getDupeType() const {
	auto dupe = AirUtil::checkFileDupe(getTTH());
	if (ShareManager::getInstance()->isTempShared(to, getTTH())) {
		dupe = DUPE_SHARE_FULL;
	}

	return dupe;
}

TTHValue Magnet::getTTH() const { 
	return TTHValue(hash); 
}

}