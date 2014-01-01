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

#ifndef DCPLUSPLUS_DCPP_GEO_MANAGER_H
#define DCPLUSPLUS_DCPP_GEO_MANAGER_H

#include "forward.h"
#include "GeoIP.h"
#include "Singleton.h"

#include <memory>
#include <string>

namespace dcpp {

using std::string;
using std::unique_ptr;

/** Manages IP->country mappings. */
class GeoManager : public Singleton<GeoManager>
{
public:
	/** Prepare the databases and fill internal caches. */
	void init();
	/** Update a database and its internal caches. Call after a new one have been downloaded. */
	void update(bool v6);
	/** Rebuild the internal caches. Call after a change of country settings. */
	void rebuild();
	/** Unload databases and clear internal caches. */
	void close();

	enum { V6 = 1 << 1, V4 = 1 << 2 };
	/** Map an IP address to a country. The flags specify which database(s) to look into. */
	const string& getCountry(const string& ip, int flags = V6 | V4);

	static string getDbPath(bool v6);

private:
	friend class Singleton<GeoManager>;

	// only these 2 for now. in the future, more databases could be added (region / city info...).
	unique_ptr<GeoIP> geo6, geo4;

	GeoManager() { }
	virtual ~GeoManager() { }
};

} // namespace dcpp

#endif
