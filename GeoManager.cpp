/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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
#include "GeoManager.h"

#include "GeoIP.h"
#include "Util.h"

namespace dcpp {

void GeoManager::init() {
	geo6.reset(new GeoIP(getDbPath(true)));
	geo4.reset(new GeoIP(getDbPath(false)));

	rebuild();
}

void GeoManager::update(bool v6) {
	auto geo = (v6 ? geo6 : geo4).get();
	if(geo) {
		geo->update();
		geo->rebuild();
	}
}

void GeoManager::rebuild() {
	geo6->rebuild();
	geo4->rebuild();
}

void GeoManager::close() {
	geo6.reset();
	geo4.reset();
}

const string& GeoManager::getCountry(const string& ip, int flags) {
	if(!ip.empty()) {

		if((flags & V6) && geo6.get()) {
			const auto& ret = geo6->getCountry(ip);
			if(!ret.empty())
				return ret;
		}

		if((flags & V4) && geo4.get()) {
			return geo4->getCountry(ip);
		}
	}

	return Util::emptyString;
}

string GeoManager::getDbPath(bool v6) {
	return Util::getPath(Util::PATH_USER_LOCAL) + (v6 ? "GeoIPv6.dat" : "GeoIP.dat");
}

} // namespace dcpp
