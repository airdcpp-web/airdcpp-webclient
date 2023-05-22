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
#include "GeoManager.h"

#include "GeoIP.h"
#include "Util.h"

namespace dcpp {

void GeoManager::init() {
	geo = make_unique<GeoIP>(getDbPath());
}

void GeoManager::update() {
	if (geo) {
		geo->update();
		//geo->rebuild();
	}
}

void GeoManager::close() {
	geo.reset();
}

string GeoManager::getCountry(const string& ip) const {
	if(!ip.empty() && geo.get()) {
		return geo->getCountry(ip);
	}

	return Util::emptyString;
}

string GeoManager::getDbPath() {
	return Util::getPath(Util::PATH_USER_LOCAL) + "country_ip_db.mmdb";
}

} // namespace dcpp
